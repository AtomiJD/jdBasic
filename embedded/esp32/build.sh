#!/bin/bash
# Build jdBasic for the ESP32-S3, in one of the configurations that
# matter: with the PSRAM an S3R8 carries, or without it, which is the
# part a Cardputer has, and with the console on whichever port the board
# actually brings out.
#
#   ./build.sh psram      512 KB SRAM + 8 MB PSRAM, console on UART0
#   ./build.sh nopsram    512 KB SRAM alone, console on UART0
#   ./build.sh usbconsole psram, console on the native USB port
#
# A board with no bridge chip - the 2.8 inch display board is one - has
# no UART0 on its socket, so the REPL would print into the log and hear
# nothing back. usbconsole puts both halves on the port that exists.
#
# Each keeps its own build directory, so switching between them does not
# force a rebuild of the other.

set -e
mode=${1:-psram}
idf=${IDF_PATH:-$HOME/esp/esp-idf-v5.5}
. "$idf/export.sh" >/dev/null

case "$mode" in
    psram)      defaults="sdkconfig.defaults;sdkconfig.psram" ;;
    nopsram)    defaults="sdkconfig.defaults" ;;
    usbconsole) defaults="sdkconfig.defaults;sdkconfig.psram;sdkconfig.usbconsole" ;;
    *)          echo "usage: $0 [psram|nopsram|usbconsole]"; exit 1 ;;
esac

idf.py -B "build-$mode" -DSDKCONFIG_DEFAULTS="$defaults" -DSDKCONFIG="build-$mode/sdkconfig" set-target esp32s3
idf.py -B "build-$mode" -DSDKCONFIG_DEFAULTS="$defaults" -DSDKCONFIG="build-$mode/sdkconfig" build
