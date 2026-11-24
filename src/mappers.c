/* Discrete-logic and simple ASIC mappers:
 *   1 MMC1 (SxROM, incl. SUROM 512 KiB PRG)   2 UxROM   3 CNROM   7 AxROM
 *   9 MMC2 (PxROM)   10 MMC4 (FxROM)   11 Color Dreams   66 GxROM
 *   71 Camerica BF909x   206 Namco 108 / DxROM
 * Bank offsets wrap modulo the ROM size, so undersized images never fault.
 * Not modelled: bus conflicts, MMC1's ignore-consecutive-write quirk (the CPU
 * core does not issue RMW dummy writes), SOROM/SXROM PRG-RAM banking. */
#include "mappers.h"

#include <stdlib.h>
#include <string.h>

/* ---- shared helpers ---- */

static uint8_t prg_bank(const Cart *c, size_t bank, size_t size, uint16_t a) {
    return c->prg[(bank * size + (a & (size - 1))) % c->prg_size];
}

static size_t chr_index(const Cart *c, size_t bank, size_t size, uint16_t a) {
    return (bank * size + (a & (size - 1))) % c->chr_size;
}

static uint8_t ram_read(Cart *c, uint16_t a) {
    return a >= 0x6000 && a < 0x8000 ? c->prg_ram[a - 0x6000] : 0;
}

/* Writes below $8000 go to PRG-RAM (if in range); returns true if handled. */
static bool ram_write(Cart *c, uint16_t a, uint8_t v) {
    if (a >= 0x8000) return false;
    if (a >= 0x6000) c->prg_ram[a - 0x6000] = v;
    return true;
}

static void set_mirroring(Cart *c, Mirroring m) {
    if (c->mirroring != MIRROR_FOUR) c->mirroring = m;
}

static size_t prg_banks(const Cart *c, size_t size) {
    size_t n = c->prg_size / size;
    return n ? n : 1;
}

/* Generic register file for the simple boards: r[0] PRG bank, r[1] CHR bank. */
typedef struct { uint8_t r[2]; } Simple;

#define SIMPLE(c) ((Simple *)(c)->mstate)

static uint8_t simple_chr_read(Cart *c, uint16_t a) {
    return c->chr[chr_index(c, SIMPLE(c)->r[1], 0x2000, a)];
}
static void simple_chr_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_is_ram) c->chr[chr_index(c, SIMPLE(c)->r[1], 0x2000, a)] = v;
}

/* ---- 1: MMC1 ---- */

typedef struct {
    uint8_t shift, count;
    uint8_t control, chr0, chr1, prg;
} Mmc1;

static uint8_t mmc1_cpu_read(Cart *c, uint16_t a) {
    if (a < 0x8000) return ram_read(c, a);
    Mmc1 *m = c->mstate;
    /* SUROM: CHR reg 0 bit 4 picks the 256 KiB half of a 512 KiB PRG ROM. */
    size_t outer = 0, inner = c->prg_size;
    if (c->prg_size > 0x40000) { inner = 0x40000; outer = (size_t)((m->chr0 >> 4) & 1) * 0x40000; }
    size_t nb = inner / 0x4000 ? inner / 0x4000 : 1, bank = m->prg & 0x0F;
    bool hi = a >= 0xC000;
    switch ((m->control >> 2) & 3) {
    case 0: case 1: bank = (bank & ~1u) + hi; break;       /* 32 KiB */
    case 2:         bank = hi ? bank : 0; break;           /* $8000 fixed to first */
    default:        bank = hi ? nb - 1 : bank; break;      /* $C000 fixed to last */
    }
    return c->prg[(outer + (bank % nb) * 0x4000 + (a & 0x3FFF)) % c->prg_size];
}

static size_t mmc1_chr(const Cart *c, uint16_t a) {
    const Mmc1 *m = c->mstate;
    if (m->control & 0x10) return chr_index(c, a < 0x1000 ? m->chr0 : m->chr1, 0x1000, a);
    return chr_index(c, (size_t)(m->chr0 & 0x1E) + (a >> 12 & 1), 0x1000, a);
}
static uint8_t mmc1_ppu_read(Cart *c, uint16_t a) { return c->chr[mmc1_chr(c, a)]; }
static void mmc1_ppu_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_is_ram) c->chr[mmc1_chr(c, a)] = v;
}

static void mmc1_apply_mirroring(Cart *c) {
    static const Mirroring mode[4] = {MIRROR_SINGLE0, MIRROR_SINGLE1, MIRROR_VERTICAL, MIRROR_HORIZONTAL};
    set_mirroring(c, mode[((Mmc1 *)c->mstate)->control & 3]);
}

static void mmc1_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v)) return;
    Mmc1 *m = c->mstate;
    if (v & 0x80) { /* reset the shift register, force PRG mode 3 */
        m->shift = m->count = 0;
        m->control |= 0x0C;
        return;
    }
    m->shift |= (uint8_t)((v & 1) << m->count);
    if (++m->count < 5) return;
    switch ((a >> 13) & 3) { /* the fifth write's address picks the register */
    case 0: m->control = m->shift; mmc1_apply_mirroring(c); break;
    case 1: m->chr0 = m->shift; break;
    case 2: m->chr1 = m->shift; break;
    default: m->prg = m->shift; break;
    }
    m->shift = m->count = 0;
}

/* ---- 2: UxROM ---- */

static uint8_t uxrom_cpu_read(Cart *c, uint16_t a) {
    if (a < 0x8000) return ram_read(c, a);
    size_t bank = a < 0xC000 ? SIMPLE(c)->r[0] : prg_banks(c, 0x4000) - 1;
    return prg_bank(c, bank, 0x4000, a);
}
static void uxrom_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (!ram_write(c, a, v)) SIMPLE(c)->r[0] = v;
}

/* ---- 3: CNROM ---- */

static uint8_t cnrom_cpu_read(Cart *c, uint16_t a) {
    return a < 0x8000 ? ram_read(c, a) : prg_bank(c, 0, 0x8000, a);
}
static void cnrom_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (!ram_write(c, a, v)) SIMPLE(c)->r[1] = v;
}

/* ---- 32 KiB PRG boards: 7 AxROM, 11 Color Dreams, 66 GxROM ---- */

static uint8_t prg32_cpu_read(Cart *c, uint16_t a) {
    return a < 0x8000 ? ram_read(c, a) : prg_bank(c, SIMPLE(c)->r[0], 0x8000, a);
}
static void axrom_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v)) return;
    SIMPLE(c)->r[0] = v & 0x0F;
    set_mirroring(c, (v & 0x10) ? MIRROR_SINGLE1 : MIRROR_SINGLE0);
}
static void colordreams_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v)) return;
    SIMPLE(c)->r[0] = v & 0x03;
    SIMPLE(c)->r[1] = v >> 4;
}
static void gxrom_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v)) return;
    SIMPLE(c)->r[0] = (v >> 4) & 0x03;
    SIMPLE(c)->r[1] = v & 0x03;
}

/* ---- 71: Camerica BF909x ---- */

static void camerica_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v)) return;
    if (a >= 0xC000) SIMPLE(c)->r[0] = v;
    else if (a >= 0x9000 && a < 0xA000) /* BF9097 (Fire Hawk) one-screen select */
        set_mirroring(c, (v & 0x10) ? MIRROR_SINGLE1 : MIRROR_SINGLE0);
}

/* ---- 9: MMC2, 10: MMC4 ---- */

typedef struct {
    uint8_t prg;
    uint8_t chr[4]; /* $0000 FD, $0000 FE, $1000 FD, $1000 FE */
    bool latch_fe[2];
    bool mmc4;
} Mmc2;

static uint8_t mmc2_cpu_read(Cart *c, uint16_t a) {
    if (a < 0x8000) return ram_read(c, a);
    Mmc2 *m = c->mstate;
    if (m->mmc4) {
        size_t bank = a < 0xC000 ? m->prg : prg_banks(c, 0x4000) - 1;
        return prg_bank(c, bank, 0x4000, a);
    }
    size_t nb = prg_banks(c, 0x2000), slot = (a - 0x8000) >> 13;
    size_t bank = slot == 0 ? m->prg : nb - 4 + slot;
    return prg_bank(c, bank, 0x2000, a);
}

static void mmc2_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v)) return;
    Mmc2 *m = c->mstate;
    switch (a >> 12) {
    case 0xA: m->prg = v & 0x0F; break;
    case 0xB: m->chr[0] = v & 0x1F; break;
    case 0xC: m->chr[1] = v & 0x1F; break;
    case 0xD: m->chr[2] = v & 0x1F; break;
    case 0xE: m->chr[3] = v & 0x1F; break;
    case 0xF: set_mirroring(c, (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL); break;
    default: break;
    }
}

static size_t mmc2_chr(const Cart *c, uint16_t a) {
    const Mmc2 *m = c->mstate;
    int half = (a >> 12) & 1;
    return chr_index(c, m->chr[half * 2 + m->latch_fe[half]], 0x1000, a);
}

/* The latch flips after the fetch, so the triggering tile uses the old bank. */
static uint8_t mmc2_ppu_read(Cart *c, uint16_t a) {
    Mmc2 *m = c->mstate;
    uint8_t v = c->chr[mmc2_chr(c, a)];
    uint16_t t = a & 0x1FFF;
    if (!m->mmc4 && t < 0x1000) { /* MMC2 left table: exact addresses only */
        if (t == 0x0FD8) m->latch_fe[0] = false;
        else if (t == 0x0FE8) m->latch_fe[0] = true;
    } else {
        int half = t >> 12;
        if ((t & 0x0FF8) == 0x0FD8) m->latch_fe[half] = false;
        else if ((t & 0x0FF8) == 0x0FE8) m->latch_fe[half] = true;
    }
    return v;
}
static void mmc2_ppu_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_is_ram) c->chr[mmc2_chr(c, a)] = v;
}

/* ---- 206: Namco 108 / DxROM (MMC3 banking without IRQ or mirroring) ---- */

typedef struct { uint8_t select, regs[8]; } N108;

static uint8_t n108_cpu_read(Cart *c, uint16_t a) {
    if (a < 0x8000) return ram_read(c, a);
    N108 *m = c->mstate;
    size_t nb = prg_banks(c, 0x2000), slot = (a - 0x8000) >> 13;
    size_t bank = slot == 0 ? m->regs[6] : slot == 1 ? m->regs[7] : nb - 4 + slot;
    return prg_bank(c, bank, 0x2000, a);
}
static void n108_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (ram_write(c, a, v) || a >= 0xA000) return;
    N108 *m = c->mstate;
    if (!(a & 1)) m->select = v & 7;
    else m->regs[m->select] = v & 0x3F;
}
static size_t n108_chr(const Cart *c, uint16_t a) {
    const N108 *m = c->mstate;
    unsigned slot = (a >> 10) & 7;
    size_t bank = slot < 4 ? (size_t)(m->regs[slot >> 1] & 0x3E) + (slot & 1) : m->regs[slot - 2];
    return chr_index(c, bank, 0x400, a);
}
static uint8_t n108_ppu_read(Cart *c, uint16_t a) { return c->chr[n108_chr(c, a)]; }
static void n108_ppu_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_is_ram) c->chr[n108_chr(c, a)] = v;
}

/* ---- setup ---- */

static int alloc_state(Cart *c, size_t size) {
    c->mstate = calloc(1, size);
    c->mstate_size = size;
    return c->mstate ? 0 : -1;
}

int mappers_setup(Cart *c) {
    switch (c->mapper) {
    case 1: {
        if (alloc_state(c, sizeof(Mmc1))) return -1;
        ((Mmc1 *)c->mstate)->control = 0x0C; /* power-on: PRG mode 3 */
        c->cpu_read = mmc1_cpu_read;
        c->cpu_write = mmc1_cpu_write;
        c->ppu_read = mmc1_ppu_read;
        c->ppu_write = mmc1_ppu_write;
        return 0;
    }
    case 2: case 3: case 7: case 11: case 66: case 71:
        if (alloc_state(c, sizeof(Simple))) return -1;
        c->ppu_read = simple_chr_read;
        c->ppu_write = simple_chr_write;
        switch (c->mapper) {
        case 2:  c->cpu_read = uxrom_cpu_read; c->cpu_write = uxrom_cpu_write; break;
        case 3:  c->cpu_read = cnrom_cpu_read; c->cpu_write = cnrom_cpu_write; break;
        case 7:  c->cpu_read = prg32_cpu_read; c->cpu_write = axrom_cpu_write;
                 set_mirroring(c, MIRROR_SINGLE0); break;
        case 11: c->cpu_read = prg32_cpu_read; c->cpu_write = colordreams_cpu_write; break;
        case 66: c->cpu_read = prg32_cpu_read; c->cpu_write = gxrom_cpu_write; break;
        default: c->cpu_read = uxrom_cpu_read; c->cpu_write = camerica_cpu_write; break;
        }
        return 0;
    case 9: case 10: {
        if (alloc_state(c, sizeof(Mmc2))) return -1;
        Mmc2 *m = c->mstate;
        m->mmc4 = c->mapper == 10;
        m->latch_fe[0] = m->latch_fe[1] = true;
        c->cpu_read = mmc2_cpu_read;
        c->cpu_write = mmc2_cpu_write;
        c->ppu_read = mmc2_ppu_read;
        c->ppu_write = mmc2_ppu_write;
        return 0;
    }
    case 206:
        if (alloc_state(c, sizeof(N108))) return -1;
        c->cpu_read = n108_cpu_read;
        c->cpu_write = n108_cpu_write;
        c->ppu_read = n108_ppu_read;
        c->ppu_write = n108_ppu_write;
        return 0;
    default:
        return 1;
    }
}
