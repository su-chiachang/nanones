cmake_minimum_required(VERSION 3.16)
project(nes-emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
add_library(nescpu STATIC src/cpu.c src/apu.c src/cart.c src/ppu.c src/nes.c src/zip.c)
target_compile_options(nescpu PRIVATE -Wall -Wextra)
target_link_libraries(nescpu PUBLIC ZLIB::ZLIB)

add_executable(nestest_runner tests/nestest_runner.c)
target_compile_options(nestest_runner PRIVATE -Wall -Wextra)
target_link_libraries(nestest_runner PRIVATE nescpu)

add_executable(cpu_interrupt_test tests/cpu_interrupt_test.c)
target_compile_options(cpu_interrupt_test PRIVATE -Wall -Wextra)
target_link_libraries(cpu_interrupt_test PRIVATE nescpu)

add_executable(ppu_sprite_test tests/ppu_sprite_test.c)
target_compile_options(ppu_sprite_test PRIVATE -Wall -Wextra)
target_link_libraries(ppu_sprite_test PRIVATE nescpu)

add_executable(mmc3_irq_test tests/mmc3_irq_test.c)
target_compile_options(mmc3_irq_test PRIVATE -Wall -Wextra)
target_link_libraries(mmc3_irq_test PRIVATE nescpu)

add_executable(apu_test tests/apu_test.c)
target_compile_options(apu_test PRIVATE -Wall -Wextra)
target_link_libraries(apu_test PRIVATE nescpu m)

add_executable(smb3_bot tests/smb3_bot.c)
target_link_libraries(smb3_bot PRIVATE nescpu)

add_executable(blargg_runner tests/blargg_runner.c)
target_link_libraries(blargg_runner PRIVATE nescpu)

enable_testing()
add_test(NAME apu COMMAND apu_test)
add_test(NAME mmc3_irq COMMAND mmc3_irq_test ${CMAKE_SOURCE_DIR}/tests/mmc3_irq_test.nes)
add_test(NAME ppu_sprite COMMAND ppu_sprite_test ${CMAKE_SOURCE_DIR}/tests/sprite_test.nes)
add_test(NAME cpu_interrupt COMMAND cpu_interrupt_test)
# Loading ROMs from zip archives (deflated, stored, macOS metadata decoys) and NES 2.0 headers.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME zip_make
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/make_zip_roms.py
                   ${CMAKE_SOURCE_DIR}/tests/sprite_test.nes ${CMAKE_BINARY_DIR}/zip_roms)
  set_tests_properties(zip_make PROPERTIES FIXTURES_SETUP zip_roms)
  add_test(NAME nes2_make
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/make_nes2_rom.py
                   ${CMAKE_SOURCE_DIR}/tests/mmc3_irq_test.nes ${CMAKE_BINARY_DIR}/nes2/mmc3_irq_test.nes)
  set_tests_properties(nes2_make PROPERTIES FIXTURES_SETUP nes2_rom)
  add_test(NAME nes2_header COMMAND mmc3_irq_test ${CMAKE_BINARY_DIR}/nes2/mmc3_irq_test.nes)
  set_tests_properties(nes2_header PROPERTIES FIXTURES_REQUIRED nes2_rom)
  foreach(kind deflate stored macos)
    add_test(NAME zip_${kind} COMMAND ppu_sprite_test ${CMAKE_BINARY_DIR}/zip_roms/${kind}.zip)
    set_tests_properties(zip_${kind} PROPERTIES FIXTURES_REQUIRED zip_roms)
  endforeach()
endif()
# Third-party ROMs are not stored in the repo: run tests/fetch_test_roms.sh first.
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/nestest.nes)
  add_test(NAME nestest
           COMMAND nestest_runner
                   ${CMAKE_SOURCE_DIR}/tests/nestest.nes
                   ${CMAKE_SOURCE_DIR}/tests/nestest.log)
else()
  message(STATUS "tests/nestest.nes missing: run tests/fetch_test_roms.sh to enable the nestest and blargg tests")
endif()

# blargg test ROMs that currently pass (see tests/run_blargg.sh for the full table)
foreach(rom
    tests/roms/apu_test/rom_singles/1-len_ctr.nes
    tests/roms/apu_test/rom_singles/2-len_table.nes
    tests/roms/apu_test/rom_singles/3-irq_flag.nes
    tests/roms/apu_test/rom_singles/4-jitter.nes
    tests/roms/apu_test/rom_singles/5-len_timing.nes
    tests/roms/apu_test/rom_singles/6-irq_flag_timing.nes
    tests/roms/apu_test/rom_singles/7-dmc_basics.nes
    tests/roms/apu_test/rom_singles/8-dmc_rates.nes
    tests/roms/cpu_interrupts_v2/rom_singles/1-cli_latency.nes
    tests/roms/cpu_interrupts_v2/rom_singles/2-nmi_and_brk.nes
    tests/roms/cpu_interrupts_v2/rom_singles/3-nmi_and_irq.nes
    tests/roms/cpu_interrupts_v2/rom_singles/4-irq_and_dma.nes
    tests/roms/cpu_interrupts_v2/rom_singles/5-branch_delays_irq.nes
    tests/roms/instr_test-v5/rom_singles/01-basics.nes
    tests/roms/instr_test-v5/rom_singles/02-implied.nes
    tests/roms/instr_test-v5/rom_singles/03-immediate.nes
    tests/roms/instr_test-v5/rom_singles/04-zero_page.nes
    tests/roms/instr_test-v5/rom_singles/05-zp_xy.nes
    tests/roms/instr_test-v5/rom_singles/06-absolute.nes
    tests/roms/instr_test-v5/rom_singles/07-abs_xy.nes
    tests/roms/instr_test-v5/rom_singles/08-ind_x.nes
    tests/roms/instr_test-v5/rom_singles/09-ind_y.nes
    tests/roms/instr_test-v5/rom_singles/10-branches.nes
    tests/roms/instr_test-v5/rom_singles/11-stack.nes
    tests/roms/instr_test-v5/rom_singles/12-jmp_jsr.nes
    tests/roms/instr_test-v5/rom_singles/13-rts.nes
    tests/roms/instr_test-v5/rom_singles/14-rti.nes
    tests/roms/instr_test-v5/rom_singles/15-brk.nes
    tests/roms/instr_test-v5/rom_singles/16-special.nes
    tests/roms/instr_timing/rom_singles/1-instr_timing.nes
    tests/roms/instr_timing/rom_singles/2-branch_timing.nes
    tests/roms/mmc3_test_2/rom_singles/1-clocking.nes
    tests/roms/mmc3_test_2/rom_singles/2-details.nes
    tests/roms/mmc3_test_2/rom_singles/3-A12_clocking.nes
    tests/roms/mmc3_test_2/rom_singles/4-scanline_timing.nes
    tests/roms/mmc3_test_2/rom_singles/5-MMC3.nes
    tests/roms/ppu_vbl_nmi/rom_singles/01-vbl_basics.nes
    tests/roms/ppu_vbl_nmi/rom_singles/02-vbl_set_time.nes
    tests/roms/ppu_vbl_nmi/rom_singles/03-vbl_clear_time.nes
    tests/roms/ppu_vbl_nmi/rom_singles/04-nmi_control.nes
    tests/roms/ppu_vbl_nmi/rom_singles/05-nmi_timing.nes
    tests/roms/ppu_vbl_nmi/rom_singles/06-suppression.nes
    tests/roms/ppu_vbl_nmi/rom_singles/07-nmi_on_timing.nes
    tests/roms/ppu_vbl_nmi/rom_singles/08-nmi_off_timing.nes
    tests/roms/ppu_vbl_nmi/rom_singles/09-even_odd_frames.nes
    tests/roms/ppu_vbl_nmi/rom_singles/10-even_odd_timing.nes
)
  get_filename_component(rom_name ${rom} NAME_WE)
  get_filename_component(rom_dir ${rom} DIRECTORY)
  get_filename_component(rom_group ${rom_dir} DIRECTORY)
  get_filename_component(rom_group ${rom_group} NAME)
  if(EXISTS ${CMAKE_SOURCE_DIR}/${rom})
    add_test(NAME ${rom_group}/${rom_name} COMMAND blargg_runner ${CMAKE_SOURCE_DIR}/${rom} 2400)
  endif()
endforeach()

find_package(SDL3 CONFIG)
if(SDL3_FOUND)
  add_executable(nes-emu src/main.c)
  target_compile_options(nes-emu PRIVATE -Wall -Wextra)
  target_link_libraries(nes-emu PRIVATE nescpu SDL3::SDL3)
else()
  message(STATUS "SDL3 not found: skipping the nes-emu front end")
endif()
