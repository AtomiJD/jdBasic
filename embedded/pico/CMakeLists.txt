cmake_minimum_required(VERSION 3.13)

# jdBasic on the Pico family: the core interpreter behind a serial
# REPL. Sources come straight from ../../src; everything platform-shaped
# lives in this directory, and the PICO define guards the few spots
# inside src the board cannot have.
#
# The matrix:
#   PICO_BOARD  pico | pico_w | pico2 | pico2_w | adafruit_fruit_jam
#                                               (default pico2_w)
#   PICOCALC    ON | OFF                          (default ON)
#   FRUITJAM    ON | OFF                          (default OFF)
#
# PICOCALC=ON adds the PicoCalc's screen, keyboard, speaker, graphics
# and SD card. OFF builds for a bare board: USB-CDC console, GPIO and
# LED builtins, and the flash store - nothing that needs the device.

set(PICO_BOARD pico2_w CACHE STRING "Board type")
option(PICOCALC "Build with the PicoCalc device drivers" ON)
option(FRUITJAM "Build with the Fruit Jam device drivers" OFF)
option(FJ_USB "Build the Fruit Jam USB host (keyboard, mouse)" OFF)

# The USB host is PIO-USB, which the SDK's TinyUSB glue picks up on its
# own once it knows where the library is - it has to be told before
# pico_sdk_init pulls that glue in.
if (FRUITJAM AND FJ_USB AND DEFINED ENV{PICO_PIO_USB_PATH})
    set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH})
endif()

set(PICO_CXX_ENABLE_EXCEPTIONS 1)
set(PICO_CXX_ENABLE_RTTI 1)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(jdbasic_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(JDB_SOURCES
    pico_main.cpp
    pico_stubs.cpp
    pico_builtins.cpp
    pico_events.cpp
    pico_psram.cpp
    pico_hw.cpp
    pico_help.cpp
    pico_syntax.cpp
    pico_fs.cpp
    littlefs/lfs.c
    littlefs/lfs_util.c
    ../../src/lexer.cpp
    ../../src/parser.cpp
    ../../src/compiler.cpp
    ../../src/vm.cpp
    ../../src/jdb_embed_api.cpp
    ../../src/channels.cpp
    ../../src/sprites.cpp
    ../../src/file_streams.cpp
)

if (FRUITJAM)
    list(APPEND JDB_SOURCES
        fruitjam_dvi.c
        fruitjam_gfx.c
        fruitjam_builtins.cpp
    )
    if (FJ_USB)
        list(APPEND JDB_SOURCES fruitjam_usb.c)
    endif()
endif()

if (PICOCALC)
    list(APPEND JDB_SOURCES
        pico_editor.cpp
        picocalc_lcd.c
        picocalc_kbd.c
        picocalc_gfx.c
        picocalc_snd.c
        pico_sound.cpp
        picocalc_sd.c
        fatfs/ff.c
        fatfs/ffsystem.c
        fatfs/ffunicode.c
    )
endif()

add_executable(jdbasic_repl ${JDB_SOURCES})

target_compile_definitions(jdbasic_repl PRIVATE
    PICO
    JDB_MCU
    NDEBUG
    # A failed malloc must raise a jdBasic error, not stop the board:
    # the SDK default panics, which is an interrupts-off spin that even
    # the BOOTSEL reset cannot reach.
    PICO_MALLOC_PANIC=0
    CFG_TUD_CDC_RX_BUFSIZE=1024
    CFG_TUD_CDC_TX_BUFSIZE=1024
)
if (PICOCALC)
    target_compile_definitions(jdbasic_repl PRIVATE PICOCALC=1)
endif()
if (FRUITJAM)
    # Linking TinyUSB host makes the SDK drop its own CDC descriptors, on
    # the assumption that an app touching TinyUSB directly brings its own.
    # We want them: they carry the console and the reset-to-BOOTSEL that
    # the serial port triggers.
    target_compile_definitions(jdbasic_repl PRIVATE FRUITJAM=1)
endif()

# The host stack replaces the SDK's TinyUSB configuration wholesale, so its
# tusb_config.h lives in a directory of its own: on the general include path
# it would reach a build that has no host linked at all.
if (FRUITJAM AND FJ_USB)
    target_compile_definitions(jdbasic_repl PRIVATE
        FRUITJAM_USB=1
        PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=1
    )
    target_include_directories(jdbasic_repl PRIVATE fjusb)
endif()

target_include_directories(jdbasic_repl PRIVATE ../../src shim ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(jdbasic_repl PRIVATE -Os "$<$<COMPILE_LANGUAGE:CXX>:-include;${CMAKE_CURRENT_LIST_DIR}/shim/jdb_pico_compat.h>")

target_link_libraries(jdbasic_repl pico_stdlib hardware_flash hardware_gpio pico_flash
    hardware_adc hardware_pwm hardware_i2c hardware_spi hardware_pio)

# The HSTX scanout is DMA driven; its registers come with the structs
# headers pico_stdlib already carries.
if (FRUITJAM)
    target_link_libraries(jdbasic_repl hardware_dma pico_multicore)
endif()
if (FRUITJAM AND FJ_USB)
    target_link_libraries(jdbasic_repl tinyusb_host tinyusb_pico_pio_usb)
endif()

# The radio chip exists only on the W boards; the LED builtin adapts
# and the WiFi family comes along, lwIP in threadsafe-background mode.
if (PICO_CYW43_SUPPORTED)
    target_sources(jdbasic_repl PRIVATE pico_wifi.cpp pico_httpd.cpp)
    target_link_libraries(jdbasic_repl pico_cyw43_arch_lwip_threadsafe_background)
    target_compile_definitions(jdbasic_repl PRIVATE JDB_HAS_CYW43=1)
endif()

pico_enable_stdio_usb(jdbasic_repl 1)
pico_enable_stdio_uart(jdbasic_repl 0)

# Both chips move the core-0 stack out of the 4 KB scratch bank to the
# top of main RAM; the RP2040's smaller RAM gets the smaller cut.
if (PICO_BOARD STREQUAL "pico" OR PICO_BOARD STREQUAL "pico_w")
    pico_set_linker_script(jdbasic_repl ${CMAKE_CURRENT_LIST_DIR}/memmap_bigstack2040.ld)
else()
    pico_set_linker_script(jdbasic_repl ${CMAKE_CURRENT_LIST_DIR}/memmap_bigstack.ld)
endif()

pico_add_extra_outputs(jdbasic_repl)
