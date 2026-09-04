# jdBasic on the ESP32-S3

A serial REPL on the S3's native USB port, the same shape as `pico/`:
the interpreter comes from `../../../src` unchanged, everything that knows it
is on an S3 lives in `main/`.

This is the headless stage: a flash store and the radio, no display and
no keyboard. It answers one question with a number instead of an
estimate - how much room does the interpreter leave on a 512 KB part.

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

The HTTP server is `common/jdb_httpd.cpp`, compiled by every port. It is
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

## Pins, converters, buses

The verbs are the RP2350 ones, so a program reads the same on either
board. What differs is which numbers are yours:

    GPIO 26 to 32     the SPI flash
    GPIO 33 to 37     the octal PSRAM
    GPIO 43 and 44    the console UART

Touching any of those takes the board down with no diagnostic, so the
answer comes before the write rather than after it: every verb refuses
them by name, and `PIN.FREE` lists what is left. On this build that is
0 to 25, 38 to 42, and 45 to 48.

    GPIO.MODE(pin, output)     1 output, 0 input
    GPIO.WRITE(pin, level)     GPIO.READ(pin)
    GPIO.PULLUP(pin [, on])

    ADC.READ(pin)              raw, and the converter is on GPIO 1 to 10
    ADC.TEMP                   the chip's own sensor, in degrees

    PWM.SET(pin, hz [, duty])  duty in percent, eight channels
    PWM.OFF(pin)

    I2C.SETUP(bus, sda, scl)   bus 0 or 1
    I2C.WRITE(bus, addr, data [, hz])
    I2C.READ(bus, addr, n [, hz])
    I2C.SCAN(bus)              the addresses that answered

    SPI.SETUP(bus, sck, mosi, miso [, hz])
    SPI.XFER(bus, data)        full duplex, same length back

Speed is per device on this chip rather than per bus, so `I2C.WRITE` and
`I2C.READ` take it where the transfer happens. `SPI.SETUP` leaves chip
select alone: drive it with `GPIO.WRITE`, which is what a display or a
card needs anyway.

### Events

The interrupts only record what happened; the VM drains the record
between statements, so a handler never runs inside an ISR, and handlers
do not nest. A tick that arrives while one is still running is dropped
rather than queued, so a slow handler cannot build a backlog.

    TIMER.EVERY(ms)   with ON "TICK" CALL Handler
    TIMER.STOP
    GPIO.WATCH(pin, edge)   1 rising, 2 falling, 3 both, 0 stop
                            with ON "PIN" CALL Handler(pin, level)
    KEY.WATCH(on)           with ON "KEY" CALL Handler(code)
    PIN.DIAG$               what the interrupt has seen

`fs/pins.jdb` walks the lot: the free pins, the temperature, an analogue
reading, a square wave, an empty bus scan, and five timed samples.

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


`SYS.NATIVES` breaks the second of those down. The heap is sampled at
the top of `register_builtins`, which runs before a single native
exists, and again the moment `app_main` gets the VM back:

    342 natives, 2869 bytes of name, cost internal 54168

342 rather than the 869 `register_native` calls in `src/`, because TUI,
GUI, AI, FORM, GL, SOUND and the rest sit behind feature flags the board
does not build. 279 of the 869 are the language itself. The name text is
2.9 KB of it, so what the table costs is structure rather than text: a
hash node, a `std::function`, and an entry in the global slot registry
for each one.

The VM's own value stack is the PSRAM figure above: `stack.resize(1024)`
of a 24-byte Value is 24576 bytes, just over the 16 KB threshold that
decides which pool an allocation lands in.
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

## The panel

The ES3C28P carries a 2.8 inch ILI9341V, 240 by 320, on FSPI. `SCREEN`
starts it; after that the drawing verbs are the RP2350 ones and the
desktop ones, so a program that draws reads the same everywhere.

    SCLK 12   MOSI 11   MISO 13   CS 10   DC 46   backlight 45

There is no reset pin. It hangs on the chip's EN, which a watchdog reset
does not pull, so the panel keeps whatever state it was left in across a
warm restart. Only unplugging the board resets it, and command 0x01 is
the only reset the driver has.

Every primitive writes a framebuffer in PSRAM and `SCREENFLIP` sends it.
`SCREENFLIP y, rows` sends only that band, which is what a game wants.

Measured on the board, 40 MHz panel clock: a whole frame is 150 KB and
costs 32 ms, so 31 frames a second if everything moves. Drawing costs
almost nothing next to that - a filled circle 0.044 ms, clearing the
whole frame 4.4 ms. Twenty sprites drawn and only their 40-row band
sent is 7 ms, so a game that pushes what changed is not waiting on the
panel.

Two things had to be right for that. The clock had been left at 10 MHz
from the bringup, on a wrong diagnosis: the black screen was chip
select, not the clock, and the lower clock was never re-tested after the
real cause was found. And 40 MHz alone changed nothing, because the
frame goes out as fifteen bands and the code waited for each one - at
the default 100 Hz FreeRTOS tick a wait costs up to 10 ms, which is the
150 ms a frame that was measured. The tick is 1000 Hz now and there are
two staging buffers, so a band is filled while the previous is on the
wire.
The RP2350 draws straight into display RAM because 264 KB has no room
for a frame; 320 by 240 at two bytes is 150 KB, which 8 MB does have.

The bytes go out through `esp_lcd`'s SPI panel-IO layer rather than
through `spi_master` directly. That matters more than it sounds: the
layer carries the data/command line inside the transaction and owns chip
select, and it reports what a hand-rolled transport reports as success.
A frame cannot go out in one piece either, because a PSRAM buffer makes
the SPI driver ask for a bounce buffer of the same size in internal
memory, and 150 KB of that does not exist here. So the frame goes as
bands through a small internal buffer: the first as memory write, the
rest as memory-write-continue.

Three settings that only the panel in front of you can decide:

- Inversion is **on**. On this IPS panel, with it off, black comes out
  white and every colour is its complement.
- The colour order is **BGR**. Sending red without that bit gives blue.
- The orientation bit puts the long edge across, so it is 320 by 240.

`GFX.DIAG` answers with the transfers attempted, the transfers refused
and the last reason.

Reading back works, and it took measuring rather than reasoning. The
panel does answer over MISO - `GFX.PANELSTATE` returns the power mode
from 0x0A, and a live display reports 0x9C: booster on, sleep out,
normal mode, display on. What it does not answer is its identity: the ID
registers at 0x04 and 0xD3 read back as zeros on this panel, so a check
against the datasheet 0x00 0x93 0x41 fails on perfectly good hardware.
That is what the first attempt was checking, and why it looked dead.

`GFX.READBACK x, y` reads one pixel out of the panel's own memory. The
answer is one dummy byte and then six bits a channel, which five known
screens settled: red gives 0, 252, 0, 0 and blue gives 0, 0, 0, 252, so
the dummy is real and the order is plain RGB whatever the BGR bit in
MADCTL says. It agrees with `GFX.PEEK` within the quantisation between
the framebuffer's 5-6-5 and the panel's 6-6-6 - 248 against 252 on the
red and blue channels, exact on green.

`GFX.PANELREG cmd, n` and `GFX.PANELREGAT cmd, x, y, n` hand back the raw
bytes of any read command. They are what turned this from guesswork into
a table.

## The console and the editor

`GFX.CONSOLE 1` turns the panel into a text console: 40 columns by 30
rows of the 8x8 font, the grid a jdBasic listing assumes. Everything
printed goes to both the serial line and the glass - prompt, `DIR`,
errors, `PRINT` - because the tee sits under `stdout` as a small VFS
file rather than in the individual calls. `GFX.CONSOLE 0` gives the
panel back to a program that wants to draw on it.

A character marks its text row dirty and the flush sends only those
eight pixel rows, 5 KB rather than the 150 KB a whole frame costs. The
escape subset is the one an editor needs: absolute cursor, clear screen,
clear to end of line, cursor right, colours.

`EDIT name` opens the full-screen editor, Ctrl-S writes, Ctrl-Q leaves.
F1 (or Ctrl-H on a board's own keyboard) lists the keys: Shift with an
arrow selects, Ctrl-A selects all, Ctrl-C, Ctrl-X and Ctrl-V copy, cut
and paste, Ctrl-F finds text, Ctrl-G the next place it occurs, Ctrl-T
replaces, Ctrl-L goes to a line, Ctrl-Z takes the last change back.
Ctrl-D duplicates the line, Ctrl-K deletes it, Tab and Shift-Tab indent
and outdent a line or a selection, Enter keeps the indentation. Ctrl
with an arrow jumps a word, Ctrl-Home and Ctrl-End go to the ends of
the file. Ctrl-R writes the file and runs it; afterwards any key opens
the editor again, on the line an error named. A line wider than the
page scrolls the whole page sideways, and a row that goes on past the
edge ends in a marker.
It is `../../common/jdb_editor.cpp`, shared by every board: it speaks
plain ANSI and asks the port how big the page is, so one file serves a
40 by 40 PicoCalc, a 40 by 30 panel and a terminal. Arrow
keys arrive as a terminal's escape sequences and fold into the same
codes the PicoCalc's keyboard controller sends, so the editor cannot
tell the two apart.

There is still no keyboard on this board. The keys come over the serial
line, which means the editor works in a terminal today and will work on
the panel the moment there is something to type on.

## Touch

An FT6336G at 0x38 on I2C, with its own reset and interrupt lines:
SDA 16, SCL 15, INT 17, RST 18. It is the one part on this board that
confirms itself - `TOUCH.ID` answers chip 0x64, vendor 0x11, which is
what a FocalTech controller must say.

`TOUCH` gives `[count, x, y]` in screen coordinates, `TOUCH.RAW` the
controller's own numbers. The controller reports in the panel's native
portrait frame while the screen is turned to landscape, so its Y is the
screen's X and its X runs backwards to the screen's Y. That mapping is
not read off the MADCTL bits, it is measured: four corners touched, all
four consistent.

    links oben   raw 199, 32   -> 32, 40
    rechts oben  raw 228, 302  -> 302, 11
    links unten  raw  36, 35   -> 35, 203
    rechts unten raw  38, 295  -> 295, 201

The interrupt line never fired across 900 samples while touches were
reported correctly, so the driver polls. Good enough for an on-screen
keyboard; worth revisiting for gestures.

`fs/touch.jdb` is a finger-painting program: colour patches along the
top, canvas below, hold the bottom left corner to clear.

## Sound

An ES8311 codec at 0x18 on the same I2C bus as the touch, fed over I2S,
into an amplifier whose enable line is GPIO 1, low to enable.

    MCLK 4   BCLK 5   LRCK 7   codec-in 8   codec-out 6   enable 1

`BEEP freq, ms`, `TONE freq`, `TONE 0` or `PLAY.STOP` to stop, and
`PLAY.VOLUME pct`, and `PLAY` with a score in the classic BASIC
notation - A-G with # + or -, O for the octave, < and > to step it, L
for the length, T for the tempo, P or R for a rest, . to dot a note.
The names are the RP2350's, so a program that beeps reads the same on
either board, and `fs/tune.jdb` plays one.

The score engine is `../../common/jdb_play.cpp`, shared rather than
copied. Of its two hundred lines exactly four things belonged to the
Pico SDK - the timer that ends a note, its cancel, and a critical
section - so those became `jdb_snd_timer_start`, `jdb_snd_timer_cancel`,
`jdb_snd_lock` and `jdb_snd_unlock`, which each board fills in. Here
that is a one-shot esp_timer and a spinlock; there it is an alarm and
the interrupt mask. The parser, the note ring and the frequency
arithmetic are the same file on both.

Three things this cost, all of them worth writing down.

The board's I2S nets are named **from the codec's point of view**.
`I2S_DO` is the codec's output, so it is the ESP32's input; `I2S_DI` is
the codec's input, so the ESP32 sends on it. The manual's wording ("bit
output data signal") does not say whose output, and reasoning about it is
a coin toss - three minutes of measuring settles it.

The two ESP-IDF I2C drivers cannot coexist in one binary; it aborts at
boot with "CONFLICT! driver_ng is not allowed to be used with this old
driver". jdBasic's I2C verbs use `i2c_master`, and the vendor's ES8311
driver was written against the old one, so the driver is vendored in
`es8311_vendor.c` with its two I2C calls pointed at the board bus. Its
clock coefficient table is untouched, which is the point of vendoring
rather than retyping.

The enable line is asserted on every note rather than once at startup.
It is an ordinary GPIO: anything that writes it - a program, a probe -
would otherwise leave the board silently muted until the next reset.
That is not a hypothetical; it happened during bringup and sent the
search after the wrong suspect.

## The microphone

The same codec the other way round, on the codec's own data-out line
(IO6), reading through a second I2S channel on the same port. The codec
is a slave and takes its clocks from the transmit side, which runs
continuously, so the microphone has a clock whether or not anything is
playing.

`MIC ms` answers `[peak, mean]` over that window, both 0 to 100. Peak
says whether something happened, mean how loud it is now - a clap
detector wants the first, a level meter the second. `MIC.GAIN step`
takes 0 to 7, six decibels apart.

Two things it needed. The read timeout has to be generous: 200 ms was
not enough to get the first block out of a freshly enabled channel, and
the failure looks exactly like a dead microphone. And the first block
after enabling holds whatever the DMA buffer held before, which reads as
one loud spike out of silence, so it is drawn and thrown away.

The board can check this one on its own, which is worth more than it
sounds after a weekend of asking a human what he heard:

    silence     peak 2   mean 0
    TONE 880    peak 81  mean 48
    silence     peak 1   mean 0

`fs/vu.jdb` is a level meter that sounds a tone every few seconds, so it
proves itself while you watch it.

## The card

SDIO with four data lines, not SPI with one: CLK 38, CMD 40, D0 to D3 on
39, 41, 48 and 47. `SD.MOUNT` answers the size in megabytes, `SD.INFO`
gives name, megabytes and the bus width the card actually negotiated,
and `SD.UNMOUNT` lets go.

It mounts at `/sd`. The flash store owns the empty prefix and is the
fallback for every path that matches no other mount, so a bare name
still means flash and only a path that says `/sd` reaches the card.
Everything else follows from that: `COPY hello.jdb /sd/hello.jdb`,
`DIR "/sd"`, `RUN "/sd/hello.jdb"`.

A card that will not mount is not formatted. `format_if_mount_failed` is
off, because a card that refuses is a card to look at, not one to erase.

Measured on a 4 GB card: 3770 MB, four lines negotiated.

## The radio, scanning

`WIFI.SCAN [ms]` looks without joining: one row a network, `[name$,
dBm, channel, open]`, sorted by the radio with the strongest first.
Hidden networks come back with an empty name rather than being left out.
The radio has to be in station mode to scan, so a scan after a
`WIFI.CONNECT` drops the connection.

`fs/wifiscan.jdb` is the analyser: an arc a network, height for signal
and place for channel, the strongest on each channel named, and the
banner naming the quietest channels. The noise figure is the one the
Arduino analyser uses - each network adds the square of its margin above
the floor to its own channel and the four either side, because 2.4 GHz
channels overlap that far, which is why the quietest channel is rarely
the emptiest.
