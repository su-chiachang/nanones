/* Runs a blargg-style test ROM headless and reports its result.
 * Protocol: $6001-$6003 = DE B0 61 once valid; $6000 = 0x80 while running,
 * 0 on pass, other values = failure code; $6004.. = NUL-terminated text.
 *
 * usage: blargg_runner rom.nes [max_frames]
 * exit status: 0 pass, 1 fail, 2 error/timeout
 */
#include "../src/nes.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s rom.nes [max_frames]\n", argv[0]); return 2; }
    int max_frames = argc > 2 ? atoi(argv[2]) : 60 * 40;
    static Nes nes;
    if (nes_load(&nes, argv[1]) != 0) return 2;

    int status = -1, seen_running = 0;
    for (int f = 0; f < max_frames; f++) {
        if (!nes_run_frame(&nes)) { printf("CPU jammed at $%04X\n", nes.cpu.pc); return 2; }
        const uint8_t *m = nes.cart.prg_ram;
        if (m[1] == 0xDE && m[2] == 0xB0 && m[3] == 0x61) {
            if (m[0] == 0x80) seen_running = 1;
            else if (seen_running || m[0] != 0x80) { status = m[0]; if (seen_running) break; }
        }
    }
    const uint8_t *m = nes.cart.prg_ram;
    if (status < 0) { printf("TIMEOUT (no result)\n"); return 2; }
    printf("status %02X\n%s\n", status, (const char *)&m[4]);
    nes_free(&nes);
    return status == 0 ? 0 : 1;
}
