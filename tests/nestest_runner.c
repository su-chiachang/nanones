/* Headless CPU check: runs nestest.nes in "automation" mode (PC=$C000) and
 * compares every instruction against the reference nestest.log.
 *
 * Compared per line: PC, opcode byte, A, X, Y, P, SP and CPU cycle count.
 * The PPU column and the disassembly text are ignored (no PPU yet).
 *
 * usage: nestest_runner <nestest.nes> <nestest.log>
 */
#include "../src/cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t ram[0x800];
    uint8_t *prg;
    size_t prg_size;
} Bus;

static uint8_t bus_read(void *ctx, uint16_t addr) {
    Bus *b = ctx;
    if (addr < 0x2000) return b->ram[addr & 0x7FF];
    if (addr >= 0x8000) return b->prg[(addr - 0x8000) & (b->prg_size - 1)];
    return 0;
}

static void bus_write(void *ctx, uint16_t addr, uint8_t v) {
    Bus *b = ctx;
    if (addr < 0x2000) b->ram[addr & 0x7FF] = v;
    /* everything else (PPU/APU/mapper) is ignored for now */
}

static int load_ines(Bus *b, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    uint8_t h[16];
    if (fread(h, 1, 16, f) != 16 || memcmp(h, "NES\x1A", 4) != 0) {
        fprintf(stderr, "%s: not an iNES file\n", path);
        fclose(f);
        return -1;
    }
    if (h[6] & 0x04) fseek(f, 512, SEEK_CUR); /* skip trainer */
    b->prg_size = (size_t)h[4] * 0x4000;
    if (b->prg_size == 0 || (b->prg_size & (b->prg_size - 1))) {
        fprintf(stderr, "unsupported PRG size %zu\n", b->prg_size);
        fclose(f);
        return -1;
    }
    b->prg = malloc(b->prg_size);
    if (!b->prg || fread(b->prg, 1, b->prg_size, f) != b->prg_size) {
        fprintf(stderr, "short read on PRG data\n");
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* Parse one reference line. Returns 0 on success. */
static int parse_line(const char *s, unsigned *pc, unsigned *op, unsigned *a,
                      unsigned *x, unsigned *y, unsigned *p, unsigned *sp,
                      unsigned long long *cyc) {
    const char *r = strstr(s, "A:");
    const char *c = strstr(s, "CYC:");
    if (!r || !c) return -1;
    if (sscanf(s, "%4x  %2x", pc, op) != 2) return -1;
    if (sscanf(r, "A:%2x X:%2x Y:%2x P:%2x SP:%2x", a, x, y, p, sp) != 5) return -1;
    if (sscanf(c, "CYC:%llu", cyc) != 1) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <nestest.nes> <nestest.log>\n", argv[0]);
        return 2;
    }

    static Bus bus;
    if (load_ines(&bus, argv[1]) != 0) return 2;

    FILE *log = fopen(argv[2], "r");
    if (!log) { perror(argv[2]); return 2; }

    Cpu cpu;
    cpu_init(&cpu, bus_read, bus_write, &bus);
    cpu_reset(&cpu);
    cpu.pc = 0xC000; /* automation entry point, skips the interactive menu */

    char line[256];
    unsigned long lineno = 0;
    while (fgets(line, sizeof line, log)) {
        lineno++;
        unsigned pc, op, a, x, y, p, sp;
        unsigned long long cyc;
        if (parse_line(line, &pc, &op, &a, &x, &y, &p, &sp, &cyc) != 0) {
            fprintf(stderr, "line %lu: cannot parse reference log\n", lineno);
            return 2;
        }

        uint8_t our_op = bus_read(&bus, cpu.pc);
        if (cpu.pc != pc || our_op != op || cpu.a != a || cpu.x != x ||
            cpu.y != y || cpu.p != p || cpu.sp != sp || cpu.cycles != cyc) {
            line[strcspn(line, "\r\n")] = 0;
            printf("MISMATCH at log line %lu\n", lineno);
            printf("  expected: %s\n", line);
            printf("  actual  : %04X  %02X  A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu\n",
                   cpu.pc, our_op, cpu.a, cpu.x, cpu.y, cpu.p, cpu.sp,
                   (unsigned long long)cpu.cycles);
            return 1;
        }

        cpu_step(&cpu);
        if (cpu.jammed) {
            printf("CPU jammed at $%04X (opcode %02X) after log line %lu\n",
                   cpu.pc, bus_read(&bus, cpu.pc), lineno);
            return 1;
        }
    }
    fclose(log);

    /* nestest stores its result codes at $0002 (official) and $0003 (unofficial). */
    printf("%lu instructions matched the reference log.\n", lineno);
    printf("nestest result: $02=%02X $03=%02X (00 00 = all passed)\n", bus.ram[2], bus.ram[3]);
    return (bus.ram[2] == 0 && bus.ram[3] == 0) ? 0 : 1;
}
