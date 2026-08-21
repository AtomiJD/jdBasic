#!/bin/sh
# Build the REPL image. Runs under WSL/Linux with the toolchain from
# ~/pico2350 (pico-sdk 2.3.0 + Arm GNU 14.2), producing jdbasic_repl.uf2
# for a Pico 2 W. Drag the uf2 onto the BOOTSEL drive.
#
#   ./build_pico.sh          configure + build
#   ./build_pico.sh clean    wipe the build directory first

set -e
cd "$(dirname "$0")"

: "${PICO_SDK_PATH:=$HOME/pico2350/pico-sdk}"
: "${PICO_TOOLCHAIN_PATH:=$HOME/pico2350/toolchain}"
export PICO_SDK_PATH PICO_TOOLCHAIN_PATH

[ "$1" = "clean" ] && rm -rf build

cmake -G Ninja -B build -S . > build_cmake.log 2>&1 || { tail -20 build_cmake.log; exit 1; }
ninja -C build 2> build_ninja.err || { tail -30 build_ninja.err; exit 1; }
ls -la build/jdbasic_repl.uf2
