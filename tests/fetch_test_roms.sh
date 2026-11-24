#!/bin/sh
# Downloads the third-party test ROMs (not stored in this repo):
#   - nestest.nes / nestest.log       (kevtris' CPU test, from qmtpro.com)
#   - blargg's suites                 (from github.com/christopherpow/nes-test-roms);
#     the combined (non-single) ROMs use MMC1, so they double as mapper 1 tests
# Files go to tests/ (nestest) and tests/roms/ (blargg); both are git-ignored.
# usage: tests/fetch_test_roms.sh
set -e
cd "$(dirname "$0")"
RAW=https://raw.githubusercontent.com/christopherpow/nes-test-roms/master

get() { # url dest
  mkdir -p "$(dirname "$2")"
  [ -s "$2" ] || curl -fsSL -o "$2" "$1" || { echo "FAILED: $1" >&2; exit 1; }
}

get https://www.qmtpro.com/~nes/misc/nestest.nes nestest.nes
get https://www.qmtpro.com/~nes/misc/nestest.log nestest.log

ROMS="
apu_test/rom_singles/1-len_ctr.nes apu_test/rom_singles/2-len_table.nes
apu_test/rom_singles/3-irq_flag.nes apu_test/rom_singles/4-jitter.nes
apu_test/rom_singles/5-len_timing.nes apu_test/rom_singles/6-irq_flag_timing.nes
apu_test/rom_singles/7-dmc_basics.nes apu_test/rom_singles/8-dmc_rates.nes
cpu_interrupts_v2/rom_singles/1-cli_latency.nes cpu_interrupts_v2/rom_singles/2-nmi_and_brk.nes
cpu_interrupts_v2/rom_singles/3-nmi_and_irq.nes cpu_interrupts_v2/rom_singles/4-irq_and_dma.nes
cpu_interrupts_v2/rom_singles/5-branch_delays_irq.nes
instr_test-v5/rom_singles/01-basics.nes instr_test-v5/rom_singles/02-implied.nes
instr_test-v5/rom_singles/03-immediate.nes instr_test-v5/rom_singles/04-zero_page.nes
instr_test-v5/rom_singles/05-zp_xy.nes instr_test-v5/rom_singles/06-absolute.nes
instr_test-v5/rom_singles/07-abs_xy.nes instr_test-v5/rom_singles/08-ind_x.nes
instr_test-v5/rom_singles/09-ind_y.nes instr_test-v5/rom_singles/10-branches.nes
instr_test-v5/rom_singles/11-stack.nes instr_test-v5/rom_singles/12-jmp_jsr.nes
instr_test-v5/rom_singles/13-rts.nes instr_test-v5/rom_singles/14-rti.nes
instr_test-v5/rom_singles/15-brk.nes instr_test-v5/rom_singles/16-special.nes
instr_timing/rom_singles/1-instr_timing.nes instr_timing/rom_singles/2-branch_timing.nes
mmc3_test_2/rom_singles/1-clocking.nes mmc3_test_2/rom_singles/2-details.nes
mmc3_test_2/rom_singles/3-A12_clocking.nes mmc3_test_2/rom_singles/4-scanline_timing.nes
mmc3_test_2/rom_singles/5-MMC3.nes mmc3_test_2/rom_singles/6-MMC3_alt.nes
ppu_vbl_nmi/rom_singles/01-vbl_basics.nes ppu_vbl_nmi/rom_singles/02-vbl_set_time.nes
ppu_vbl_nmi/rom_singles/03-vbl_clear_time.nes ppu_vbl_nmi/rom_singles/04-nmi_control.nes
ppu_vbl_nmi/rom_singles/05-nmi_timing.nes ppu_vbl_nmi/rom_singles/06-suppression.nes
ppu_vbl_nmi/rom_singles/07-nmi_on_timing.nes ppu_vbl_nmi/rom_singles/08-nmi_off_timing.nes
ppu_vbl_nmi/rom_singles/09-even_odd_frames.nes ppu_vbl_nmi/rom_singles/10-even_odd_timing.nes
apu_test/apu_test.nes cpu_interrupts_v2/cpu_interrupts.nes instr_test-v5/all_instrs.nes
instr_test-v5/official_only.nes instr_timing/instr_timing.nes ppu_vbl_nmi/ppu_vbl_nmi.nes
"
for r in $ROMS; do get "$RAW/$r" "roms/$r"; done
echo "test ROMs ready in tests/ and tests/roms/"
