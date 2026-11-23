cmake_minimum_required(VERSION 3.16)
project(nes-emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(nescpu STATIC src/cpu.c)
target_compile_options(nescpu PRIVATE -Wall -Wextra)

add_executable(nestest_runner tests/nestest_runner.c)
target_compile_options(nestest_runner PRIVATE -Wall -Wextra)
target_link_libraries(nestest_runner PRIVATE nescpu)

add_executable(cpu_interrupt_test tests/cpu_interrupt_test.c)
target_compile_options(cpu_interrupt_test PRIVATE -Wall -Wextra)
target_link_libraries(cpu_interrupt_test PRIVATE nescpu)

enable_testing()
add_test(NAME cpu_interrupt COMMAND cpu_interrupt_test)
add_test(NAME nestest
         COMMAND nestest_runner
                 ${CMAKE_SOURCE_DIR}/tests/nestest.nes
                 ${CMAKE_SOURCE_DIR}/tests/nestest.log)
