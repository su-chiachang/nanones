/* Save states: saving, running on, then loading and re-running must give
 * the same frames bit for bit; states for another ROM must be refused.
 * usage: state_test rom.nes other.nes workdir */
#include "../src/nes.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s rom.nes other.nes workdir\n", argv[0]); return 2; }
    char path[512];
    snprintf(path, sizeof path, "%s/state_test.st0", argv[3]);

    static Nes nes, other;
    if (nes_load(&nes, argv[1]) != 0 || nes_load(&other, argv[2]) != 0) return 2;
    for (int i = 0; i < 30; i++) nes_run_frame(&nes);
    CHECK(nes_save_state(&nes, path) == 0, "save succeeds");

    /* Reference: 20 more frames from the saved point. */
    static uint32_t want[PPU_W * PPU_H];
    for (int i = 0; i < 20; i++) nes_run_frame(&nes);
    memcpy(want, nes.ppu.fb, sizeof want);
    uint64_t want_cycles = nes.cpu.cycles;
    uint16_t want_pc = nes.cpu.pc;

    /* Wander off, come back, replay. */
    for (int i = 0; i < 50; i++) nes_run_frame(&nes);
    CHECK(nes_load_state(&nes, path) == 0, "load succeeds");
    for (int i = 0; i < 20; i++) nes_run_frame(&nes);
    CHECK(memcmp(want, nes.ppu.fb, sizeof want) == 0, "frame after load matches");
    CHECK(nes.cpu.cycles == want_cycles, "CPU cycle count matches");
    CHECK(nes.cpu.pc == want_pc, "PC matches");

    CHECK(nes_load_state(&other, path) != 0, "state for another ROM is refused");

    nes_free(&nes);
    nes_free(&other);
    remove(path);
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("state tests passed\n");
    return 0;
}
