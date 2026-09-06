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
shift || true
# Knobs after the mode, each with the small value as its default:
# stack=96 (main task stack in KB, 64), frames=512 (call depth, 256),
# hist=16 histlen=256 (the prompt's history, 8 by 128), fat (keep the
# desktop-only builtins and std::regex).
extra=""
stack=""
for a in "$@"; do
    case "$a" in
        stack=*)   stack=${a#stack=} ;;
        hist=*)    extra="$extra -DJDB_HIST_N=${a#hist=}" ;;
        histlen=*) extra="$extra -DJDB_HIST_LEN=${a#histlen=}" ;;
        fat)       extra="$extra -DJDB_LEAN=0" ;;
        frames=*)  extra="$extra -DJDB_MAX_FRAMES=${a#frames=}" ;;
        lean)      extra="$extra -DJDB_LEAN=1" ;;
        *) echo "unknown argument: $a"; exit 1 ;;
    esac
done
idf=${IDF_PATH:-$HOME/esp/esp-idf-v5.5}
. "$idf/export.sh" >/dev/null

case "$mode" in
    psram)      defaults="sdkconfig.defaults;sdkconfig.psram" ;;
    nopsram)    defaults="sdkconfig.defaults" ;;
    usbconsole) defaults="sdkconfig.defaults;sdkconfig.psram;sdkconfig.usbconsole" ;;
    *)          echo "usage: $0 [psram|nopsram|usbconsole]"; exit 1 ;;
esac
if [ -n "$stack" ]; then
    echo "CONFIG_ESP_MAIN_TASK_STACK_SIZE=$((stack * 1024))" > "build-$mode.stack"
    defaults="$defaults;build-$mode.stack"
fi

idf.py -B "build-$mode" -DSDKCONFIG_DEFAULTS="$defaults" -DSDKCONFIG="build-$mode/sdkconfig" $extra set-target esp32s3
idf.py -B "build-$mode" -DSDKCONFIG_DEFAULTS="$defaults" -DSDKCONFIG="build-$mode/sdkconfig" $extra build
