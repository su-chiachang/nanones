#include "nes.h"

#include <string.h>

static uint8_t bus_read(void *ctx, uint16_t addr) {
    Nes *n = ctx;
    if (addr < 0x2000) return n->ram[addr & 0x7FF];
    if (addr < 0x4000) return ppu_reg_read(&n->ppu, addr);
    if (addr == 0x4016) {
        uint8_t bit = n->pad_strobe ? (n->buttons & 1) : (n->pad_shift & 1);
        if (!n->pad_strobe) n->pad_shift = (uint8_t)((n->pad_shift >> 1) | 0x80);
        return (uint8_t)(0x40 | bit); /* bit 6 mimics open bus on the data lines */
    }
    if (addr < 0x4020) return 0; /* APU / second pad: not implemented yet */
    return n->cart.cpu_read(&n->cart, addr);
}

static void bus_write(void *ctx, uint16_t addr, uint8_t v) {
    Nes *n = ctx;
    if (addr < 0x2000) { n->ram[addr & 0x7FF] = v; return; }
    if (addr < 0x4000) { ppu_reg_write(&n->ppu, addr, v); return; }
    if (addr == 0x4014) { /* OAM DMA */
        uint16_t base = (uint16_t)(v << 8);
        for (int i = 0; i < 256; i++)
            n->ppu.oam[(uint8_t)(n->ppu.oam_addr + i)] = bus_read(n, (uint16_t)(base + i));
        n->dma_stall = 513 + (int)(n->cpu.cycles & 1);
        return;
    }
    if (addr == 0x4016) {
        n->pad_strobe = v & 1;
        if (n->pad_strobe) n->pad_shift = n->buttons;
        return;
    }
    if (addr < 0x4020) return; /* APU: not implemented yet */
    n->cart.cpu_write(&n->cart, addr, v);
}

int nes_load(Nes *n, const char *rom_path) {
    memset(n, 0, sizeof *n);
    if (cart_load(&n->cart, rom_path) != 0) return -1;
    ppu_init(&n->ppu, &n->cart);
    cpu_init(&n->cpu, bus_read, bus_write, n);
    nes_reset(n);
    return 0;
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
    for (int i = 0; i < 21; i++) ppu_tick(&n->ppu);
}

int nes_step(Nes *n) {
    int cycles = cpu_step(&n->cpu);
    if (n->dma_stall) {
        cycles += n->dma_stall;
        n->cpu.cycles += (uint64_t)n->dma_stall;
        n->dma_stall = 0;
    }
    for (int i = 0; i < cycles * 3; i++) ppu_tick(&n->ppu);
    n->cpu.irq_line = n->cart.irq;
    if (n->ppu.nmi_edge) {
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
