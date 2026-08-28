# jdBasic on the ESP32-S3

A serial REPL on the S3's native USB port, the same shape as `pico/`:
the interpreter comes from `../src` unchanged, everything that knows it
is on an S3 lives in `main/`.

This is the headless stage. No display, no keyboard, no filesystem. It
exists to answer one question with a number instead of an estimate: how
much room does the interpreter leave on a 512 KB part.

## Building

Needs ESP-IDF v5.5 (GCC 14.2, the same compiler the RP2350 build uses).

    ./build.sh nopsram      512 KB SRAM alone      -> an S3FN8 part
    ./build.sh psram        plus 8 MB PSRAM        -> an S3R8 part

Each keeps its own build directory. `psram` needs the 240 MHz clock that
`sdkconfig.defaults` sets; at IDF's default of 160 MHz the first access
to PSRAM stalls the bus. `sdkconfig.psram` has the details.

## Flashing

The DevKitC-1's native USB port enumerates as `303a:4001` running, and
`303a:1001` in download mode. esptool's `--before usb-reset` gets it
there; `--after hard-reset` does not bring it back, because that resets
via an RTS pin the native USB path does not have. Use
`--after watchdog-reset`.

    esptool --chip esp32s3 --port COMn --baud 921600 \
        --before usb-reset --after no-reset \
        write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
        0x0 build-nopsram/bootloader/bootloader.bin \
        0x8000 build-nopsram/partition_table/partition-table.bin \
        0x10000 build-nopsram/jdbasic_esp32.bin

Check the run wrote three times "Hash of data verified". A flash that
fails leaves the previous image running, and the boot log then looks
like a build that did not take.

If the board falls off USB entirely, unplug it. Nothing software-side
brings it back.

## What it measures

`SYS.FREE` and `SYS.LARGEST` answer directly here; the RP2350 has to
find the largest block by binary search over malloc. `SYS.INTERNAL` and
`SYS.PSRAM` separate the two pools, and `SYS.MEM` prints both with the
low-water mark. `esp32_main.cpp` reports the heap before and after the
VM is built, which is what the interpreter's own baseline costs.

Measured on a DevKitC-1 N16R8, 96 KB REPL stack, at 240 MHz:

    without PSRAM
    boot         internal  280000   largest  221184
    after init   internal  181416   largest  139264
    VM costs     internal   98584

    with PSRAM
    boot         internal  272643   largest  180224   psram  8386156
    after init   internal  198639   largest  124928   psram  8361576
    VM costs     internal   74004                     psram    24580

For comparison the PicoCalc reports 120376 free at a bare prompt. Even
with PSRAM out the S3 has about half again as much room, on a part with
8 KB less SRAM, because the RP2350 build spends 128 KB of its on a stack
in the linker script and carries the panel, keyboard and flash store as
well.

With PSRAM in, the interpreter's own tables stay internal - they are
below the 16 KB threshold that decides where an allocation goes - and
arrays go outside. A jdBasic array costs about 24.2 bytes an element,
measured twice:

    IOTA(50000)    1212420 bytes of PSRAM
    IOTA(100000)   2424836 bytes of PSRAM

So roughly 340,000 elements fit, against the 2,500 or so that fit in
what a PicoCalc has left at its prompt. What ends the run is the largest
free block rather than the total: IOTA(200000) wants 4.85 MB in one
piece and fails while 4.72 MB is the biggest the heap will hand over.

## Shared with the pico port

Six guards in `src/` say "small controller" rather than "RP2350": the
chunk shrink, the read-once program load, the lexer's token reserve, the
VM's opening stack size, character-at-a-time INPUT, and the narrow DIR
listing. Those are `JDB_MCU`, which both ports define. `PICO` and
`ESP32` are left for what is genuinely one board's: the platform's own
builtins and its event poll.
