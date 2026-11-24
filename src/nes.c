#include "nes.h"

#include <stdlib.h>
#include <string.h>

#ifndef PPU_ALIGN
#define PPU_ALIGN 1
#endif

/* Brings the PPU up to the dot at which the CPU is touching it, so register
 * reads and writes see the right scanline/dot even inside a long instruction. */
static void ppu_catch_up(Nes *n, uint64_t target) {
    while (n->ppu_ticked < target) { ppu_tick(&n->ppu); n->ppu_ticked++; }
}

#ifndef MAPPER_POLL_BACK
#define MAPPER_POLL_BACK 1 /* CPU cycles before the end of an instruction at which the mapper IRQ is sampled */
#endif

#ifndef APU_POLL_BACK
#define APU_POLL_BACK 2
#endif

#ifndef APU_ALIGN
#define APU_ALIGN 0
#endif

static void apu_catch_up(Nes *n, uint64_t target) {
    while (n->apu_ticked < target) { apu_tick(&n->apu); n->apu_ticked++; }
}

static uint8_t bus_read(void *ctx, uint16_t addr) {
    Nes *n = ctx;
    if (addr < 0x2000) return n->ram[addr & 0x7FF];
    if (addr < 0x4000) {
        ppu_catch_up(n, n->cpu.access_cycle * 3 + PPU_ALIGN);
        return ppu_reg_read(&n->ppu, addr);
    }
    if (addr == 0x4016) {
        uint8_t bit = n->pad_strobe ? (n->buttons & 1) : (n->pad_shift & 1);
        if (!n->pad_strobe) n->pad_shift = (uint8_t)((n->pad_shift >> 1) | 0x80);
        return (uint8_t)(0x40 | bit); /* bit 6 mimics open bus on the data lines */
    }
    if (addr == 0x4015) {
        apu_catch_up(n, n->cpu.access_cycle + APU_ALIGN);
        return apu_read_status(&n->apu);
    }
    if (addr < 0x4020) return 0; /* second pad: not implemented */
    return n->cart.cpu_read(&n->cart, addr);
}

static void bus_write(void *ctx, uint16_t addr, uint8_t v) {
    Nes *n = ctx;
    if (addr < 0x2000) { n->ram[addr & 0x7FF] = v; return; }
    if (addr < 0x4000) {
        ppu_catch_up(n, n->cpu.access_cycle * 3 + PPU_ALIGN);
        ppu_reg_write(&n->ppu, addr, v);
        return;
    }
    if (addr == 0x4014) { /* OAM DMA */
        uint16_t base = (uint16_t)(v << 8);
        for (int i = 0; i < 256; i++)
            n->ppu.oam[(uint8_t)(n->ppu.oam_addr + i)] = bus_read(n, (uint16_t)(base + i));
        n->dma_stall = 513 + (int)(n->cpu.cycles & 1);
        return;
    }
    if (addr == 0x4016) { /* strobe */
        n->pad_strobe = v & 1;
        if (n->pad_strobe) n->pad_shift = n->buttons;
        return;
    }
    if (addr < 0x4020) {
        apu_catch_up(n, n->cpu.access_cycle + APU_ALIGN);
        apu_write(&n->apu, addr, v);
        return;
    }
    n->cart.cpu_write(&n->cart, addr, v);
}

static uint8_t dmc_read(void *ctx, uint16_t addr) { return bus_read(ctx, addr); }

int nes_load(Nes *n, const char *rom_path) {
    memset(n, 0, sizeof *n);
    if (cart_load(&n->cart, rom_path) != 0) return -1;
    ppu_init(&n->ppu, &n->cart);
    apu_init(&n->apu, 48000.0, dmc_read, n);
    cpu_init(&n->cpu, bus_read, bus_write, n);
    nes_reset(n);
    return 0;
}

void nes_clone(Nes *dst, const Nes *src) {
    void *mstate = dst->cart.mstate;
    uint8_t *chr = dst->cart.chr;
    bool had_chr = dst->cart.chr_is_ram && chr;
    memcpy(dst, src, sizeof *dst);
    if (src->cart.mstate) {
        if (!mstate) mstate = malloc(src->cart.mstate_size);
        memcpy(mstate, src->cart.mstate, src->cart.mstate_size);
        dst->cart.mstate = mstate;
    }
    if (src->cart.chr_is_ram) {
        if (!had_chr) chr = malloc(src->cart.chr_size);
        memcpy(chr, src->cart.chr, src->cart.chr_size);
        dst->cart.chr = chr;
    }
    dst->cpu.ctx = dst;
    dst->apu.ctx = dst;
    dst->ppu.cart = &dst->cart;
}

void nes_free(Nes *n) { cart_free(&n->cart); }

void nes_reset(Nes *n) {
    memset(n->ram, 0, sizeof n->ram);
    ppu_reset(&n->ppu);
    n->pad_shift = 0;
    n->pad_strobe = false;
    n->dma_stall = 0;
    cpu_reset(&n->cpu);
    /* The reset sequence takes 7 CPU cycles, during which the PPU runs 21 dots. */
    n->apu_ticked = 7;
    n->ppu_ticked = 0;
    ppu_catch_up(n, 21);
}

int nes_step(Nes *n) {
    int cycles = cpu_step(&n->cpu);
    if (n->dma_stall) {
        cycles += n->dma_stall;
        n->cpu.cycles += (uint64_t)n->dma_stall;
        n->dma_stall = 0;
    }
    /* Interrupts are polled at the end of the second-to-last cycle: an edge or
     * level that appears only during the final cycle waits one more instruction. */
    uint64_t end = n->cpu.cycles * 3;
    int early = n->cpu.poll_early ? 1 : 0;
    int back = 1 + early; /* CPU cycles before the end at which NMI is polled */
    if (cycles > MAPPER_POLL_BACK + early) ppu_catch_up(n, end - 3 * (uint64_t)(MAPPER_POLL_BACK + early));
    bool mapper_irq = n->cart.irq;
    if (cycles > back) ppu_catch_up(n, end - 3 * (uint64_t)back);
    bool nmi = n->ppu.nmi_edge && !n->ppu.nmi_late;
    n->ppu.nmi_late = false;
    if (cycles > 0 && n->cpu.cycles >= (uint64_t)(APU_POLL_BACK + early))
        apu_catch_up(n, n->cpu.cycles - (uint64_t)(APU_POLL_BACK + early));
    n->cpu.irq_line = mapper_irq || apu_irq(&n->apu);
    apu_catch_up(n, n->cpu.cycles);
    ppu_catch_up(n, end);
    if (nmi) {
        n->ppu.nmi_edge = false;
        cpu_nmi(&n->cpu);
    }
    return cycles;
}

bool nes_run_frame(Nes *n) {
    n->ppu.frame_done = false;
    while (!n->ppu.frame_done) {
        nes_step(n);
        if (n->cpu.jammed) return false;
    }
    return true;
}
