# nanones

A NES emulator written in C, built step by step (CPU, PPU, APU, MMC3) with the goal of running
*Super Mario Bros. 3*. 用 C 從零寫的 NES 模擬器，目標是能玩 SMB3。

- CPU: 6502 incl. unofficial opcodes, interrupt polling
- PPU: dot-accurate background/sprites, sprite 0 hit, NMI/VBL race behaviour
- APU: pulse, triangle, noise, DMC, frame counter (SDL audio)
- Mappers: 0 (NROM), 4 (MMC3 with A12-clocked scanline IRQ)

## Build & run

Needs CMake, SDL3 and zlib (zlib ships with macOS) (`brew install cmake sdl3`).

```sh
cmake -S . -B build && cmake --build build
./build/nes-emu game.nes
```

The ROM may be an iNES `.nes` file or a `.zip` containing one (stored or deflated; the first
`.nes` entry is used). Battery-backed games save to a `.sav` next to the ROM (`roms/dq4.zip` →
`roms/dq4.sav`), written whenever the save RAM changes and on quit. You must supply your own legally obtained ROM; none is included.

Keys: arrows = D-pad, Z = B, X = A, Enter = Start, Right Shift = Select.
F1 toggles the coordinate overlay (SMB3 only), F2 opens key settings, Esc quits.
`--headless N out.ppm "frame:hexmask,..."` runs without a window; `--replay file.inp` replays recorded input.

## Tests

```sh
tests/fetch_test_roms.sh     # downloads nestest + blargg's ROMs (not stored in this repo)
cmake -S . -B build && cmake --build build
ctest --test-dir build
tests/run_blargg.sh          # full table, including the ones that still fail
SMB3_ROM=/path/smb3.nes tests/smb3_smoke.sh
```

- nestest: all 8991 log lines match (CPU, cycles and PPU dot).
- blargg suites: 46 of 47 pass. The one failure is `mmc3_test_2` 6, which checks MMC3 revision A behaviour (SMB3 uses revision B, covered by test 5).
- The test ROMs are third-party works fetched by `tests/fetch_test_roms.sh` into git-ignored paths; they are not part of this repo or its license.
