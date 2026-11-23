#include "ppu.h"

#include <string.h>

/* 2C02 palette (RGB) */
static const uint32_t SYSTEM_PALETTE[64] = {
    0x545454, 0x001E74, 0x081090, 0x300088, 0x440064, 0x5C0030, 0x540400, 0x3C1800,
    0x202A00, 0x083A00, 0x004000, 0x003C00, 0x00323C, 0x000000, 0x000000, 0x000000,
    0x989698, 0x084CC4, 0x3032EC, 0x5C1EE4, 0x8814B0, 0xA01464, 0x982220, 0x783C00,
    0x545A00, 0x287200, 0x087C00, 0x007628, 0x006678, 0x000000, 0x000000, 0x000000,
    0xECEEEC, 0x4C9AEC, 0x787CEC, 0xB062EC, 0xE454EC, 0xEC58B4, 0xEC6A64, 0xD48820,
    0xA0AA00, 0x74C400, 0x4CD020, 0x38CC6C, 0x38B4CC, 0x3C3C3C, 0x000000, 0x000000,
    0xECEEEC, 0xA8CCEC, 0xBCBCEC, 0xD4B2EC, 0xECAEEC, 0xECAED4, 0xECB4B0, 0xE4C490,
    0xCCD278, 0xB4DE78, 0xA8E290, 0x98E2B4, 0xA0D6E4, 0xA0A2A0, 0x000000, 0x000000,
};

/* ---- PPU address space ---- */

static unsigned nt_index(const Ppu *p, uint16_t addr) {
    unsigned a = (addr - 0x2000u) & 0xFFF;
    unsigned table = a >> 10, offset = a & 0x3FF, page;
    switch (p->cart->mirroring) {
    case MIRROR_HORIZONTAL: page = table >> 1; break;
    case MIRROR_VERTICAL:   page = table & 1; break;
    case MIRROR_SINGLE0:    page = 0; break;
    case MIRROR_SINGLE1:    page = 1; break;
    default:                page = table; break;
    }
    return page * 0x400 + offset;
}

static unsigned pal_index(uint16_t addr) {
    unsigned i = addr & 0x1F;
    if (i >= 16 && (i & 3) == 0) i -= 16; /* $3F10/14/18/1C mirror $3F00/04/08/0C */
    return i;
}

static uint8_t mem_read(Ppu *p, uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x3000 && p->cart->ppu_observe) p->cart->ppu_observe(p->cart, addr);
    if (addr < 0x2000) return p->cart->ppu_read(p->cart, addr);
    if (addr < 0x3F00) return p->vram[nt_index(p, addr)];
    return p->palette[pal_index(addr)];
}

static void mem_write(Ppu *p, uint16_t addr, uint8_t v) {
    addr &= 0x3FFF;
    if (addr < 0x2000) p->cart->ppu_write(p->cart, addr, v);
    else if (addr < 0x3F00) p->vram[nt_index(p, addr)] = v;
    else p->palette[pal_index(addr)] = v;
}

/* ---- lifecycle ---- */

void ppu_init(Ppu *p, Cart *cart) {
    memset(p, 0, sizeof *p);
    p->cart = cart;
}

void ppu_reset(Ppu *p) {
    Cart *cart = p->cart;
    memset(p, 0, sizeof *p);
    p->cart = cart;
}

/* ---- CPU-visible registers ---- */

uint8_t ppu_reg_read(Ppu *p, uint16_t addr) {
    uint8_t v = p->open_bus;
    switch (addr & 7) {
    case 2: /* PPUSTATUS */
        /* Races around the dot where vblank is set (scanline 241, dot 1). */
        if (p->scanline == 241 && p->dot == 0) p->suppress_vbl = true;       /* one dot early */
        if (p->scanline == 241 && p->dot == 1) {                              /* same dot */
            p->status |= PPUSTATUS_VBLANK;
            p->suppress_vbl = true;
        }
        if (p->scanline == 241 && p->dot == 2) p->nmi_edge = false;         /* one dot late */
        /* The pre-render line clears the flags on dot 1; a read on that dot already sees them clear. */
        if (p->scanline == 261 && p->dot == 1)
            p->status &= (uint8_t)~(PPUSTATUS_VBLANK | PPUSTATUS_SPRITE0 | PPUSTATUS_OVERFLOW);
        v = (uint8_t)((p->status & 0xE0) | (p->open_bus & 0x1F));
        p->status &= (uint8_t)~PPUSTATUS_VBLANK;
        p->w = false;
        break;
    case 4: /* OAMDATA */
        v = p->oam[p->oam_addr];
        break;
    case 7: /* PPUDATA */
        if ((p->v & 0x3FFF) >= 0x3F00) {
            v = mem_read(p, p->v);
            p->read_buf = mem_read(p, (uint16_t)(p->v - 0x1000));
        } else {
            v = p->read_buf;
            p->read_buf = mem_read(p, p->v);
        }
        p->v = (uint16_t)(p->v + ((p->ctrl & PPUCTRL_INC32) ? 32 : 1));
        break;
    default:
        break; /* write-only registers return the open bus */
    }
    p->open_bus = v;
    return v;
}

void ppu_reg_write(Ppu *p, uint16_t addr, uint8_t v) {
    p->open_bus = v;
    switch (addr & 7) {
    case 0: { /* PPUCTRL */
        uint8_t old = p->ctrl;
        p->ctrl = v;
        p->t = (uint16_t)((p->t & ~0x0C00) | ((v & 3) << 10));
        bool vbl_visible = (p->status & PPUSTATUS_VBLANK) && !(p->scanline == 261 && p->dot <= 1);
        if (!(v & PPUCTRL_NMI) && p->scanline == 241 && p->dot <= 2)
            p->nmi_edge = false; /* disabling within a dot of vblank cancels the NMI */
        if (!(old & PPUCTRL_NMI) && (v & PPUCTRL_NMI) && vbl_visible) {
            p->nmi_edge = true;
            p->nmi_late = true;
        }
        break;
    }
    case 1: p->mask = v; break;
    case 3: p->oam_addr = v; break;
    case 4: p->oam[p->oam_addr++] = v; break;
    case 5: /* PPUSCROLL */
        if (!p->w) {
            p->fine_x = v & 7;
            p->t = (uint16_t)((p->t & ~0x001F) | (v >> 3));
        } else {
            p->t = (uint16_t)((p->t & ~0x73E0) | ((v & 7) << 12) | ((v & 0xF8) << 2));
        }
        p->w = !p->w;
        break;
    case 6: /* PPUADDR */
        if (!p->w) {
            p->t = (uint16_t)((p->t & 0x00FF) | ((v & 0x3F) << 8));
        } else {
            p->t = (uint16_t)((p->t & 0xFF00) | v);
            p->v = p->t;
        }
        p->w = !p->w;
        break;
    case 7: /* PPUDATA */
        mem_write(p, p->v, v);
        p->v = (uint16_t)(p->v + ((p->ctrl & PPUCTRL_INC32) ? 32 : 1));
        break;
    default: break; /* $2002 is read-only */
    }
}

/* ---- scroll helpers ---- */

static void inc_x(Ppu *p) {
    if ((p->v & 0x001F) == 31) { p->v &= (uint16_t)~0x001F; p->v ^= 0x0400; }
    else p->v++;
}

static void inc_y(Ppu *p) {
    if ((p->v & 0x7000) != 0x7000) { p->v += 0x1000; return; }
    p->v &= (uint16_t)~0x7000;
    unsigned y = (p->v & 0x03E0) >> 5;
    if (y == 29) { y = 0; p->v ^= 0x0800; }
    else if (y == 31) y = 0;
    else y++;
    p->v = (uint16_t)((p->v & ~0x03E0) | (y << 5));
}

static void copy_x(Ppu *p) { p->v = (uint16_t)((p->v & ~0x041F) | (p->t & 0x041F)); }
static void copy_y(Ppu *p) { p->v = (uint16_t)((p->v & ~0x7BE0) | (p->t & 0x7BE0)); }

static void load_shifters(Ppu *p) {
    p->sh_pat_lo = (uint16_t)((p->sh_pat_lo & 0xFF00) | p->pat_lo_latch);
    p->sh_pat_hi = (uint16_t)((p->sh_pat_hi & 0xFF00) | p->pat_hi_latch);
    p->sh_at_lo = (uint16_t)((p->sh_at_lo & 0xFF00) | ((p->at_latch & 1) ? 0xFF : 0));
    p->sh_at_hi = (uint16_t)((p->sh_at_hi & 0xFF00) | ((p->at_latch & 2) ? 0xFF : 0));
}

static void shift(Ppu *p) {
    p->sh_pat_lo <<= 1; p->sh_pat_hi <<= 1;
    p->sh_at_lo <<= 1;  p->sh_at_hi <<= 1;
}

#ifndef SKIP_DECIDE_DOT
#define SKIP_DECIDE_DOT 337
#endif

/* ---- the dot clock ---- */

static void fetch_step(Ppu *p, int dot) {
    shift(p);
    switch ((dot - 1) & 7) {
    case 0:
        load_shifters(p);
        p->nt_latch = mem_read(p, (uint16_t)(0x2000 | (p->v & 0x0FFF)));
        break;
    case 2: {
        uint8_t at = mem_read(p, (uint16_t)(0x23C0 | (p->v & 0x0C00) |
                                            ((p->v >> 4) & 0x38) | ((p->v >> 2) & 0x07)));
        if (p->v & 0x0040) at >>= 4;
        if (p->v & 0x0002) at >>= 2;
        p->at_latch = at & 3;
        break;
    }
    case 4: {
        uint16_t a = (uint16_t)(((p->ctrl & PPUCTRL_BG_TABLE) ? 0x1000 : 0) +
                                (p->nt_latch << 4) + ((p->v >> 12) & 7));
        p->pat_lo_latch = mem_read(p, a);
        break;
    }
    case 6: {
        uint16_t a = (uint16_t)(((p->ctrl & PPUCTRL_BG_TABLE) ? 0x1000 : 0) +
                                (p->nt_latch << 4) + ((p->v >> 12) & 7) + 8);
        p->pat_hi_latch = mem_read(p, a);
        break;
    }
    case 7:
        inc_x(p);
        break;
    }
}

static uint8_t reverse8(uint8_t b) {
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

/* Picks the sprites for scanline `line` and fetches their patterns.
 * Unused slots still fetch tile $FF so the pattern bus (and later MMC3's A12
 * counter) sees the same traffic as the real PPU. */
static void eval_sprites(Ppu *p, int line) {
    int height = (p->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;
    int count = 0;
    p->spr0_in_line = false;

    uint8_t sel[8][4];
    for (int n = 0; n < 64; n++) {
        int row = line - 1 - p->oam[n * 4];
        if (row < 0 || row >= height) continue;
        if (count == 8) { p->status |= PPUSTATUS_OVERFLOW; break; }
        memcpy(sel[count], &p->oam[n * 4], 4);
        if (n == 0) p->spr0_in_line = true;
        count++;
    }

    for (int i = 0; i < 8; i++) {
        uint8_t tile = 0xFF, attr = 0, x = 0xFF;
        int row = 0;
        if (i < count) {
            tile = sel[i][1]; attr = sel[i][2]; x = sel[i][3];
            row = line - 1 - sel[i][0];
            if (attr & 0x80) row = height - 1 - row; /* vertical flip */
        }
        uint16_t addr;
        if (height == 8) {
            addr = (uint16_t)(((p->ctrl & PPUCTRL_SPRITE_TABLE) ? 0x1000 : 0) + tile * 16 + row);
        } else {
            addr = (uint16_t)(((tile & 1) ? 0x1000 : 0) + (tile & 0xFE) * 16 +
                              (row & 7) + (row & 8 ? 16 : 0));
        }
        uint8_t lo = mem_read(p, addr), hi = mem_read(p, (uint16_t)(addr + 8));
        if (i >= count) lo = hi = 0;
        if (attr & 0x40) { lo = reverse8(lo); hi = reverse8(hi); } /* horizontal flip */
        p->spr[i].attr = attr; p->spr[i].x = x;
        p->spr[i].lo = lo; p->spr[i].hi = hi;
    }
    p->spr_count = count;
}

static void draw_pixel(Ppu *p, int x, int y) {
    uint8_t pixel = 0, pal = 0;
    if ((p->mask & PPUMASK_BG) && (x >= 8 || (p->mask & PPUMASK_BG_LEFT))) {
        uint16_t bit = (uint16_t)(0x8000 >> p->fine_x);
        pixel = (uint8_t)((((p->sh_pat_hi & bit) ? 1 : 0) << 1) | ((p->sh_pat_lo & bit) ? 1 : 0));
        pal = (uint8_t)((((p->sh_at_hi & bit) ? 1 : 0) << 1) | ((p->sh_at_lo & bit) ? 1 : 0));
    }
    uint8_t color = pixel ? (uint8_t)((pal << 2) | pixel) : 0;

    if ((p->mask & PPUMASK_SPR) && (x >= 8 || (p->mask & PPUMASK_SPR_LEFT))) {
        for (int i = 0; i < p->spr_count; i++) {
            int col = x - p->spr[i].x;
            if (col < 0 || col > 7) continue;
            uint8_t sp = (uint8_t)((((p->spr[i].hi >> (7 - col)) & 1) << 1) |
                                   ((p->spr[i].lo >> (7 - col)) & 1));
            if (!sp) continue;
            if (i == 0 && p->spr0_in_line && pixel && x != 255)
                p->status |= PPUSTATUS_SPRITE0;
            if (!(p->spr[i].attr & 0x20) || !pixel)
                color = (uint8_t)(0x10 | ((p->spr[i].attr & 3) << 2) | sp);
            break; /* the first opaque sprite pixel wins */
        }
    }
    uint8_t idx = p->palette[color] & 0x3F;
    if (p->mask & PPUMASK_GRAY) idx &= 0x30;
    p->fb[y * PPU_W + x] = 0xFF000000u | SYSTEM_PALETTE[idx];
}

void ppu_tick(Ppu *p) {
    p->cart->ppu_clock++;
    int sl = p->scanline, d = p->dot;
    bool rendering = (p->mask & (PPUMASK_BG | PPUMASK_SPR)) != 0;
    bool render_line = sl < 240 || sl == 261;

    if (render_line) {
        if (rendering) {
            if ((d >= 2 && d <= 257) || (d >= 321 && d <= 337)) {
                if (d == 257) { shift(p); load_shifters(p); }
                else fetch_step(p, d);
            }
            if (d == 256) inc_y(p);
            if (d == 257) copy_x(p);
            if (d == 260) eval_sprites(p, sl == 261 ? 0 : sl + 1);
            if (d == 338 || d == 340)
                p->nt_latch = mem_read(p, (uint16_t)(0x2000 | (p->v & 0x0FFF)));
            if (sl == 261 && d >= 280 && d <= 304) copy_y(p);
        }
        if (sl < 240 && d >= 1 && d <= 256) draw_pixel(p, d - 1, sl);
    }

    if (sl == 241 && d == 1) {
        if (!p->suppress_vbl) {
            p->status |= PPUSTATUS_VBLANK;
            if (p->ctrl & PPUCTRL_NMI) p->nmi_edge = true;
        }
        p->suppress_vbl = false;
        p->frame_done = true;
    }
    if (sl == 261 && d == 1)
        p->status &= (uint8_t)~(PPUSTATUS_VBLANK | PPUSTATUS_SPRITE0 | PPUSTATUS_OVERFLOW);

    /* advance */
    p->dot++;
    /* odd frames skip the last pre-render dot when the background is enabled */
    if (sl == 261 && d == SKIP_DECIDE_DOT) p->skip_dot = (p->frame & 1) && (p->mask & PPUMASK_BG);
    if (sl == 261 && p->dot == 340 && p->skip_dot) p->dot = 341;
    if (p->dot >= 341) {
        p->dot = 0;
        if (++p->scanline > 261) { p->scanline = 0; p->frame++; }
    }
}
