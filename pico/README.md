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

## Coming from MicroPython

The stock PicoCalc firmware leaves an RP2350 partition table at the
start of flash. The bootrom then loads any uf2 into a partition and
switches on QMI address translation: reads above 1 MB return 0xFF and
the flash store cannot work. The prompt has the tools to see and fix
it once:

    PRINT FS.ATRANS()   ' identity is 04000000 04000400 04000800 04000c00
    FS.NUKEPT()         ' erase the partition table, drop to BOOTSEL

then copy the uf2 onto the drive again. FS.TEST() exercises the flash
layer end to end and reports each step - note that it reformats the
store.
