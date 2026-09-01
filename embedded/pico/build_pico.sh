#!/bin/sh
# Build the REPL image. Runs under WSL/Linux with the toolchain from
# ~/pico2350 (pico-sdk 2.3.0 + Arm GNU 14.2), producing jdbasic_repl.uf2.
# Drag the uf2 onto the BOOTSEL drive.
#
#   ./build_pico.sh [board] [calc|nocalc] [clean]
#
#   board   pico | pico_w | pico2 | pico2_w | fruitjam  (default pico2_w)
#   calc    with the PicoCalc drivers (default); nocalc = bare board
#   usb     fruitjam only: build the USB host (keyboard, mouse)
#   psram   fruitjam only: put the interpreter's large allocations
#           in the 8 MB on QMI chip select 1 (see README)
#   heaptrace  print what each boot step costs the heap
#
# Each combination builds in its own directory, build-<board>[-nocalc],
# so the matrix coexists. The plain "build" directory stays the default
# pico2_w + PicoCalc image.

set -e
cd "$(dirname "$0")"

: "${PICO_SDK_PATH:=$HOME/pico2350/pico-sdk}"
: "${PICO_TOOLCHAIN_PATH:=$HOME/pico2350/toolchain}"
: "${PICO_PIO_USB_PATH:=$HOME/pico2350/Pico-PIO-USB}"
export PICO_SDK_PATH PICO_TOOLCHAIN_PATH PICO_PIO_USB_PATH

BOARD=pico2_w
CALC=ON
JAM=OFF
USBHOST=OFF
PSRAMHEAP=OFF
HEAPTRACE=OFF
CLEAN=0
for a in "$@"; do
    case "$a" in
        pico|pico_w|pico2|pico2_w) BOARD=$a ;;
        fruitjam|adafruit_fruit_jam) BOARD=adafruit_fruit_jam; CALC=OFF; JAM=ON ;;
        calc)   CALC=ON ;;
        nocalc) CALC=OFF ;;
        usb)    USBHOST=ON ;;
        psram)  PSRAMHEAP=ON ;;
        heaptrace) HEAPTRACE=ON ;;
        clean)  CLEAN=1 ;;
        *) echo "unknown argument: $a"; exit 1 ;;
    esac
done

if [ "$BOARD" = "pico2_w" ] && [ "$CALC" = "ON" ]; then
    DIR=build
else
    DIR=build-$BOARD
    [ "$CALC" = "OFF" ] && DIR=$DIR-nocalc
    [ "$USBHOST" = "ON" ] && DIR=$DIR-usb
    [ "$PSRAMHEAP" = "ON" ] && DIR=$DIR-psram
fi

[ "$CLEAN" = "1" ] && rm -rf "$DIR"

cmake -G Ninja -B "$DIR" -S . -DPICO_BOARD=$BOARD -DPICOCALC=$CALC -DFRUITJAM=$JAM -DFJ_USB=$USBHOST -DFJ_PSRAM_HEAP=$PSRAMHEAP -DFJ_HEAP_TRACE=$HEAPTRACE \
    > build_cmake.log 2>&1 || { tail -20 build_cmake.log; exit 1; }
ninja -C "$DIR" 2> build_ninja.err || { tail -30 build_ninja.err; exit 1; }
ls -la "$DIR"/jdbasic_repl.uf2
