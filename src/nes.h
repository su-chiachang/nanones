#ifndef NES_NES_H
#define NES_NES_H

#include "apu.h"
#include "cart.h"
#include "cpu.h"
#include "ppu.h"

enum {
    BTN_A = 0x01, BTN_B = 0x02, BTN_SELECT = 0x04, BTN_START = 0x08,
    BTN_UP = 0x10, BTN_DOWN = 0x20, BTN_LEFT = 0x40, BTN_RIGHT = 0x80,
};

/* Holds pointers into itself: allocate it once (static or heap) and never copy. */
typedef struct Nes {
    Cpu cpu;
    Ppu ppu;
    Apu apu;
    Cart cart;
    uint8_t ram[0x800];

    uint8_t buttons;     /* live state of controller 1, BTN_* bits */
    uint8_t pad_shift;   /* serial shift register */
    bool pad_strobe;

    uint64_t ppu_ticked; /* PPU dots executed so far; kept equal to 3 * CPU cycles between instructions */
    uint64_t apu_ticked; /* APU cycles executed so far */
    int dma_stall;       /* extra CPU cycles queued by an OAM DMA */
} Nes;

int nes_load(Nes *n, const char *rom_path);
void nes_free(Nes *n);
void nes_reset(Nes *n);

/* Runs one CPU instruction and the matching PPU dots. Returns CPU cycles used. */
int nes_step(Nes *n);

/* Runs until the PPU finishes a frame. Returns false if the CPU jammed. */
bool nes_run_frame(Nes *n);

#endif
