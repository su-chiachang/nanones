# nanones

A NES emulator written in C, built step by step (CPU, PPU, APU, MMC3) with the goal of running
*Super Mario Bros. 3*. 用 C 從零寫的 NES 模擬器，目標是能玩 SMB3。

- CPU: 6502 incl. unofficial opcodes, interrupt polling
- PPU: dot-accurate background/sprites, sprite 0 hit, NMI/VBL race behaviour
- APU: pulse, triangle, noise, DMC, frame counter (SDL audio)
- Mappers: 0 (NROM), 4 (MMC3 with A12-clocked scanline IRQ)

## Build & run

Needs CMake and SDL3 (`brew install cmake sdl3`).

```sh
cmake -S . -B build && cmake --build build
./build/nes-emu game.nes
```

You must supply your own legally obtained ROM; none is included.

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
- blargg suites: 41 of 47 pass. Failing: `mmc3_test_2` 4 and 6, `cpu_interrupts_v2` 2-5.
- The test ROMs are third-party works fetched by `tests/fetch_test_roms.sh` into git-ignored paths; they are not part of this repo or its license.
