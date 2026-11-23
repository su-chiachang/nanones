/* Headless system check: runs nestest.nes in "automation" mode (PC=$C000)
 * through the full Nes system and compares every instruction against the
 * reference nestest.log.
 *
 * Compared per line: PC, opcode byte, A, X, Y, P, SP, CPU cycle count and the
 * PPU scanline/dot (so CPU/PPU clock sync is checked too).
 * The disassembly text is ignored.
 *
 * usage: nestest_runner <nestest.nes> <nestest.log>
 */
#include "../src/nes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_line(const char *s, unsigned *pc, unsigned *op, unsigned *a,
                      unsigned *x, unsigned *y, unsigned *p, unsigned *sp,
                      int *scan, int *dot, unsigned long long *cyc) {
    const char *r = strstr(s, "A:");
    const char *q = strstr(s, "PPU:");
    const char *c = strstr(s, "CYC:");
    if (!r || !q || !c) return -1;
    if (sscanf(s, "%4x  %2x", pc, op) != 2) return -1;
    if (sscanf(r, "A:%2x X:%2x Y:%2x P:%2x SP:%2x", a, x, y, p, sp) != 5) return -1;
    if (sscanf(q, "PPU:%d,%d", scan, dot) != 2) return -1;
    if (sscanf(c, "CYC:%llu", cyc) != 1) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <nestest.nes> <nestest.log>\n", argv[0]);
        return 2;
    }

    static Nes nes;
    if (nes_load(&nes, argv[1]) != 0) return 2;

    FILE *log = fopen(argv[2], "r");
    if (!log) { perror(argv[2]); return 2; }

    nes.cpu.pc = 0xC000; /* automation entry point, skips the interactive menu */

    char line[256];
    unsigned long lineno = 0;
    while (fgets(line, sizeof line, log)) {
        lineno++;
        unsigned pc, op, a, x, y, p, sp;
        int scan, dot;
        unsigned long long cyc;
        if (parse_line(line, &pc, &op, &a, &x, &y, &p, &sp, &scan, &dot, &cyc) != 0) {
            fprintf(stderr, "line %lu: cannot parse reference log\n", lineno);
            return 2;
        }

        Cpu *c = &nes.cpu;
        uint8_t our_op = c->read(c->ctx, c->pc);
        if (c->pc != pc || our_op != op || c->a != a || c->x != x || c->y != y ||
            c->p != p || c->sp != sp || c->cycles != cyc ||
            nes.ppu.scanline != scan || nes.ppu.dot != dot) {
            line[strcspn(line, "\r\n")] = 0;
            printf("MISMATCH at log line %lu\n", lineno);
            printf("  expected: %s\n", line);
            printf("  actual  : %04X  %02X  A:%02X X:%02X Y:%02X P:%02X SP:%02X PPU:%3d,%3d CYC:%llu\n",
                   c->pc, our_op, c->a, c->x, c->y, c->p, c->sp,
                   nes.ppu.scanline, nes.ppu.dot, (unsigned long long)c->cycles);
            return 1;
        }

        nes_step(&nes);
        if (c->jammed) {
            printf("CPU jammed at $%04X after log line %lu\n", c->pc, lineno);
            return 1;
        }
    }
    fclose(log);

    /* nestest stores its result codes at $0002 (official) and $0003 (unofficial). */
    printf("%lu instructions matched the reference log (CPU + PPU clock).\n", lineno);
    printf("nestest result: $02=%02X $03=%02X (00 00 = all passed)\n", nes.ram[2], nes.ram[3]);
    int rc = (nes.ram[2] == 0 && nes.ram[3] == 0) ? 0 : 1;
    nes_free(&nes);
    return rc;
}
