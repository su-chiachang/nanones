#!/usr/bin/env python3
"""Builds tests/mmc3_irq_test.nes (mapper 4). The whole screen is a solid red
tile; an MMC3 IRQ fires at scanline SPLIT and its handler turns rendering off,
so rows >= SPLIT show the black backdrop. NMI re-arms everything each frame.
Also uses PRG/CHR banking: the red tile only exists in CHR bank 5."""
import sys

SPLIT = 100
def asm(base):
    code = bytearray(); fix = []
    def emit(*b): code.extend(b)
    def lda(v): emit(0xA9, v)
    def sta(a): emit(0x8D, a & 0xFF, a >> 8)
    def poke(a, v): lda(v); sta(a)
    return code, emit, lda, sta, poke

# ---- reset code at $E000 ----
code, emit, lda, sta, poke = asm(0xE000)
emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)
poke(0x4017, 0x40)                       # inhibit the APU frame IRQ (it is enabled at power-on)
w = len(code); emit(0x2C, 0x02, 0x20); emit(0x10, (w - (len(code) + 2)) & 0xFF)
# CHR banking: R2 (1K at $0000 .. wait: bank_select 0 -> R0/R1 at $0000/$0800 (2K each).
poke(0x8000, 0); poke(0x8001, 10)        # R0 = 10 -> 2K banks 10,11 at $0000-$07FF (tile 1 lives in bank 11 -> slot offset)
poke(0x2006, 0x3F); poke(0x2006, 0x00)
for v in (0x0F, 0x16, 0x2A, 0x30) * 4: poke(0x2007, v)   # all four bg palettes alike
poke(0x2006, 0x20); poke(0x2006, 0x00)
lda(1)
emit(0xA0, 4)                            # LDY #4
outer = len(code)
emit(0xA2, 0)                            # LDX #0
inner = len(code)
sta(0x2007); emit(0xE8); emit(0xD0, (inner - (len(code) + 2)) & 0xFF)
emit(0x88); emit(0xD0, (outer - (len(code) + 2)) & 0xFF)
poke(0x2000, 0x88)                       # NMI on, sprites at $1000 (A12 toggles each line)
poke(0x2005, 0); poke(0x2005, 0)
poke(0xC000, SPLIT); poke(0xC001, 0); poke(0xE001, 0)
emit(0x58)                               # CLI
poke(0x2001, 0x1E)
spin = len(code); emit(0x4C, (0xE000 + spin) & 0xFF, (0xE000 + spin) >> 8)
reset = code

# ---- NMI at $E100 ----
code, emit, lda, sta, poke = asm(0xE100)
poke(0x2001, 0x1E); poke(0xC001, 0); poke(0xE001, 0)
emit(0x40)
nmi = code

# ---- IRQ at $E200 ----
code, emit, lda, sta, poke = asm(0xE200)
poke(0x2001, 0x00); poke(0xE000, 0)
emit(0x40)
irq = code

prg = bytearray(0x8000)
base = 0x6000                              # CPU $E000 lives in the last 8K bank
prg[base:base + len(reset)] = reset
prg[base + 0x100:base + 0x100 + len(nmi)] = nmi
prg[base + 0x200:base + 0x200 + len(irq)] = irq
for off, addr in ((0x1FFA, 0xE100), (0x1FFC, 0xE000), (0x1FFE, 0xE200)):
    prg[base + off:base + off + 2] = bytes([addr & 0xFF, addr >> 8])

chr_ = bytearray(0x2000)
# 2K bank 10 covers 1K banks 10,11 -> $0000-$07FF. Tile 1 sits at $0010 -> 1K bank 10 offset $10.
# (CHR is 8K = 8 x 1K, bank numbers wrap modulo 8: 10 % 8 = 2.)
chr_[2 * 0x400 + 16:2 * 0x400 + 24] = bytes([0xFF] * 8)

hdr = b"NES\x1a" + bytes([2, 1, 0x40, 0]) + bytes(8)   # 32K PRG, 8K CHR, mapper 4
open(sys.argv[1], "wb").write(hdr + prg + chr_)
