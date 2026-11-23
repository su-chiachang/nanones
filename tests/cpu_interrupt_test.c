/* Direct tests for NMI, BRK and RTI. nestest never triggers these, and
 * SMB1/SMB3 depend on NMI every frame. */
#include "../src/cpu.h"

#include <stdio.h>
#include <string.h>

static uint8_t mem[0x10000];
static int failures;

static uint8_t rd(void *ctx, uint16_t a) { (void)ctx; return mem[a]; }
static void wr(void *ctx, uint16_t a, uint8_t v) { (void)ctx; mem[a] = v; }

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
    } while (0)

static void setup(Cpu *c) {
    memset(mem, 0, sizeof mem);
    mem[0xFFFA] = 0x00; mem[0xFFFB] = 0x90; /* NMI vector -> $9000 */
    mem[0xFFFE] = 0x00; mem[0xFFFF] = 0xA0; /* IRQ/BRK vector -> $A000 */
    mem[0x9000] = 0x40;                     /* RTI */
    mem[0xA000] = 0x40;                     /* RTI */
    cpu_init(c, rd, wr, NULL);
    c->sp = 0xFD;
    c->p = CPU_U | CPU_C | CPU_N; /* I clear, a few flags set */
    c->pc = 0x8123;
    c->cycles = 100;
}

static void test_nmi(void) {
    Cpu c; setup(&c);
    uint8_t p_before = c.p;
    cpu_nmi(&c);
    int cyc = cpu_step(&c);
    CHECK(cyc == 7, "NMI takes 7 cycles");
    CHECK(c.cycles == 107, "NMI advances cycle counter");
    CHECK(c.pc == 0x9000, "NMI jumps to vector");
    CHECK(c.sp == 0xFA, "NMI pushes 3 bytes");
    CHECK(mem[0x1FD] == 0x81 && mem[0x1FC] == 0x23, "NMI pushes return PC");
    CHECK(mem[0x1FB] == (uint8_t)((p_before | CPU_U) & ~CPU_B), "NMI pushes P with B=0, U=1");
    CHECK(c.p & CPU_I, "NMI sets I");
    CHECK(!c.nmi_pending, "NMI is consumed");

    cpu_step(&c); /* RTI */
    CHECK(c.pc == 0x8123 && c.sp == 0xFD && c.p == p_before, "RTI restores PC, SP, P");
}

static void test_brk(void) {
    Cpu c; setup(&c);
    mem[0x8123] = 0x00; /* BRK */
    uint8_t p_before = c.p;
    int cyc = cpu_step(&c);
    CHECK(cyc == 7, "BRK takes 7 cycles");
    CHECK(c.pc == 0xA000, "BRK jumps to IRQ vector");
    CHECK(mem[0x1FD] == 0x81 && mem[0x1FC] == 0x25, "BRK pushes opcode address + 2");
    CHECK(mem[0x1FB] == (uint8_t)(p_before | CPU_U | CPU_B), "BRK pushes P with B=1");
    CHECK(c.p & CPU_I, "BRK sets I");
    CHECK(!(c.p & CPU_B), "B never lives in the register");

    cpu_step(&c); /* RTI */
    CHECK(c.pc == 0x8125 && c.sp == 0xFD && c.p == p_before, "RTI after BRK returns past padding byte");
}

static void test_irq_masked(void) {
    Cpu c; setup(&c);
    mem[0x8123] = 0xEA; /* NOP */
    c.irq_line = true;
    c.p |= CPU_I;
    cpu_step(&c);
    CHECK(c.pc == 0x8124, "IRQ ignored while I is set");
    c.p &= (uint8_t)~CPU_I;
    cpu_step(&c);
    CHECK(c.pc == 0xA000 && !(mem[0x1FB] & CPU_B), "IRQ taken when I clear, pushes B=0");
}

int main(void) {
    test_nmi();
    test_brk();
    test_irq_masked();
    if (failures) { printf("%d check(s) failed\n", failures); return 1; }
    printf("interrupt tests passed\n");
    return 0;
}
