#!/bin/bash
# Build jdBasic for the ESP32-S3, in one of the two configurations that
# matter: with the PSRAM an S3R8 carries, or without it, which is the
# part a Cardputer has.
#
#   ./build.sh psram      512 KB SRAM + 8 MB PSRAM
#   ./build.sh nopsram    512 KB SRAM alone
#
# Each keeps its own build directory, so switching between them does not
# force a rebuild of the other.

set -e
mode=${1:-psram}
idf=${IDF_PATH:-$HOME/esp/esp-idf-v5.5}
. "$idf/export.sh" >/dev/null

case "$mode" in
    psram)   defaults="sdkconfig.defaults;sdkconfig.psram" ;;
    nopsram) defaults="sdkconfig.defaults" ;;
    *)       echo "usage: $0 [psram|nopsram]"; exit 1 ;;
esac

idf.py -B "build-$mode" -DSDKCONFIG_DEFAULTS="$defaults" -DSDKCONFIG="build-$mode/sdkconfig" set-target esp32s3
idf.py -B "build-$mode" -DSDKCONFIG_DEFAULTS="$defaults" -DSDKCONFIG="build-$mode/sdkconfig" build
