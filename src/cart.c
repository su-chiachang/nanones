#include "cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- loader ---- */

static bool is_pow2(size_t n) { return n && !(n & (n - 1)); }

int cart_load(Cart *c, const char *path) {
    memset(c, 0, sizeof *c);

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }

    uint8_t h[16];
    if (fread(h, 1, 16, f) != 16 || memcmp(h, "NES\x1A", 4) != 0) {
        fprintf(stderr, "%s: not an iNES file\n", path);
        fclose(f);
        return -1;
    }
    if ((h[7] & 0x0C) == 0x08) {
        fprintf(stderr, "%s: NES 2.0 headers are not supported yet\n", path);
        fclose(f);
        return -1;
    }

    c->mapper = (h[6] >> 4) | (h[7] & 0xF0);
    if (h[6] & 0x08)      c->mirroring = MIRROR_FOUR;
    else if (h[6] & 0x01) c->mirroring = MIRROR_VERTICAL;
    else                  c->mirroring = MIRROR_HORIZONTAL;

    if (h[6] & 0x04) fseek(f, 512, SEEK_CUR); /* skip trainer */

    c->prg_size = (size_t)h[4] * 0x4000;
    c->chr_size = (size_t)h[5] * 0x2000;
    if (c->prg_size == 0) {
        fprintf(stderr, "%s: no PRG ROM\n", path);
        fclose(f);
        return -1;
    }

    c->prg = malloc(c->prg_size);
    if (!c->prg || fread(c->prg, 1, c->prg_size, f) != c->prg_size) {
        fprintf(stderr, "%s: truncated PRG ROM\n", path);
        fclose(f);
        cart_free(c);
        return -1;
    }

    if (c->chr_size == 0) {
        c->chr_is_ram = true;
        c->chr_size = 0x2000;
        c->chr = calloc(1, c->chr_size);
    } else {
        c->chr = malloc(c->chr_size);
        if (c->chr && fread(c->chr, 1, c->chr_size, f) != c->chr_size) {
            fprintf(stderr, "%s: truncated CHR ROM\n", path);
            fclose(f);
            cart_free(c);
            return -1;
        }
    }
    fclose(f);
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
    default:
        fprintf(stderr, "%s: mapper %d is not supported yet\n", path, c->mapper);
        cart_free(c);
        return -1;
    }
    return 0;
}

void cart_free(Cart *c) {
    free(c->prg);
    free(c->chr);
    free(c->mstate);
    memset(c, 0, sizeof *c);
}
