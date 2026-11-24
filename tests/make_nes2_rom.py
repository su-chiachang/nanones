#!/usr/bin/env python3
"""Rewrites an iNES 1.0 ROM's header as NES 2.0 (same mapper and sizes).

usage: make_nes2_rom.py in.nes out.nes
"""
import os
import sys

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, "rb").read())
d[7] = (d[7] & 0xF0) | 0x08                         # NES 2.0 identifier
d[8] = 0                                            # mapper bits 8-11, submapper
d[9] = 0                                            # PRG/CHR size MSB nibbles
d[10] = 0x70 if d[6] & 0x02 else 0x07               # 8 KiB PRG-RAM (NVRAM if battery)
d[11] = 0x07 if d[5] == 0 else 0x00                 # 8 KiB CHR-RAM when no CHR ROM
d[12:16] = bytes(4)
os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
open(dst, "wb").write(d)
