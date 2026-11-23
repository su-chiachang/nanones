cmake_minimum_required(VERSION 3.16)
project(nes-emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(nescpu STATIC src/cpu.c src/cart.c src/ppu.c src/nes.c)
target_compile_options(nescpu PRIVATE -Wall -Wextra)

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

enable_testing()
add_test(NAME mmc3_irq COMMAND mmc3_irq_test ${CMAKE_SOURCE_DIR}/tests/mmc3_irq_test.nes)
add_test(NAME ppu_sprite COMMAND ppu_sprite_test ${CMAKE_SOURCE_DIR}/tests/sprite_test.nes)
add_test(NAME cpu_interrupt COMMAND cpu_interrupt_test)
add_test(NAME nestest
         COMMAND nestest_runner
                 ${CMAKE_SOURCE_DIR}/tests/nestest.nes
                 ${CMAKE_SOURCE_DIR}/tests/nestest.log)

find_package(SDL3 CONFIG)
if(SDL3_FOUND)
  add_executable(nes-emu src/main.c)
  target_compile_options(nes-emu PRIVATE -Wall -Wextra)
  target_link_libraries(nes-emu PRIVATE nescpu SDL3::SDL3)
else()
  message(STATUS "SDL3 not found: skipping the nes-emu front end")
endif()
