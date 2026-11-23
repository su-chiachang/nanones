/* Runs tests/mmc3_irq_test.nes (built by make_mmc3_rom.py): checks MMC3 CHR
 * banking and that the scanline IRQ lands on the expected row, every frame. */
#include "../src/nes.h"

#include <stdio.h>

#define SPLIT 100

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s mmc3_irq_test.nes\n", argv[0]); return 2; }
    static Nes nes;
    if (nes_load(&nes, argv[1]) != 0) return 2;

    int failures = 0;
    for (int frame = 0; frame < 8; frame++) {
        nes_run_frame(&nes);
        if (frame < 3) continue; /* let the ROM finish initialising */
        for (int y = 0; y < PPU_H; y++) {
            for (int x = 8; x < 256; x += 61) {
                uint32_t want = y < SPLIT ? 0x982220 : 0x000000;
                uint32_t got = nes.ppu.fb[y * PPU_W + x] & 0xFFFFFF;
                if (got != want && failures++ < 5)
                    printf("frame %d: pixel (%d,%d) = %06X, want %06X\n", frame, x, y, got, want);
            }
        }
    }
    if (failures) { printf("%d mismatches\n", failures); return 1; }
    printf("mmc3 irq test passed (split at row %d)\n", SPLIT);
    nes_free(&nes);
    return 0;
}
