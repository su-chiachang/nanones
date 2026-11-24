#include "cpu.h"

typedef enum {
    JAM = 0, /* default for unlisted opcodes */
    ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL, BRK, BVC, BVS, CLC,
    CLD, CLI, CLV, CMP, CPX, CPY, DEC, DEX, DEY, EOR, INC, INX, INY, JMP,
    JSR, LDA, LDX, LDY, LSR, NOP, ORA, PHA, PHP, PLA, PLP, ROL, ROR, RTI,
    RTS, SBC, SEC, SED, SEI, STA, STX, STY, TAX, TAY, TSX, TXA, TXS, TYA,
    /* unofficial */
    DCP, ISB, LAX, RLA, RRA, SAX, SLO, SRE,
    ANC, ALR, ARR, AXS, XAA, LXA, SHY, SHX, AHX, TAS, LAS,
} Mnemonic;

typedef enum {
    IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL,
} Mode;

typedef struct {
    uint8_t op;      /* Mnemonic */
    uint8_t mode;    /* Mode */
    uint8_t cycles;  /* base cycles */
    uint8_t penalty; /* +1 cycle when indexing crosses a page */
} Op;

#define O(code, m, mode, cyc, pen) [code] = {m, mode, cyc, pen}

static const Op OPS[256] = {
    /* ADC */ O(0x69, ADC, IMM, 2, 0), O(0x65, ADC, ZP, 3, 0), O(0x75, ADC, ZPX, 4, 0),
              O(0x6D, ADC, ABS, 4, 0), O(0x7D, ADC, ABX, 4, 1), O(0x79, ADC, ABY, 4, 1),
              O(0x61, ADC, IZX, 6, 0), O(0x71, ADC, IZY, 5, 1),
    /* AND */ O(0x29, AND, IMM, 2, 0), O(0x25, AND, ZP, 3, 0), O(0x35, AND, ZPX, 4, 0),
              O(0x2D, AND, ABS, 4, 0), O(0x3D, AND, ABX, 4, 1), O(0x39, AND, ABY, 4, 1),
              O(0x21, AND, IZX, 6, 0), O(0x31, AND, IZY, 5, 1),
    /* ASL */ O(0x0A, ASL, ACC, 2, 0), O(0x06, ASL, ZP, 5, 0), O(0x16, ASL, ZPX, 6, 0),
              O(0x0E, ASL, ABS, 6, 0), O(0x1E, ASL, ABX, 7, 0),
    /* Bxx */ O(0x90, BCC, REL, 2, 0), O(0xB0, BCS, REL, 2, 0), O(0xF0, BEQ, REL, 2, 0),
              O(0x30, BMI, REL, 2, 0), O(0xD0, BNE, REL, 2, 0), O(0x10, BPL, REL, 2, 0),
              O(0x50, BVC, REL, 2, 0), O(0x70, BVS, REL, 2, 0),
    /* BIT */ O(0x24, BIT, ZP, 3, 0), O(0x2C, BIT, ABS, 4, 0),
    /* BRK */ O(0x00, BRK, IMP, 7, 0),
    /* CLx */ O(0x18, CLC, IMP, 2, 0), O(0xD8, CLD, IMP, 2, 0),
              O(0x58, CLI, IMP, 2, 0), O(0xB8, CLV, IMP, 2, 0),
    /* CMP */ O(0xC9, CMP, IMM, 2, 0), O(0xC5, CMP, ZP, 3, 0), O(0xD5, CMP, ZPX, 4, 0),
              O(0xCD, CMP, ABS, 4, 0), O(0xDD, CMP, ABX, 4, 1), O(0xD9, CMP, ABY, 4, 1),
              O(0xC1, CMP, IZX, 6, 0), O(0xD1, CMP, IZY, 5, 1),
    /* CPX */ O(0xE0, CPX, IMM, 2, 0), O(0xE4, CPX, ZP, 3, 0), O(0xEC, CPX, ABS, 4, 0),
    /* CPY */ O(0xC0, CPY, IMM, 2, 0), O(0xC4, CPY, ZP, 3, 0), O(0xCC, CPY, ABS, 4, 0),
    /* DEC */ O(0xC6, DEC, ZP, 5, 0), O(0xD6, DEC, ZPX, 6, 0),
              O(0xCE, DEC, ABS, 6, 0), O(0xDE, DEC, ABX, 7, 0),
    /* DEX/Y */ O(0xCA, DEX, IMP, 2, 0), O(0x88, DEY, IMP, 2, 0),
    /* EOR */ O(0x49, EOR, IMM, 2, 0), O(0x45, EOR, ZP, 3, 0), O(0x55, EOR, ZPX, 4, 0),
              O(0x4D, EOR, ABS, 4, 0), O(0x5D, EOR, ABX, 4, 1), O(0x59, EOR, ABY, 4, 1),
              O(0x41, EOR, IZX, 6, 0), O(0x51, EOR, IZY, 5, 1),
    /* INC */ O(0xE6, INC, ZP, 5, 0), O(0xF6, INC, ZPX, 6, 0),
              O(0xEE, INC, ABS, 6, 0), O(0xFE, INC, ABX, 7, 0),
    /* INX/Y */ O(0xE8, INX, IMP, 2, 0), O(0xC8, INY, IMP, 2, 0),
    /* JMP */ O(0x4C, JMP, ABS, 3, 0), O(0x6C, JMP, IND, 5, 0),
    /* JSR */ O(0x20, JSR, ABS, 6, 0),
    /* LDA */ O(0xA9, LDA, IMM, 2, 0), O(0xA5, LDA, ZP, 3, 0), O(0xB5, LDA, ZPX, 4, 0),
              O(0xAD, LDA, ABS, 4, 0), O(0xBD, LDA, ABX, 4, 1), O(0xB9, LDA, ABY, 4, 1),
              O(0xA1, LDA, IZX, 6, 0), O(0xB1, LDA, IZY, 5, 1),
    /* LDX */ O(0xA2, LDX, IMM, 2, 0), O(0xA6, LDX, ZP, 3, 0), O(0xB6, LDX, ZPY, 4, 0),
              O(0xAE, LDX, ABS, 4, 0), O(0xBE, LDX, ABY, 4, 1),
    /* LDY */ O(0xA0, LDY, IMM, 2, 0), O(0xA4, LDY, ZP, 3, 0), O(0xB4, LDY, ZPX, 4, 0),
              O(0xAC, LDY, ABS, 4, 0), O(0xBC, LDY, ABX, 4, 1),
    /* LSR */ O(0x4A, LSR, ACC, 2, 0), O(0x46, LSR, ZP, 5, 0), O(0x56, LSR, ZPX, 6, 0),
              O(0x4E, LSR, ABS, 6, 0), O(0x5E, LSR, ABX, 7, 0),
    /* NOP */ O(0xEA, NOP, IMP, 2, 0),
    /* ORA */ O(0x09, ORA, IMM, 2, 0), O(0x05, ORA, ZP, 3, 0), O(0x15, ORA, ZPX, 4, 0),
              O(0x0D, ORA, ABS, 4, 0), O(0x1D, ORA, ABX, 4, 1), O(0x19, ORA, ABY, 4, 1),
              O(0x01, ORA, IZX, 6, 0), O(0x11, ORA, IZY, 5, 1),
    /* stack */ O(0x48, PHA, IMP, 3, 0), O(0x08, PHP, IMP, 3, 0),
                O(0x68, PLA, IMP, 4, 0), O(0x28, PLP, IMP, 4, 0),
    /* ROL */ O(0x2A, ROL, ACC, 2, 0), O(0x26, ROL, ZP, 5, 0), O(0x36, ROL, ZPX, 6, 0),
              O(0x2E, ROL, ABS, 6, 0), O(0x3E, ROL, ABX, 7, 0),
    /* ROR */ O(0x6A, ROR, ACC, 2, 0), O(0x66, ROR, ZP, 5, 0), O(0x76, ROR, ZPX, 6, 0),
              O(0x6E, ROR, ABS, 6, 0), O(0x7E, ROR, ABX, 7, 0),
    /* RTI/RTS */ O(0x40, RTI, IMP, 6, 0), O(0x60, RTS, IMP, 6, 0),
    /* SBC */ O(0xE9, SBC, IMM, 2, 0), O(0xE5, SBC, ZP, 3, 0), O(0xF5, SBC, ZPX, 4, 0),
              O(0xED, SBC, ABS, 4, 0), O(0xFD, SBC, ABX, 4, 1), O(0xF9, SBC, ABY, 4, 1),
              O(0xE1, SBC, IZX, 6, 0), O(0xF1, SBC, IZY, 5, 1),
    /* SEx */ O(0x38, SEC, IMP, 2, 0), O(0xF8, SED, IMP, 2, 0), O(0x78, SEI, IMP, 2, 0),
    /* STA */ O(0x85, STA, ZP, 3, 0), O(0x95, STA, ZPX, 4, 0), O(0x8D, STA, ABS, 4, 0),
              O(0x9D, STA, ABX, 5, 0), O(0x99, STA, ABY, 5, 0),
              O(0x81, STA, IZX, 6, 0), O(0x91, STA, IZY, 6, 0),
    /* STX */ O(0x86, STX, ZP, 3, 0), O(0x96, STX, ZPY, 4, 0), O(0x8E, STX, ABS, 4, 0),
    /* STY */ O(0x84, STY, ZP, 3, 0), O(0x94, STY, ZPX, 4, 0), O(0x8C, STY, ABS, 4, 0),
    /* Txx */ O(0xAA, TAX, IMP, 2, 0), O(0xA8, TAY, IMP, 2, 0), O(0xBA, TSX, IMP, 2, 0),
              O(0x8A, TXA, IMP, 2, 0), O(0x9A, TXS, IMP, 2, 0), O(0x98, TYA, IMP, 2, 0),

    /* ---- unofficial ---- */
    /* NOP variants */
    O(0x1A, NOP, IMP, 2, 0), O(0x3A, NOP, IMP, 2, 0), O(0x5A, NOP, IMP, 2, 0),
    O(0x7A, NOP, IMP, 2, 0), O(0xDA, NOP, IMP, 2, 0), O(0xFA, NOP, IMP, 2, 0),
    O(0x80, NOP, IMM, 2, 0), O(0x82, NOP, IMM, 2, 0), O(0x89, NOP, IMM, 2, 0),
    O(0xC2, NOP, IMM, 2, 0), O(0xE2, NOP, IMM, 2, 0),
    O(0x04, NOP, ZP, 3, 0),  O(0x44, NOP, ZP, 3, 0),  O(0x64, NOP, ZP, 3, 0),
    O(0x14, NOP, ZPX, 4, 0), O(0x34, NOP, ZPX, 4, 0), O(0x54, NOP, ZPX, 4, 0),
    O(0x74, NOP, ZPX, 4, 0), O(0xD4, NOP, ZPX, 4, 0), O(0xF4, NOP, ZPX, 4, 0),
    O(0x0C, NOP, ABS, 4, 0),
    O(0x1C, NOP, ABX, 4, 1), O(0x3C, NOP, ABX, 4, 1), O(0x5C, NOP, ABX, 4, 1),
    O(0x7C, NOP, ABX, 4, 1), O(0xDC, NOP, ABX, 4, 1), O(0xFC, NOP, ABX, 4, 1),
    /* LAX */ O(0xA7, LAX, ZP, 3, 0), O(0xB7, LAX, ZPY, 4, 0), O(0xAF, LAX, ABS, 4, 0),
              O(0xBF, LAX, ABY, 4, 1), O(0xA3, LAX, IZX, 6, 0), O(0xB3, LAX, IZY, 5, 1),
    /* SAX */ O(0x87, SAX, ZP, 3, 0), O(0x97, SAX, ZPY, 4, 0),
              O(0x8F, SAX, ABS, 4, 0), O(0x83, SAX, IZX, 6, 0),
    /* SBC */ O(0xEB, SBC, IMM, 2, 0),
    /* DCP */ O(0xC7, DCP, ZP, 5, 0), O(0xD7, DCP, ZPX, 6, 0), O(0xCF, DCP, ABS, 6, 0),
              O(0xDF, DCP, ABX, 7, 0), O(0xDB, DCP, ABY, 7, 0),
              O(0xC3, DCP, IZX, 8, 0), O(0xD3, DCP, IZY, 8, 0),
    /* ISB */ O(0xE7, ISB, ZP, 5, 0), O(0xF7, ISB, ZPX, 6, 0), O(0xEF, ISB, ABS, 6, 0),
              O(0xFF, ISB, ABX, 7, 0), O(0xFB, ISB, ABY, 7, 0),
              O(0xE3, ISB, IZX, 8, 0), O(0xF3, ISB, IZY, 8, 0),
    /* SLO */ O(0x07, SLO, ZP, 5, 0), O(0x17, SLO, ZPX, 6, 0), O(0x0F, SLO, ABS, 6, 0),
              O(0x1F, SLO, ABX, 7, 0), O(0x1B, SLO, ABY, 7, 0),
              O(0x03, SLO, IZX, 8, 0), O(0x13, SLO, IZY, 8, 0),
    /* RLA */ O(0x27, RLA, ZP, 5, 0), O(0x37, RLA, ZPX, 6, 0), O(0x2F, RLA, ABS, 6, 0),
              O(0x3F, RLA, ABX, 7, 0), O(0x3B, RLA, ABY, 7, 0),
              O(0x23, RLA, IZX, 8, 0), O(0x33, RLA, IZY, 8, 0),
    /* SRE */ O(0x47, SRE, ZP, 5, 0), O(0x57, SRE, ZPX, 6, 0), O(0x4F, SRE, ABS, 6, 0),
              O(0x5F, SRE, ABX, 7, 0), O(0x5B, SRE, ABY, 7, 0),
              O(0x43, SRE, IZX, 8, 0), O(0x53, SRE, IZY, 8, 0),
    /* odd ones */
    O(0x0B, ANC, IMM, 2, 0), O(0x2B, ANC, IMM, 2, 0), O(0x4B, ALR, IMM, 2, 0),
    O(0x6B, ARR, IMM, 2, 0), O(0xCB, AXS, IMM, 2, 0), O(0x8B, XAA, IMM, 2, 0),
    O(0xAB, LXA, IMM, 2, 0), O(0x9C, SHY, ABX, 5, 0), O(0x9E, SHX, ABY, 5, 0),
    O(0x9F, AHX, ABY, 5, 0), O(0x93, AHX, IZY, 6, 0), O(0x9B, TAS, ABY, 5, 0),
    O(0xBB, LAS, ABY, 4, 1),
    /* RRA */ O(0x67, RRA, ZP, 5, 0), O(0x77, RRA, ZPX, 6, 0), O(0x6F, RRA, ABS, 6, 0),
              O(0x7F, RRA, ABX, 7, 0), O(0x7B, RRA, ABY, 7, 0),
              O(0x63, RRA, IZX, 8, 0), O(0x73, RRA, IZY, 8, 0),
};

#undef O

/* ---- bus helpers ---- */

static inline uint8_t rd(Cpu *c, uint16_t a) { return c->read(c->ctx, a); }
static inline void wr(Cpu *c, uint16_t a, uint8_t v) { c->write(c->ctx, a, v); }

static inline uint16_t rd16(Cpu *c, uint16_t a) {
    return (uint16_t)(rd(c, a) | (rd(c, (uint16_t)(a + 1)) << 8));
}

static inline void push(Cpu *c, uint8_t v) { wr(c, (uint16_t)(0x100 | c->sp--), v); }
static inline uint8_t pull(Cpu *c) { return rd(c, (uint16_t)(0x100 | ++c->sp)); }

/* ---- flags ---- */

static inline void set_flag(Cpu *c, uint8_t f, bool on) {
    if (on) c->p |= f; else c->p &= (uint8_t)~f;
}

static inline void set_zn(Cpu *c, uint8_t v) {
    set_flag(c, CPU_Z, v == 0);
    set_flag(c, CPU_N, v & 0x80);
}

/* ---- ALU ---- */

static void adc(Cpu *c, uint8_t m) {
    unsigned sum = c->a + m + (c->p & CPU_C);
    set_flag(c, CPU_C, sum > 0xFF);
    set_flag(c, CPU_V, (~(c->a ^ m) & (c->a ^ sum)) & 0x80);
    c->a = (uint8_t)sum;
    set_zn(c, c->a);
}

static void cmp(Cpu *c, uint8_t reg, uint8_t m) {
    set_flag(c, CPU_C, reg >= m);
    set_zn(c, (uint8_t)(reg - m));
}

static uint8_t asl(Cpu *c, uint8_t v) {
    set_flag(c, CPU_C, v & 0x80);
    v = (uint8_t)(v << 1);
    set_zn(c, v);
    return v;
}

static uint8_t lsr(Cpu *c, uint8_t v) {
    set_flag(c, CPU_C, v & 0x01);
    v >>= 1;
    set_zn(c, v);
    return v;
}

static uint8_t rol(Cpu *c, uint8_t v) {
    uint8_t carry = c->p & CPU_C;
    set_flag(c, CPU_C, v & 0x80);
    v = (uint8_t)((v << 1) | carry);
    set_zn(c, v);
    return v;
}

static uint8_t ror(Cpu *c, uint8_t v) {
    uint8_t carry = (uint8_t)((c->p & CPU_C) << 7);
    set_flag(c, CPU_C, v & 0x01);
    v = (uint8_t)((v >> 1) | carry);
    set_zn(c, v);
    return v;
}

/* ---- interrupts ---- */

static int interrupt(Cpu *c, uint16_t vector, bool brk) {
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((c->p | CPU_U) | (brk ? CPU_B : 0)));
    c->p |= CPU_I;
    c->i_poll = true;
    c->just_vectored = true;
    /* An NMI that arrives before the vector fetch (about cycle 4 of 7) hijacks BRK/IRQ. */
    if (vector == 0xFFFE && c->nmi_poll && c->nmi_poll(c->ctx, c->cycles + 4)) vector = 0xFFFA;
    c->pc = rd16(c, vector);
    return 7;
}

/* ---- public API ---- */

void cpu_init(Cpu *c, CpuRead read, CpuWrite write, void *ctx) {
    *c = (Cpu){0};
    c->read = read;
    c->write = write;
    c->ctx = ctx;
}

void cpu_reset(Cpu *c) {
    c->a = c->x = c->y = 0;
    c->sp = 0xFD;
    c->p = CPU_I | CPU_U;
    c->i_poll = true;
    c->pc = rd16(c, 0xFFFC);
    c->cycles = 7;
    c->jammed = false;
    c->nmi_pending = false;
}

void cpu_nmi(Cpu *c) { c->nmi_pending = true; }

/* Resolves the effective address of a non-relative mode.
 * Sets *crossed when an indexed access crosses a page boundary. */
static uint16_t resolve(Cpu *c, Mode mode, bool *crossed) {
    uint16_t base, addr;
    *crossed = false;
    switch (mode) {
    case IMM: return c->pc++;
    case ZP:  return rd(c, c->pc++);
    case ZPX: return (uint8_t)(rd(c, c->pc++) + c->x);
    case ZPY: return (uint8_t)(rd(c, c->pc++) + c->y);
    case ABS: addr = rd16(c, c->pc); c->pc += 2; return addr;
    case ABX:
        base = rd16(c, c->pc); c->pc += 2;
        addr = (uint16_t)(base + c->x);
        *crossed = (base & 0xFF00) != (addr & 0xFF00);
        return addr;
    case ABY:
        base = rd16(c, c->pc); c->pc += 2;
        addr = (uint16_t)(base + c->y);
        *crossed = (base & 0xFF00) != (addr & 0xFF00);
        return addr;
    case IND: {
        uint16_t ptr = rd16(c, c->pc); c->pc += 2;
        /* 6502 bug: the high byte is fetched without carrying into the page */
        uint8_t lo = rd(c, ptr);
        uint8_t hi = rd(c, (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF)));
        return (uint16_t)(lo | (hi << 8));
    }
    case IZX: {
        uint8_t z = (uint8_t)(rd(c, c->pc++) + c->x);
        return (uint16_t)(rd(c, z) | (rd(c, (uint8_t)(z + 1)) << 8));
    }
    case IZY: {
        uint8_t z = rd(c, c->pc++);
        base = (uint16_t)(rd(c, z) | (rd(c, (uint8_t)(z + 1)) << 8));
        addr = (uint16_t)(base + c->y);
        *crossed = (base & 0xFF00) != (addr & 0xFF00);
        return addr;
    }
    default: return 0; /* IMP, ACC, REL do not use this */
    }
}

static int branch(Cpu *c, bool taken) {
    int8_t off = (int8_t)rd(c, c->pc++);
    int cycles = 2;
    if (taken) {
        uint16_t target = (uint16_t)(c->pc + off);
        bool crossed = (target & 0xFF00) != (c->pc & 0xFF00);
        cycles += 1 + crossed;
        c->poll_early = !crossed;
        c->pc = target;
    }
    return cycles;
}

int cpu_step(Cpu *c) {
    int cycles;

    bool allow_interrupt = !c->just_vectored;
    c->just_vectored = false;
    if (allow_interrupt && c->nmi_pending) {
        c->nmi_pending = false;
        cycles = interrupt(c, 0xFFFA, false);
        c->cycles += (uint64_t)cycles;
        return cycles;
    }
    if (allow_interrupt && c->irq_line && !c->i_poll) {
        cycles = interrupt(c, 0xFFFE, false);
        c->cycles += (uint64_t)cycles;
        return cycles;
    }

    c->poll_early = false;
    bool i_before = c->p & CPU_I;
    uint8_t opcode = rd(c, c->pc++);
    const Op *o = &OPS[opcode];
    cycles = o->cycles;

    bool crossed = false;
    uint16_t addr = 0;
    uint8_t m = 0;

    if (o->mode != IMP && o->mode != ACC && o->mode != REL)
        addr = resolve(c, (Mode)o->mode, &crossed);
    if (crossed) cycles += o->penalty;
    /* Operand reads and writes happen on the last cycle of the instruction. */
    c->access_cycle = c->cycles + (uint64_t)cycles - 1;

    switch ((Mnemonic)o->op) {
    case JAM:
        c->jammed = true;
        c->pc--; /* stay on the offending opcode */
        return 0;

    /* ---- loads / stores ---- */
    case LDA: c->a = rd(c, addr); set_zn(c, c->a); break;
    case LDX: c->x = rd(c, addr); set_zn(c, c->x); break;
    case LDY: c->y = rd(c, addr); set_zn(c, c->y); break;
    case LAX: c->a = c->x = rd(c, addr); set_zn(c, c->a); break;
    case STA: wr(c, addr, c->a); break;
    case STX: wr(c, addr, c->x); break;
    case STY: wr(c, addr, c->y); break;
    case SAX: wr(c, addr, (uint8_t)(c->a & c->x)); break;

    /* ---- transfers ---- */
    case TAX: c->x = c->a; set_zn(c, c->x); break;
    case TAY: c->y = c->a; set_zn(c, c->y); break;
    case TXA: c->a = c->x; set_zn(c, c->a); break;
    case TYA: c->a = c->y; set_zn(c, c->a); break;
    case TSX: c->x = c->sp; set_zn(c, c->x); break;
    case TXS: c->sp = c->x; break;

    /* ---- stack ---- */
    case PHA: push(c, c->a); break;
    case PHP: push(c, (uint8_t)(c->p | CPU_B | CPU_U)); break;
    case PLA: c->a = pull(c); set_zn(c, c->a); break;
    case PLP: c->p = (uint8_t)((pull(c) & ~CPU_B) | CPU_U); break;

    /* ---- logic / arithmetic ---- */
    case AND: c->a &= rd(c, addr); set_zn(c, c->a); break;
    case ORA: c->a |= rd(c, addr); set_zn(c, c->a); break;
    case EOR: c->a ^= rd(c, addr); set_zn(c, c->a); break;
    case ADC: adc(c, rd(c, addr)); break;
    case SBC: adc(c, (uint8_t)~rd(c, addr)); break;
    case CMP: cmp(c, c->a, rd(c, addr)); break;
    case CPX: cmp(c, c->x, rd(c, addr)); break;
    case CPY: cmp(c, c->y, rd(c, addr)); break;
    case BIT:
        m = rd(c, addr);
        set_flag(c, CPU_Z, (c->a & m) == 0);
        set_flag(c, CPU_V, m & 0x40);
        set_flag(c, CPU_N, m & 0x80);
        break;

    /* ---- shifts / rotates ---- */
    case ASL:
        if (o->mode == ACC) c->a = asl(c, c->a);
        else wr(c, addr, asl(c, rd(c, addr)));
        break;
    case LSR:
        if (o->mode == ACC) c->a = lsr(c, c->a);
        else wr(c, addr, lsr(c, rd(c, addr)));
        break;
    case ROL:
        if (o->mode == ACC) c->a = rol(c, c->a);
        else wr(c, addr, rol(c, rd(c, addr)));
        break;
    case ROR:
        if (o->mode == ACC) c->a = ror(c, c->a);
        else wr(c, addr, ror(c, rd(c, addr)));
        break;

    /* ---- inc / dec ---- */
    case INC: m = (uint8_t)(rd(c, addr) + 1); wr(c, addr, m); set_zn(c, m); break;
    case DEC: m = (uint8_t)(rd(c, addr) - 1); wr(c, addr, m); set_zn(c, m); break;
    case INX: c->x++; set_zn(c, c->x); break;
    case INY: c->y++; set_zn(c, c->y); break;
    case DEX: c->x--; set_zn(c, c->x); break;
    case DEY: c->y--; set_zn(c, c->y); break;

    /* ---- flags ---- */
    case CLC: c->p &= (uint8_t)~CPU_C; break;
    case CLD: c->p &= (uint8_t)~CPU_D; break;
    case CLI: c->p &= (uint8_t)~CPU_I; break;
    case CLV: c->p &= (uint8_t)~CPU_V; break;
    case SEC: c->p |= CPU_C; break;
    case SED: c->p |= CPU_D; break;
    case SEI: c->p |= CPU_I; break;

    /* ---- control flow ---- */
    case JMP: c->pc = addr; break;
    case JSR:
        push(c, (uint8_t)((c->pc - 1) >> 8));
        push(c, (uint8_t)(c->pc - 1));
        c->pc = addr;
        break;
    case RTS: {
        uint8_t lo = pull(c);
        uint8_t hi = pull(c);
        c->pc = (uint16_t)((lo | (hi << 8)) + 1);
        break;
    }
    case RTI: {
        c->p = (uint8_t)((pull(c) & ~CPU_B) | CPU_U);
        uint8_t lo = pull(c);
        uint8_t hi = pull(c);
        c->pc = (uint16_t)(lo | (hi << 8));
        break;
    }
    case BRK:
        c->pc++; /* BRK has a padding byte; the pushed PC skips it */
        interrupt(c, 0xFFFE, true);
        break;
    case BCC: cycles = branch(c, !(c->p & CPU_C)); break;
    case BCS: cycles = branch(c,  (c->p & CPU_C)); break;
    case BNE: cycles = branch(c, !(c->p & CPU_Z)); break;
    case BEQ: cycles = branch(c,  (c->p & CPU_Z)); break;
    case BPL: cycles = branch(c, !(c->p & CPU_N)); break;
    case BMI: cycles = branch(c,  (c->p & CPU_N)); break;
    case BVC: cycles = branch(c, !(c->p & CPU_V)); break;
    case BVS: cycles = branch(c,  (c->p & CPU_V)); break;

    case NOP:
        /* Unofficial NOPs still perform the read (matters for I/O registers). */
        if (o->mode != IMP) (void)rd(c, addr);
        break;

    /* ---- unofficial read-modify-write combos ---- */
    case DCP:
        m = (uint8_t)(rd(c, addr) - 1);
        wr(c, addr, m);
        cmp(c, c->a, m);
        break;
    case ISB:
        m = (uint8_t)(rd(c, addr) + 1);
        wr(c, addr, m);
        adc(c, (uint8_t)~m);
        break;
    case SLO:
        m = asl(c, rd(c, addr));
        wr(c, addr, m);
        c->a |= m; set_zn(c, c->a);
        break;
    case RLA:
        m = rol(c, rd(c, addr));
        wr(c, addr, m);
        c->a &= m; set_zn(c, c->a);
        break;
    case SRE:
        m = lsr(c, rd(c, addr));
        wr(c, addr, m);
        c->a ^= m; set_zn(c, c->a);
        break;
    case RRA:
        m = ror(c, rd(c, addr));
        wr(c, addr, m);
        adc(c, m);
        break;

    /* ---- the rest of the unofficial set ---- */
    case ANC: c->a &= rd(c, addr); set_zn(c, c->a); set_flag(c, CPU_C, c->a & 0x80); break;
    case ALR: c->a &= rd(c, addr); c->a = lsr(c, c->a); break;
    case ARR:
        c->a &= rd(c, addr);
        c->a = (uint8_t)((c->a >> 1) | ((c->p & CPU_C) << 7));
        set_zn(c, c->a);
        set_flag(c, CPU_C, c->a & 0x40);
        set_flag(c, CPU_V, ((c->a >> 6) ^ (c->a >> 5)) & 1);
        break;
    case AXS:
        m = rd(c, addr);
        set_flag(c, CPU_C, (c->a & c->x) >= m);
        c->x = (uint8_t)((c->a & c->x) - m);
        set_zn(c, c->x);
        break;
    case XAA: c->a = c->x & rd(c, addr); set_zn(c, c->a); break;
    case LXA: c->a = c->x = rd(c, addr); set_zn(c, c->a); break;
    case LAS:
        c->a = c->x = c->sp = (uint8_t)(rd(c, addr) & c->sp);
        set_zn(c, c->a);
        break;
    case SHY: case SHX: case AHX: case TAS: {
        /* Store (reg & (high byte of the base address + 1)); on a page cross the
         * high byte of the target is replaced by that value. */
        uint8_t index = (o->mode == ABX) ? c->x : c->y;
        uint16_t base = (uint16_t)(addr - index);
        uint8_t val;
        switch ((Mnemonic)o->op) {
        case SHY: val = c->y; break;
        case SHX: val = c->x; break;
        case AHX: val = (uint8_t)(c->a & c->x); break;
        default:  c->sp = (uint8_t)(c->a & c->x); val = c->sp; break; /* TAS */
        }
        uint8_t hi = (uint8_t)(base >> 8);
        val &= (uint8_t)(hi + 1);
        if ((base & 0xFF00) != (addr & 0xFF00)) addr = (uint16_t)((val << 8) | (addr & 0xFF));
        wr(c, addr, val);
        break;
    }
    }

    c->cycles += (uint64_t)cycles;
    /* The poll happens before the last cycle, so CLI/SEI/PLP only affect it one instruction later. */
    c->i_poll = (o->op == CLI || o->op == SEI || o->op == PLP) ? i_before : (c->p & CPU_I);
    return cycles;
}
