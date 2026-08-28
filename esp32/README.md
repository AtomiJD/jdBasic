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

## The flash store

A 2 MB FATFS partition on wear levelling, mounted as the *default*
filesystem rather than under a prefix. IDF has no `chdir`, so a mount at
`/flash` would leave the interpreter's own opens - `IMPORT`,
`TXTREADER$`, `OPEN` - unable to find anything. Registering a VFS with
an empty prefix makes it the fallback for every path that matches no
other mount, and bare names work everywhere.

One consequence: IDF builds FATFS with `FF_FS_RPATH 0`, so there is no
working directory at all and `"."` never resolves. `DIR$` starts its
listing at the root on this target.

At the prompt:

    DIR                 the store, name and size
    LIST                the current program, numbered and coloured
    LIST name           any other file
    RUN name            load and run a program
    LOAD name           remember it, so a bare RUN repeats it
    TYPE name           print a file
    DEL name            remove one
    COPY from to        and REN from to
    RECV name           take a file straight off the wire
    AUTORUN name        run it at power-on; OFF clears it; bare reports

`SYS.DF` reports the store as a line, `SYS.FREEDISK` as a number.

A name without an extension may mean the `.jdb` of that name: `RUN hello`
finds `hello.jdb`. What was typed wins, so a file that really has no
extension is still reachable. `RECV` is the exception and writes exactly
the name given, because not everything on the store is a program.

`RECV` parses nothing and echoes nothing, so a program arrives at the
speed of the link. It ends on a single `0x04`, or after three seconds of
silence once data has started. Send in small chunks with a short pause,
128 bytes every 30 ms works, and any line ending is fine.

`AUTORUN` leaves two seconds of ESC at boot to cancel, so a misbehaving
program never locks the board out.

### The examples

`fs/` becomes the storage partition at build time, so a freshly flashed
board already has something to run:

    hello.jdb     the board, its two memories and the store
    primes.jdb    a sieve over 20000, as a mask rather than a loop
    mem.jdb       where an array lives, and what an element costs
    mandel.jdb    the set as text, 78 by 24, about 1.3 s
    bench.jdb     vectors, an interpreted loop, string building

Reflashing `storage.bin` resets the store, which is why `build.sh` does
not do it. Flash it once at `0x410000`, then leave it alone and use
`RECV` for everything after.

### What ends a run

Reducers copy. `SUM` over a 100000-element array asks for about 2.2 MB
on top of the 2.4 MB the array already holds, and then wants more, so it
fails while five megabytes are still free. 50000 elements is comfortable.
The limit to watch is `SYS.LARGEST`, not `SYS.FREE`.

## The radio

The S3 is a station on someone else's network or its own access point,
and jdBasic reaches both. It is a mode rather than a state: the radio
costs about 113 KB of internal RAM, which is most of what the
interpreter has, so it is started and stopped rather than left on.

    WIFI.AP(ssid$ [, pass$ [, channel]])   own network; open with no pass
    WIFI.CONNECT(ssid$, pass$ [, ms])      join one; 0 on success
    WIFI.AUTO                              the two lines from wifi.txt
    WIFI.OFF                               give the memory back
    WIFI.STATUS                            0 down, 1 serving, 2 joined
    WIFI.IP$   WIFI.MAC$   WIFI.CLIENTS   WIFI.DIAG$

An access point comes up on 192.168.4.1 with a DHCP server behind it.
`WIFI.OFF` returns about 44 KB of the 113; the rest belongs to the
TCP/IP stack, which is set up once and not torn down again.

The HTTP server is `pico/pico_httpd.cpp`, compiled by both ports. It is
raw lwIP, and lwIP is the same library here; what differs is who owns
it. On the RP2350 the callbacks run in the radio interrupt and the code
brackets them with the SDK's lock, here lwIP has its own task and the
brackets are the core lock. The millisecond clock, the sleep, the netif
pump and the non-blocking key read are named rather than taken from an
SDK, and the file picks a side with one `#ifdef`.

    HTTP.SERVER.ON_GET(path$, handler$)    and ON_POST, ON_NOTFOUND
    HTTP.SERVER.START(port)                and STOP
    HTTP.SERVER.POLL                       one pass
    HTTP.SERVER.WAIT(ms)                   keep going; 0 means forever
    HTTP.SERVER.SERVED                     requests so far

`fs/hotspot.jdb` is the two together: the board raises its own network,
serves a page built by a jdBasic function, and ESC takes it all down
again.

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
