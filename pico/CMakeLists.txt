cmake_minimum_required(VERSION 3.13)

# jdBasic on the RP2350: the core interpreter behind a USB-CDC REPL.
# Sources come straight from ../src; everything platform-shaped lives in
# this directory, and the PICO define guards the few spots inside src
# the board cannot have.

set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_CXX_ENABLE_EXCEPTIONS 1)
set(PICO_CXX_ENABLE_RTTI 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(jdbasic_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(jdbasic_repl
    pico_main.cpp
    pico_stubs.cpp
    pico_builtins.cpp
    pico_fs.cpp
    littlefs/lfs.c
    littlefs/lfs_util.c
    ../src/lexer.cpp
    ../src/parser.cpp
    ../src/compiler.cpp
    ../src/vm.cpp
    ../src/jdb_embed_api.cpp
    ../src/channels.cpp
    ../src/file_streams.cpp
)

target_compile_definitions(jdbasic_repl PRIVATE
    PICO
    NDEBUG
)
target_include_directories(jdbasic_repl PRIVATE ../src shim ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(jdbasic_repl PRIVATE -Os "$<$<COMPILE_LANGUAGE:CXX>:-include;${CMAKE_CURRENT_LIST_DIR}/shim/jdb_pico_compat.h>")

target_link_libraries(jdbasic_repl pico_stdlib pico_cyw43_arch_none hardware_flash)

pico_enable_stdio_usb(jdbasic_repl 1)
pico_enable_stdio_uart(jdbasic_repl 0)
pico_set_linker_script(jdbasic_repl ${CMAKE_CURRENT_LIST_DIR}/memmap_bigstack.ld)
pico_add_extra_outputs(jdbasic_repl)
