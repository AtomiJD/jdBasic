# jdBasic on the RP2350

The core interpreter from `src/` behind a USB-CDC REPL, built for a
Raspberry Pi Pico 2 W. Plug the board in, open its serial port, and the
prompt takes the whole language: arrays, lambdas, maps, strings, with
state held across lines.

## Building

Runs under WSL/Linux with pico-sdk 2.3.0 and Arm GNU 14.2 (default
locations under `~/pico2350`, override with PICO_SDK_PATH and
PICO_TOOLCHAIN_PATH):

    ./build_pico.sh          # configure + build -> build/jdbasic_repl.uf2
    ./build_pico.sh clean    # wipe the build directory first

Hold BOOTSEL while plugging the board in and drag the uf2 onto the
RP2350 drive. It reboots into the REPL; any serial terminal at any baud
rate reaches it.

## How the port is cut

- Sources come straight from `src/`: lexer, parser, compiler, vm,
  channels, file_streams and the embed API. The `PICO` define guards
  the few spots inside src the board cannot have; every other build
  leaves it unset.
- `shim/` carries the headers newlib does not: a threadless mutex and
  condition_variable and thread (ASYNC and CHAN degrade to no-ops),
  dirent and termios for the console paths that never run here.
- `pico_stubs.cpp` answers for the desktop pieces the board does not
  carry: the debug adapter, AI/LLM/numerics builtin families, process
  spawning, filesystem syscalls (no filesystem yet - SD comes with the
  PicoCalc work).
- `memmap_bigstack.ld` moves the core-0 stack from the 4 KB scratch
  bank to the top of main RAM (128 KB): parser and interpreter recurse.

Image: ~810 KB flash, ~14 KB static RAM; roughly 370 KB of heap remain
for the VM.
