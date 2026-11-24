/* Bank switching for the mappers in src/mappers.c, checked on synthetic
 * images: every byte of PRG holds its 8 KiB bank number and every byte of
 * CHR its 1 KiB bank number, so a read tells which bank is mapped in. */
#include "../src/cart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define EQ(got, want, msg) do { long g_ = (long)(got), w_ = (long)(want); \
    if (g_ != w_) { printf("FAIL %s: got %ld, want %ld\n", msg, g_, w_); failures++; } } while (0)

/* prg16: PRG size in 16 KiB units; chr8: CHR size in 8 KiB units (0 = CHR-RAM). */
static int make(Cart *c, int mapper, int prg16, int chr8, uint8_t flags6) {
    size_t prg = (size_t)prg16 * 0x4000, chr = (size_t)chr8 * 0x2000;
    uint8_t *img = calloc(1, 16 + prg + chr);
    memcpy(img, "NES\x1A", 4);
    img[4] = (uint8_t)prg16;
    img[5] = (uint8_t)chr8;
    img[6] = (uint8_t)((mapper & 0x0F) << 4 | flags6);
    img[7] = (uint8_t)(mapper & 0xF0);
    for (size_t i = 0; i < prg; i++) img[16 + i] = (uint8_t)(i / 0x2000);
    for (size_t i = 0; i < chr; i++) img[16 + prg + i] = (uint8_t)(i / 0x400);
    int r = cart_load_mem(c, img, 16 + prg + chr, "synthetic");
    free(img);
    return r;
}

static int P(Cart *c, uint16_t a) { return c->cpu_read(c, a); }
static int C(Cart *c, uint16_t a) { return c->ppu_read(c, a); }
static void W(Cart *c, uint16_t a, uint8_t v) { c->cpu_write(c, a, v); }

static void mmc1_reg(Cart *c, uint16_t a, uint8_t v) {
    for (int i = 0; i < 5; i++) W(c, a, (uint8_t)((v >> i) & 1));
}

static void test_mmc1(void) {
    static Cart c;
    if (make(&c, 1, 16, 16, 0)) { failures++; return; } /* 256K PRG, 128K CHR */
    EQ(P(&c, 0x8000), 0, "mmc1 power-on $8000");
    EQ(P(&c, 0xC000), 30, "mmc1 power-on $C000 = last bank");
    mmc1_reg(&c, 0xE000, 3);
    EQ(P(&c, 0x8000), 6, "mmc1 mode 3 switch $8000");
    EQ(P(&c, 0xFFFF), 31, "mmc1 mode 3 fixed $C000");

    mmc1_reg(&c, 0x8000, 0x08); /* PRG mode 2, CHR 8K, one-screen A */
    EQ(c.mirroring, MIRROR_SINGLE0, "mmc1 mirroring single0");
    EQ(P(&c, 0x8000), 0, "mmc1 mode 2 fixed $8000");
    EQ(P(&c, 0xC000), 6, "mmc1 mode 2 switch $C000");

    mmc1_reg(&c, 0x8000, 0x02); /* PRG mode 0 (32K), vertical */
    EQ(c.mirroring, MIRROR_VERTICAL, "mmc1 mirroring vertical");
    EQ(P(&c, 0x8000), 4, "mmc1 32K mode ignores low bit");
    EQ(P(&c, 0xC000), 6, "mmc1 32K mode upper half");

    mmc1_reg(&c, 0x8000, 0x1F); /* CHR 4K, PRG mode 3, horizontal */
    EQ(c.mirroring, MIRROR_HORIZONTAL, "mmc1 mirroring horizontal");
    mmc1_reg(&c, 0xA000, 5);
    mmc1_reg(&c, 0xC000, 9);
    EQ(C(&c, 0x0000), 20, "mmc1 CHR 4K low");
    EQ(C(&c, 0x1000), 36, "mmc1 CHR 4K high");
    mmc1_reg(&c, 0x8000, 0x0C); /* CHR 8K */
    EQ(C(&c, 0x0000), 16, "mmc1 CHR 8K low (bit 0 ignored)");
    EQ(C(&c, 0x1C00), 23, "mmc1 CHR 8K high");

    /* A reset write mid-sequence discards the partial value. */
    W(&c, 0xE000, 1); W(&c, 0xE000, 1);
    W(&c, 0xE000, 0x80);
    mmc1_reg(&c, 0xE000, 1);
    EQ(P(&c, 0x8000), 2, "mmc1 reset clears shift register");

    W(&c, 0x6123, 0x5A);
    EQ(P(&c, 0x6123), 0x5A, "mmc1 PRG-RAM");
    cart_free(&c);

    /* SUROM: 512K PRG, CHR-RAM; CHR reg 0 bit 4 picks the 256K half. */
    if (make(&c, 1, 32, 0, 0)) { failures++; return; }
    EQ(P(&c, 0xC000), 30, "surom $C000 = last of first half");
    mmc1_reg(&c, 0xA000, 0x10);
    EQ(P(&c, 0xC000), 62, "surom $C000 = last of second half");
    mmc1_reg(&c, 0xE000, 2);
    EQ(P(&c, 0x8000), 36, "surom $8000 in second half");
    c.ppu_write(&c, 0x0123, 0x77);
    EQ(C(&c, 0x0123), 0x77, "surom CHR-RAM");
    cart_free(&c);
}

static void test_discrete(void) {
    static Cart c;

    if (make(&c, 2, 8, 0, 0)) { failures++; return; } /* UxROM 128K */
    W(&c, 0x8000, 3);
    EQ(P(&c, 0x8000), 6, "uxrom switch");
    EQ(P(&c, 0xC000), 14, "uxrom fixed last");
    c.ppu_write(&c, 0x1FFF, 0x42);
    EQ(C(&c, 0x1FFF), 0x42, "uxrom CHR-RAM");
    cart_free(&c);

    if (make(&c, 3, 2, 4, 0)) { failures++; return; } /* CNROM 32K/32K */
    W(&c, 0x8000, 2);
    EQ(C(&c, 0x0000), 16, "cnrom CHR switch");
    EQ(P(&c, 0xC000), 2, "cnrom PRG");
    cart_free(&c);

    if (make(&c, 7, 16, 0, 0)) { failures++; return; } /* AxROM 256K */
    EQ(c.mirroring, MIRROR_SINGLE0, "axrom starts single0");
    W(&c, 0x8000, 0x13);
    EQ(P(&c, 0x8000), 12, "axrom 32K switch");
    EQ(c.mirroring, MIRROR_SINGLE1, "axrom single1");
    cart_free(&c);

    if (make(&c, 7, 1, 0, 0)) { failures++; return; } /* 16K image in a 32K mapper */
    W(&c, 0x8000, 1);
    EQ(P(&c, 0xC000), 0, "axrom undersized image wraps");
    cart_free(&c);

    if (make(&c, 66, 8, 4, 0)) { failures++; return; } /* GxROM */
    W(&c, 0x8000, 0x21);
    EQ(P(&c, 0x8000), 8, "gxrom PRG");
    EQ(C(&c, 0x0000), 8, "gxrom CHR");
    cart_free(&c);

    if (make(&c, 11, 8, 8, 0)) { failures++; return; } /* Color Dreams */
    W(&c, 0x8000, 0x31);
    EQ(P(&c, 0x8000), 4, "color dreams PRG");
    EQ(C(&c, 0x0000), 24, "color dreams CHR");
    cart_free(&c);

    if (make(&c, 71, 8, 0, 0)) { failures++; return; } /* Camerica */
    W(&c, 0xC000, 5);
    EQ(P(&c, 0x8000), 10, "camerica switch");
    EQ(P(&c, 0xC000), 14, "camerica fixed last");
    W(&c, 0x9000, 0x10);
    EQ(c.mirroring, MIRROR_SINGLE1, "camerica one-screen");
    cart_free(&c);
}

static void test_mmc2_mmc4(void) {
    static Cart c;
    if (make(&c, 9, 8, 16, 0)) { failures++; return; } /* MMC2 128K/128K */
    W(&c, 0xA000, 2);
    EQ(P(&c, 0x8000), 2, "mmc2 switch $8000");
    EQ(P(&c, 0xA000), 13, "mmc2 fixed $A000");
    EQ(P(&c, 0xE000), 15, "mmc2 fixed $E000");
    W(&c, 0xB000, 1); W(&c, 0xC000, 2); W(&c, 0xD000, 3); W(&c, 0xE000, 4);
    EQ(C(&c, 0x0000), 8, "mmc2 latch0 starts FE");
    EQ(C(&c, 0x0FD8), 11, "mmc2 triggering read still uses old bank");
    EQ(C(&c, 0x0000), 4, "mmc2 latch0 -> FD");
    C(&c, 0x0FD9);
    C(&c, 0x0FE9); /* MMC2 left table: only $0FD8/$0FE8 trigger */
    EQ(C(&c, 0x0000), 4, "mmc2 latch0 ignores $0FE9");
    C(&c, 0x0FE8);
    EQ(C(&c, 0x0000), 8, "mmc2 latch0 -> FE");
    EQ(C(&c, 0x1000), 16, "mmc2 latch1 starts FE");
    C(&c, 0x1FDA);
    EQ(C(&c, 0x1000), 12, "mmc2 latch1 range -> FD");
    W(&c, 0xF000, 1);
    EQ(c.mirroring, MIRROR_HORIZONTAL, "mmc2 mirroring");
    cart_free(&c);

    if (make(&c, 10, 8, 16, 0x02)) { failures++; return; } /* MMC4, battery */
    W(&c, 0xA000, 3);
    EQ(P(&c, 0x8000), 6, "mmc4 16K switch");
    EQ(P(&c, 0xC000), 14, "mmc4 fixed last");
    W(&c, 0xB000, 1); W(&c, 0xC000, 2);
    C(&c, 0x0FDA);
    EQ(C(&c, 0x0000), 4, "mmc4 latch0 range -> FD");
    EQ(c.battery, 1, "mmc4 battery flag");
    cart_free(&c);
}

static void test_n108(void) {
    static Cart c;
    if (make(&c, 206, 8, 8, 0x01)) { failures++; return; } /* 128K/64K, vertical */
    W(&c, 0x8000, 0xC6); W(&c, 0x8001, 3); /* select bits 6-7 are ignored */
    W(&c, 0x8000, 7); W(&c, 0x8001, 5);
    EQ(P(&c, 0x8000), 3, "n108 R6");
    EQ(P(&c, 0xA000), 5, "n108 R7");
    EQ(P(&c, 0xC000), 14, "n108 fixed second-last");
    EQ(P(&c, 0xE000), 15, "n108 fixed last");
    W(&c, 0x8000, 0); W(&c, 0x8001, 5);
    W(&c, 0x8000, 2); W(&c, 0x8001, 9);
    EQ(C(&c, 0x0000), 4, "n108 2K CHR (low bit ignored)");
    EQ(C(&c, 0x0400), 5, "n108 2K CHR second half");
    EQ(C(&c, 0x1000), 9, "n108 1K CHR");
    W(&c, 0xA000, 1);
    EQ(c.mirroring, MIRROR_VERTICAL, "n108 has no mirroring register");
    cart_free(&c);
}

int main(void) {
    test_mmc1();
    test_discrete();
    test_mmc2_mmc4();
    test_n108();

    static Cart c;
    EQ(make(&c, 5, 2, 1, 0), -1, "unsupported mapper is rejected");

    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("mapper tests passed\n");
    return 0;
}
