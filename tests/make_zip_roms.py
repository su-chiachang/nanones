#!/usr/bin/env python3
"""Writes zip archives of a test ROM for the zip-loading tests.

usage: make_zip_roms.py rom.nes outdir
  deflate.zip  the ROM, deflated
  stored.zip   the ROM, uncompressed
  macos.zip    a directory entry and a __MACOSX/._ resource fork (which also
               ends in .nes) ahead of the real ROM, like Finder produces
"""
import os
import sys
import zipfile

rom_path, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
rom = open(rom_path, "rb").read()
name = os.path.basename(rom_path)

with zipfile.ZipFile(os.path.join(out, "deflate.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(name, rom)
with zipfile.ZipFile(os.path.join(out, "stored.zip"), "w", zipfile.ZIP_STORED) as z:
    z.writestr(name, rom)
with zipfile.ZipFile(os.path.join(out, "macos.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("__MACOSX/", b"")
    z.writestr("__MACOSX/._" + name, b"\x00\x05\x16\x07junk")
    z.writestr("game/._" + name, b"\x00\x05\x16\x07junk")
    z.writestr("game/" + name, rom)
