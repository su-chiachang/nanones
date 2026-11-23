/* Runs tests/sprite_test.nes (built by make_sprite_rom.py) and checks sprite
 * 0 hit, flips, bg priority and sprite palettes against known pixels. */
#include "../src/nes.h"

#include <stdio.h>

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static uint32_t px(const Nes *n, int x, int y) { return n->ppu.fb[y * PPU_W + x] & 0xFFFFFF; }

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s sprite_test.nes\n", argv[0]); return 2; }
    static Nes nes;
    if (nes_load(&nes, argv[1]) != 0) return 2;
    for (int i = 0; i < 5; i++) nes_run_frame(&nes);

    const uint32_t BLACK = 0x000000, BLUE = 0x4C9AEC, YEL = 0xA0AA00, GREEN = 0x4CD020, RED = 0x982220;
    const int y0 = 0x20; /* sprites with OAM y=$1F start on line $20 */

    CHECK(nes.ram[0] == 1, "sprite 0 hit was observed");
    CHECK(px(&nes, 0x28, y0) == BLUE, "sprite 0 draws in front of the bg block");
    CHECK(px(&nes, 0x50, y0 + 1) == BLACK, "h-flip: column 0 empty on row 1");
    CHECK(px(&nes, 0x57, y0 + 1) == BLUE, "h-flip: column 7 set on row 1");
    CHECK(px(&nes, 0x70, y0) == BLUE, "v-flip: row 0 shows the old row 7 (col 0)");
    CHECK(px(&nes, 0x77, y0) == YEL, "v-flip: old row 7 colour-2 dot lands in col 7");
    CHECK(px(&nes, 0x90 + 7, y0) == BLUE, "h+v flip: col 7");
    CHECK(px(&nes, 0x90, y0) == YEL, "h+v flip: col 0 holds the colour-2 dot");
    CHECK(px(&nes, 0xB0, y0) == RED, "behind-bg sprite is hidden by an opaque bg pixel");
    CHECK(px(&nes, 0xD0, y0) == GREEN, "sprite palette 1 is used");
    CHECK(px(&nes, 0x28, y0 + 8) == BLACK, "nothing below the 8-pixel sprite");

    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("sprite tests passed\n");
    nes_free(&nes);
    return 0;
}
