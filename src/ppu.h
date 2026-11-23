#ifndef NES_PPU_H
#define NES_PPU_H

#include "cart.h"

#include <stdbool.h>
#include <stdint.h>

#define PPU_W 256
#define PPU_H 240

enum {
    PPUCTRL_NMI = 0x80,
    PPUCTRL_SPRITE_SIZE = 0x20,
    PPUCTRL_BG_TABLE = 0x10,
    PPUCTRL_SPRITE_TABLE = 0x08,
    PPUCTRL_INC32 = 0x04,

    PPUMASK_GRAY = 0x01,
    PPUMASK_BG_LEFT = 0x02,
    PPUMASK_SPR_LEFT = 0x04,
    PPUMASK_BG = 0x08,
    PPUMASK_SPR = 0x10,

    PPUSTATUS_OVERFLOW = 0x20,
    PPUSTATUS_SPRITE0 = 0x40,
    PPUSTATUS_VBLANK = 0x80,
};

typedef struct Ppu {
    /* registers */
    uint8_t ctrl, mask, status, oam_addr;
    uint16_t v, t;      /* loopy current / temporary VRAM address */
    uint8_t fine_x;
    bool w;             /* write toggle shared by $2005/$2006 */
    uint8_t read_buf;
    uint8_t open_bus;

    /* memory */
    uint8_t vram[0x1000]; /* 2 KB normally; 4 KB for four-screen */
    uint8_t palette[32];
    uint8_t oam[256];

    /* position */
    int scanline;       /* 0-239 visible, 240 post, 241-260 vblank, 261 pre-render */
    int dot;            /* 0-340 */
    uint64_t frame;

    /* background pipeline */
    uint8_t nt_latch, at_latch, pat_lo_latch, pat_hi_latch;
    uint16_t sh_pat_lo, sh_pat_hi, sh_at_lo, sh_at_hi;

    /* sprites for the scanline being drawn (filled at dot 257 of the previous line) */
    struct {
        uint8_t attr, x, lo, hi;
    } spr[8];
    int spr_count;
    bool spr0_in_line; /* OAM sprite 0 is in spr[0] */

    /* outputs */
    bool suppress_vbl;  /* a $2002 read one dot before vblank blocks the flag and the NMI */
    bool skip_dot;      /* this pre-render line drops its last dot */
    bool nmi_late;      /* edge came from a $2000 write: too late to be polled by this instruction */
    bool nmi_edge;      /* raised on a rising NMI line; consumed by the system */
    bool frame_done;    /* set once per frame at the start of vblank */
    uint32_t fb[PPU_W * PPU_H]; /* 0xAARRGGBB */

    Cart *cart;
} Ppu;

void ppu_init(Ppu *p, Cart *cart);
void ppu_reset(Ppu *p);
void ppu_tick(Ppu *p);                       /* one PPU dot */
uint8_t ppu_reg_read(Ppu *p, uint16_t addr); /* $2000-$3FFF */
void ppu_reg_write(Ppu *p, uint16_t addr, uint8_t value);

#endif
