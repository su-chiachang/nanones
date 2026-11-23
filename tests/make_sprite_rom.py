#!/usr/bin/env python3
"""Builds tests/sprite_test.nes: a tiny NROM ROM exercising sprites, flips,
priority and the sprite 0 hit. After it sees the hit it stores 1 at $0000."""
import sys

code = bytearray()
def emit(*b): code.extend(b)
def lda(v): emit(0xA9, v)
def sta(a): emit(0x8D, a & 0xFF, a >> 8)
def poke(a, v): lda(v); sta(a)

emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)          # SEI CLD LDX #$FF TXS
loop = len(code)
emit(0x2C, 0x02, 0x20)                       # BIT $2002
emit(0x10, (loop - (len(code) + 2)) & 0xFF)  # BPL wait-vblank

def ppu_addr(a): poke(0x2006, a >> 8); poke(0x2006, a & 0xFF)
ppu_addr(0x3F00)
for v in [0x0F,0x16,0x2A,0x30]: poke(0x2007, v)
ppu_addr(0x3F10)
for v in [0x0F,0x21,0x28,0x30, 0x0F,0x2A,0x16,0x30]: poke(0x2007, v)
ppu_addr(0x2000 + 4*32 + 5);  poke(0x2007, 1)   # bg block at tile (5,4)
ppu_addr(0x2000 + 4*32 + 22); poke(0x2007, 1)   # bg block at tile (22,4)

poke(0x2003, 0)
sprites = [  # y, tile, attr, x
    (0x1F, 2, 0x00, 0x28),   # sprite 0, overlaps bg block -> hit
    (0x1F, 2, 0x40, 0x50),   # h-flip
    (0x1F, 2, 0x80, 0x70),   # v-flip
    (0x1F, 2, 0xC0, 0x90),   # h+v flip
    (0x1F, 2, 0x20, 0xB0),   # behind bg, over the bg block
    (0x1F, 2, 0x01, 0xD0),   # palette 1
]
for s in sprites:
    for v in s: poke(0x2004, v)
for _ in range(64 - len(sprites)):               # park the rest off screen
    for v in (0xFF, 0, 0, 0): poke(0x2004, v)

poke(0x2005, 0); poke(0x2005, 0)
poke(0x2000, 0x00)
poke(0x2001, 0x1E)                               # bg + sprites, left column on

hit = len(code)
emit(0xAD, 0x02, 0x20)                           # LDA $2002
emit(0x29, 0x40)                                 # AND #$40
emit(0xF0, (hit - (len(code) + 2)) & 0xFF)       # BEQ hit
poke(0x0000, 1)
spin = len(code)
emit(0x4C, (0x8000 + spin) & 0xFF, (0x8000 + spin) >> 8)

prg = bytearray(0x4000)
prg[:len(code)] = code
prg[0x3FFA:0x3FFC] = b"\x00\x80"; prg[0x3FFC:0x3FFE] = b"\x00\x80"; prg[0x3FFE:0x4000] = b"\x00\x80"

chr_ = bytearray(0x2000)
chr_[16:32] = bytes([0xFF]*8 + [0x00]*8)                    # tile 1: solid colour 1
chr_[32:48] = bytes([0xFF,0x80,0x80,0x80,0xF0,0x80,0x80,0x80] +
                    [0,0,0,0,0,0,0,0x01])                   # tile 2: an "F" with a colour-2 dot

hdr = b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8)
open(sys.argv[1], "wb").write(hdr + prg + chr_)
