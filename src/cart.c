#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zip.h"

/* ---- mapper 0: NROM ---- */

static uint8_t nrom_cpu_read(Cart *c, uint16_t a) {
    if (a >= 0x8000) return c->prg[(a - 0x8000) & (c->prg_size - 1)];
    if (a >= 0x6000) return c->prg_ram[a - 0x6000];
    return 0;
}

static void nrom_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    if (a >= 0x6000 && a < 0x8000) c->prg_ram[a - 0x6000] = v;
}

static uint8_t chr_read(Cart *c, uint16_t a) { return c->chr[a & (c->chr_size - 1)]; }

static void chr_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_is_ram) c->chr[a & (c->chr_size - 1)] = v;
}

#ifndef MMC3_FILTER
#define MMC3_FILTER 11
#endif

/* ---- mapper 4: MMC3 ---- */

typedef struct {
    uint8_t bank_select;
    uint8_t regs[8];
    uint8_t irq_latch, irq_counter;
    bool irq_reload, irq_enabled;
    bool a12_high;
    uint64_t a12_low_since;
} Mmc3;

static uint32_t mmc3_prg_offset(Cart *c, uint16_t a) {
    Mmc3 *m = c->mstate;
    unsigned nb = (unsigned)(c->prg_size / 0x2000), bank;
    bool swap = m->bank_select & 0x40;
    switch ((a - 0x8000) >> 13) {
    case 0:  bank = swap ? nb - 2 : (m->regs[6] & 0x3F); break;
    case 1:  bank = m->regs[7] & 0x3F; break;
    case 2:  bank = swap ? (m->regs[6] & 0x3F) : nb - 2; break;
    default: bank = nb - 1; break;
    }
    return (bank % nb) * 0x2000u + (a & 0x1FFF);
}

static uint32_t mmc3_chr_offset(Cart *c, uint16_t a) {
    Mmc3 *m = c->mstate;
    unsigned nb = (unsigned)(c->chr_size / 0x400), slot = (a >> 10) & 7, bank;
    if (m->bank_select & 0x80) slot ^= 4;
    if (slot < 2)      bank = (m->regs[0] & 0xFE) + (slot & 1);
    else if (slot < 4) bank = (m->regs[1] & 0xFE) + (slot & 1);
    else               bank = m->regs[slot - 2];
    return (bank % nb) * 0x400u + (a & 0x3FF);
}

static uint8_t mmc3_cpu_read(Cart *c, uint16_t a) {
    if (a >= 0x8000) return c->prg[mmc3_prg_offset(c, a)];
    if (a >= 0x6000) return c->prg_ram[a - 0x6000];
    return 0;
}

static void mmc3_cpu_write(Cart *c, uint16_t a, uint8_t v) {
    Mmc3 *m = c->mstate;
    if (a >= 0x6000 && a < 0x8000) { c->prg_ram[a - 0x6000] = v; return; }
    if (a < 0x8000) return;
    bool odd = a & 1;
    switch ((a >> 13) & 3) {
    case 0: /* $8000-$9FFF */
        if (!odd) m->bank_select = v;
        else m->regs[m->bank_select & 7] = v;
        break;
    case 1: /* $A000-$BFFF */
        if (!odd && c->mirroring != MIRROR_FOUR)
            c->mirroring = (v & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
        break; /* odd: PRG RAM protect, ignored */
    case 2: /* $C000-$DFFF */
        if (!odd) m->irq_latch = v;
        else { m->irq_counter = 0; m->irq_reload = true; }
        break;
    default: /* $E000-$FFFF */
        if (!odd) { m->irq_enabled = false; c->irq = false; }
        else m->irq_enabled = true;
        break;
    }
}

static uint8_t mmc3_ppu_read(Cart *c, uint16_t a) { return c->chr[mmc3_chr_offset(c, a)]; }

static void mmc3_ppu_write(Cart *c, uint16_t a, uint8_t v) {
    if (c->chr_is_ram) c->chr[mmc3_chr_offset(c, a)] = v;
}

/* The counter is clocked by a rising edge of A12 that follows at least ~3 CPU
 * cycles (9 PPU dots) of A12 being low; this filters the short low gaps between
 * sprite pattern fetches. */
static void mmc3_observe(Cart *c, uint16_t a) {
    Mmc3 *m = c->mstate;
    bool high = (a & 0x1000) != 0;
    if (high && !m->a12_high && c->ppu_clock - m->a12_low_since >= MMC3_FILTER) {
        if (m->irq_counter == 0 || m->irq_reload) {
            m->irq_counter = m->irq_latch;
            m->irq_reload = false;
        } else {
            m->irq_counter--;
        }
        if (m->irq_counter == 0 && m->irq_enabled) c->irq = true;
    }
    if (!high && m->a12_high) m->a12_low_since = c->ppu_clock;
    m->a12_high = high;
}

/* ---- loader ---- */

static bool is_pow2(size_t n) { return n && !(n & (n - 1)); }

/* Parses an iNES image held in memory; path is only used in messages. */
static int cart_load_mem(Cart *c, const uint8_t *buf, size_t len, const char *path) {
    memset(c, 0, sizeof *c);

    const uint8_t *h = buf;
    if (len < 16 || memcmp(h, "NES\x1A", 4) != 0) {
        fprintf(stderr, "%s: not an iNES file\n", path);
        return -1;
    }
    if ((h[7] & 0x0C) == 0x08) {
        fprintf(stderr, "%s: NES 2.0 headers are not supported yet\n", path);
        return -1;
    }

    c->mapper = (h[6] >> 4) | (h[7] & 0xF0);
    if (h[6] & 0x08)      c->mirroring = MIRROR_FOUR;
    else if (h[6] & 0x01) c->mirroring = MIRROR_VERTICAL;
    else                  c->mirroring = MIRROR_HORIZONTAL;

    size_t off = 16;
    if (h[6] & 0x04) off += 512; /* skip trainer */

    c->prg_size = (size_t)h[4] * 0x4000;
    c->chr_size = (size_t)h[5] * 0x2000;
    if (c->prg_size == 0) {
        fprintf(stderr, "%s: no PRG ROM\n", path);
        return -1;
    }

    if (off > len || len - off < c->prg_size) {
        fprintf(stderr, "%s: truncated PRG ROM\n", path);
        return -1;
    }
    c->prg = malloc(c->prg_size);
    if (!c->prg) return -1;
    memcpy(c->prg, buf + off, c->prg_size);
    off += c->prg_size;

    if (c->chr_size == 0) {
        c->chr_is_ram = true;
        c->chr_size = 0x2000;
        c->chr = calloc(1, c->chr_size);
    } else {
        if (len - off < c->chr_size) {
            fprintf(stderr, "%s: truncated CHR ROM\n", path);
            cart_free(c);
            return -1;
        }
        c->chr = malloc(c->chr_size);
        if (c->chr) memcpy(c->chr, buf + off, c->chr_size);
    }
    if (!c->chr) { cart_free(c); return -1; }

    switch (c->mapper) {
    case 0:
        if (!is_pow2(c->prg_size) || !is_pow2(c->chr_size)) {
            fprintf(stderr, "%s: NROM with odd bank sizes\n", path);
            cart_free(c);
            return -1;
        }
        c->cpu_read = nrom_cpu_read;
        c->cpu_write = nrom_cpu_write;
        c->ppu_read = chr_read;
        c->ppu_write = chr_write;
        break;
    case 4: {
        Mmc3 *m = calloc(1, sizeof *m);
        if (!m || c->prg_size < 0x4000 || c->chr_size < 0x2000) {
            free(m);
            fprintf(stderr, "%s: bad MMC3 layout\n", path);
            cart_free(c);
            return -1;
        }
        static const uint8_t init[8] = {0, 2, 4, 5, 6, 7, 0, 1};
        memcpy(m->regs, init, sizeof init);
        c->mstate = m;
        c->mstate_size = sizeof *m;
        c->cpu_read = mmc3_cpu_read;
        c->cpu_write = mmc3_cpu_write;
        c->ppu_read = mmc3_ppu_read;
        c->ppu_write = mmc3_ppu_write;
        c->ppu_observe = mmc3_observe;
        break;
    }
    default:
        fprintf(stderr, "%s: mapper %d is not supported yet\n", path, c->mapper);
        cart_free(c);
        return -1;
    }
    return 0;
}

int cart_load(Cart *c, const char *path) {
    memset(c, 0, sizeof *c);

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    uint8_t *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 1 << 20;
            if (cap > 64u << 20) { fprintf(stderr, "%s: file too large\n", path); break; }
            uint8_t *nb = realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }
    int err = ferror(f) || !feof(f);
    fclose(f);
    if (err) { free(buf); fprintf(stderr, "%s: could not read file\n", path); return -1; }

    int r;
    if (zip_is_zip(buf, len)) {
        uint8_t *rom;
        size_t rom_len, name_len;
        const char *name;
        if (zip_extract_rom(buf, len, path, &rom, &rom_len, &name, &name_len) != 0) {
            free(buf);
            return -1;
        }
        char label[512];
        snprintf(label, sizeof label, "%s:%.*s", path, (int)name_len, name);
        r = cart_load_mem(c, rom, rom_len, label);
        free(rom);
    } else {
        r = cart_load_mem(c, buf, len, path);
    }
    free(buf);
    return r;
}

void cart_free(Cart *c) {
    free(c->prg);
    free(c->chr);
    free(c->mstate);
    memset(c, 0, sizeof *c);
}
