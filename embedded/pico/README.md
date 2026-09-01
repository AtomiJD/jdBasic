# jdBasic on the RP2350

The core interpreter from `src/` behind a REPL, built for the RP2350
family. Plug the board in, open its serial port, and the prompt takes
the whole language: arrays, lambdas, maps, strings, with state held
across lines. With a PicoCalc or a Fruit Jam under it, the prompt is on
the board's own screen and the serial port is only a second console.

## Building

Runs under WSL/Linux with pico-sdk 2.3.0 and Arm GNU 14.2 (default
locations under `~/pico2350`, override with PICO_SDK_PATH,
PICO_TOOLCHAIN_PATH and PICO_PIO_USB_PATH):

    ./build_pico.sh                   # pico2_w + PicoCalc -> build/
    ./build_pico.sh pico2 nocalc      # a bare board
    ./build_pico.sh fruitjam usb      # Fruit Jam, DVI and USB host
    ./build_pico.sh ... clean         # wipe the build directory first

Each combination builds in its own directory, so the matrix coexists.
Hold BOOTSEL while plugging the board in and drag the uf2 onto the
RP2350 drive. It reboots into the REPL; any serial terminal at any baud
rate reaches it.

## The boards

| | bare Pico 2 | PicoCalc | Fruit Jam |
|---|---|---|---|
| console | USB-CDC | 320x320 panel, 40x40 text | DVI 320x240, 40x30 text |
| keyboard | the terminal | the board's own | USB host |
| sound | none | the buzzer | TLV320 codec, speaker or jack |
| storage | flash store | flash store and SD | flash store and SD |
| extras | | touch-free, `LCD.*` probes | buttons, NeoPixels, IR |

## How the port is cut

- Sources come straight from `src/`: lexer, parser, compiler, vm,
  channels, file_streams and the embed API. The `PICO` define guards
  the few spots inside src the board cannot have; every other build
  leaves it unset.
- `../common/` carries what is the same job on every board and is
  compiled into all of them: `jdb_repl.cpp` is the prompt with its
  command set, history and syntax colour, `jdb_editor.cpp` the
  full-screen editor, `jdb_play.cpp` the score engine behind `PLAY`,
  `jdb_help.cpp` the on-board manual and `jdb_httpd.cpp` the web
  server. `pico_main.cpp` supplies what only this SDK can: how a byte
  arrives with a deadline, how big the console is, the note timer and
  the lock, and `INSTALL` where there is a radio.
- `shim/` carries the headers newlib does not: a threadless mutex and
  condition_variable and thread (ASYNC and CHAN degrade to no-ops),
  dirent and termios for the console paths that never run here.
- `pico_stubs.cpp` answers for the desktop pieces the board does not
  carry: the debug adapter, the AI/LLM/numerics builtin families and
  process spawning.
- `pico_fs.cpp` is the flash store, littlefs over the top of flash,
  wired into the interpreter's own file syscalls so `TXTREADER$`,
  `OPEN` and `IMPORT` all reach it. `picocalc_sd.c` puts a FAT card at
  `/sd` beside it.
- `memmap_bigstack.ld` moves the core-0 stack from the 4 KB scratch
  bank to the top of main RAM (128 KB): parser and interpreter recurse.

Image: ~1.8 MB flash for a Fruit Jam with the USB host; roughly 120 KB
of heap remain for the VM.

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
