#ifndef NES_CPU_H
#define NES_CPU_H

#include <stdbool.h>
#include <stdint.h>

/* 2A03 (6502 without decimal mode). Bus access goes through callbacks so the
 * CPU knows nothing about RAM, PPU or cartridges. */

enum {
    CPU_C = 0x01,
    CPU_Z = 0x02,
    CPU_I = 0x04,
    CPU_D = 0x08, /* stored but has no effect on the NES */
    CPU_B = 0x10, /* only exists on the stack, never in the register */
    CPU_U = 0x20, /* always reads as 1 */
    CPU_V = 0x40,
    CPU_N = 0x80,
};

typedef uint8_t (*CpuRead)(void *ctx, uint16_t addr);
typedef void (*CpuWrite)(void *ctx, uint16_t addr, uint8_t value);

typedef struct Cpu {
    uint8_t a, x, y, sp, p;
    uint16_t pc;
    uint64_t cycles;   /* total CPU cycles since reset */
    bool jammed;       /* hit a KIL / unsupported opcode */
    bool nmi_pending;  /* set by cpu_nmi(), consumed by the next cpu_step() */
    uint64_t access_cycle; /* cycle number of the operand access of the instruction being executed */
    bool irq_line;     /* level-triggered; serviced while I flag is clear */

    CpuRead read;
    CpuWrite write;
    void *ctx;
} Cpu;

void cpu_init(Cpu *c, CpuRead read, CpuWrite write, void *ctx);

/* Power-on / reset: SP=$FD, P=$24, PC from $FFFC, cycles=7. */
void cpu_reset(Cpu *c);

/* Executes one instruction (or services a pending interrupt).
 * Returns the number of CPU cycles it took. */
int cpu_step(Cpu *c);

void cpu_nmi(Cpu *c);

#endif
