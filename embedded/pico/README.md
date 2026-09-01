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

Image: ~1.8 MB flash for a Fruit Jam with the USB host; roughly 40 KB of
SRAM heap remain for the VM once the framebuffer is out, with about 39 KB
of that in one piece.

That number is the ceiling on how big a program can be, and it is low:
a 169-byte program loads, a 1305-byte one does not. Loading is what
costs, not running - the lexer reserves `source/3` tokens up front and a
token carries a std::string, so a kilobyte of source wants tens of
kilobytes in one contiguous block, on top of the AST and the chunk.
The compiled p-code is small; the compiler is not.

### Where the 327 KB goes

`./build_pico.sh fruitjam usb heaptrace` prints what each step of coming
up costs, and `SYS.HEAP$` says where the heap stands at any moment.
Measured on the board:

| | bytes |
|---|---|
| framebuffer, 640 by 240 at a byte a pixel | 153,600 |
| DVI command list, two words per transfer | 8,048 |
| flash store, USB host, sound, buttons | ~1,100 |
| **everything above, before the VM exists** | **162,764** |
| constructing the VM | 84,880 |
| **at the prompt, before any program** | **247,644** |

The heap region is 327,188 bytes, so a program starts with about 79 KB
and the prompt's own working set takes it to roughly 41 KB free with 39
in one piece.

Two things dominate, and neither is the interpreter's bytecode. The
framebuffer is half the heap and it is the obvious candidate for moving
into PSRAM: the scanout DMA can read the XIP window, and at 640 bytes a
line read twice for the vertical doubling it wants 18.4 MB/s against a
QMI that has about 63. The VM's 85 KB is the other half of the problem;
`SYS.NATIVES` says 386 builtins with 3.2 KB of names between them, so
the names are not it and the rest is worth measuring before it is
guessed at.

## The Fruit Jam's PSRAM, and where it stands

The board has 8 MB on QMI chip select 1. The SDK's `hardware_psram`
maps it from the board header, and `psram_reinitialize` is called after
`set_sys_clock_khz` because the timings computed during runtime init
describe the clock the board booted on, not the 126 MHz the scanout
needs. The window is proven: `PSRAM.TEST$` reads a pattern back from
both ends, and `PSRAM.TORTURE$` allocates, fills, verifies and frees a
few thousand blocks while the scanout and the USB host are running -
"1508 blocks, intact". `tests/psram_heap_test.cpp` runs the same
allocator on the desktop under AddressSanitizer.

`./build_pico.sh fruitjam usb psram` puts the interpreter's allocations
of 512 bytes and up in it. Free SRAM at the prompt goes from 41 KB to
91 KB, and the ceiling above lifts: the 73 KB desktop space shooter
loads and starts, where a 3 KB program cannot without it.

It is **off by default** because one thing still fails, and it reduces
to this:

    DIM n AS INTEGER = 6
    DIM a[6]
    FOR i = 0 TO n - 1
        a[i] = RND(1) * 320      ' a native call inside the loop
    NEXT i

With the pool on, that program dies partway with no message and takes
the board with it. `a[i] = i * 0.5` in the same loop is fine, and
`x = RND(1)` on its own is fine; it is the native call inside the loop
that does it. With the pool off the same program runs. The pool itself
is not the suspect - the arithmetic is proven on both sides - so the
next thing to look at is what a native call does to the value stack
while that stack lives behind the XIP cache.

Raising the threshold to 16 KB makes that program run again, which is
the clue: whatever breaks is a block between 512 bytes and 16 KB. But
it also gives the ceiling back, because the allocations that exhaust
SRAM during a load are individually smaller than that. The two ends of
the trade are one constant apart, `PSRAM_MIN` in
`fruitjam_psram_heap.cpp`, which is where to start.

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
