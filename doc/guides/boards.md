# jdBasic on a microcontroller

[Docs home](../README.md) · [Tour](../tour.md) · [Language reference](../languages.md) · [Module library](../../lib/README.md)

jdBasic runs on two microcontroller families: the RP2350 (a bare Pico 2, a PicoCalc, an Adafruit Fruit Jam) and the ESP32-S3 (a DevKitC, the 2.8 inch ES3C28P). The board runs the same interpreter as the desktop, so arrays, lambdas, maps and strings all work at its prompt. On top of that it adds verbs for pins, buses, timers, sound and the radio.

This guide takes you from a flashed board to a prompt, a saved program, a blinking LED, a sensor reading and a picture. The full list of board verbs is in the reference section [On a board: RP2350 and ESP32-S3](../languages.md#on-a-board-rp2350-and-esp32-s3).

Most examples here are **board only**: they call verbs the desktop build does not have, so on a PC they stop with `Undefined function`. Plain language examples run anywhere.

## 1. The boards

| | bare Pico 2 | PicoCalc | Fruit Jam | ESP32-S3 DevKitC | ES3C28P |
|---|---|---|---|---|---|
| chip | RP2350 | RP2350 | RP2350B | ESP32-S3 | ESP32-S3 |
| console | USB serial | 320x320 panel, 40x40 text | DVI 320x240, 40x30 text | USB or UART serial | 320x240 panel, 40x30 text, and USB serial |
| keyboard | the terminal | its own | USB host | the terminal | the terminal, or an M5Stack CardKB on the I2C connector |
| sound | none | buzzer | TLV320 codec, speaker or jack | none | ES8311 codec, speaker and microphone |
| storage | flash store | flash store and SD | flash store and SD | flash store | flash store and SD |
| radio | on a W part | on a W part (Pico 2 W) | ESP32-C6 beside it | always, also as access point | always, also as access point |
| extras | PIO | `LCD.*` probes, PIO | buttons, NeoPixels, IR, PIO | | touch screen |

Free memory at a bare prompt, from the reference: 351720 bytes on a PicoCalc, 209912 on a Fruit Jam (plus 8 MB of PSRAM), 229615 on an ESP32-S3 (plus 8 MB of PSRAM). The table [What differs](../languages.md#what-differs) lists the other differences between the two families.

## 2. Getting the firmware on the board

The repository holds the sources and the build scripts. The READMEs describe building the image yourself; follow them for every detail.

### RP2350

[embedded/pico/README.md](../../embedded/pico/README.md) builds under WSL or Linux with pico-sdk 2.3.0 and Arm GNU 14.2. One command per board:

```
./build_pico.sh                   # PicoCalc            -> build/
./build_pico.sh fruitjam usb      # Fruit Jam           -> build-adafruit_fruit_jam-nocalc-usb/
./build_pico.sh pico2 nocalc      # a bare Pico 2       -> build-pico2-nocalc/
```

To flash, hold BOOTSEL while you plug the board in and drag the uf2 onto the RP2350 drive. A board that already runs jdBasic reboots into BOOTSEL when its serial port is opened at 1200 baud, so you need the button only once.

Coming from the stock PicoCalc MicroPython firmware: its partition table makes the flash store fail. The section "Coming from MicroPython" in the README shows how to check it with `FS.ATRANS()` and clear it with `FS.NUKEPT("ERASE")`, which cannot be undone.

### ESP32-S3

[embedded/esp32/README.md](../../embedded/esp32/README.md) builds with ESP-IDF v5.5:

```
./build.sh usbconsole   the ES3C28P display board: PSRAM, console on USB
./build.sh psram        an S3R8 DevKit, console on UART0
./build.sh nopsram      an S3FN8 part, 512 KB SRAM alone
```

Flash with esptool. The README's command for a DevKitC on its native USB port (here with the `nopsram` build directory):

```
esptool --chip esp32s3 --port COMn --baud 921600 \
    --before usb-reset --after no-reset \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB \
    0x0 build-nopsram/bootloader/bootloader.bin \
    0x8000 build-nopsram/partition_table/partition-table.bin \
    0x10000 build-nopsram/jdbasic_esp32.bin
```

Check that the output says "Hash of data verified" three times. For the `usbconsole` build (the ES3C28P) use `--before default-reset --after hard-reset` instead. The example programs in `embedded/esp32/fs/` become the flash store through `storage.bin`: flash it once at `0x410000` and then leave it alone, because reflashing it resets the store.

## 3. The first session

### Connecting

On a bare Pico 2 or an ESP32-S3 DevKitC, open the board's serial port in any terminal program. On the RP2350 any baud rate works. On a PicoCalc, a Fruit Jam with monitor and USB keyboard, or an ES3C28P, the prompt is also on the board's own screen; the serial port stays a second console.

The prompt keeps state between lines, as on the desktop:

```
> DIM a = IOTA(10)
> PRINT SUM(a * a)
385
```

`HELP` lists the topics of the on-board manual, `HELP topic` shows one, written for forty columns. **Ctrl-C** ends a running program in any state with `Break at line N`.

### Writing and saving a program

On a board the program is a file in the flash store. These commands work on it:

| Command | What it does |
|---------|--------------|
| `NEW name` | creates an empty file and opens it in the editor |
| `EDIT name` | opens a file in the full-screen editor |
| `RUN name` | loads and runs a program; `RUN hello` finds `hello.jdb` |
| `LOAD name` | makes it the current program, so a bare `RUN`, `LIST` or `EDIT` uses it |
| `LIST`, `LIST name` | shows a file with line numbers and syntax colour |
| `SAVE name` | copies the current program under a new name, which becomes the current one |
| `DIR`, `TYPE`, `DEL`, `COPY`, `REN`, `MD`, `RD` | the files in the store |

In the editor, Ctrl-S saves, Ctrl-Q leaves and Ctrl-R saves, runs and returns. F1 lists the other keys (on a CardKB, Fn then Q leaves the editor). The editor speaks plain ANSI, so it works on the board's screen and in a serial terminal.

A first program that runs on the desktop and on any board:

```basic
DIM a = IOTA(10)
PRINT "squares: "; a * a
PRINT "sum:     "; SUM(a * a)
PRINT "even:    "; FILTER(LAMBDA x -> x MOD 2 = 0, a)
```
```text
squares: [1, 4, 9, 16, 25, 36, 49, 64, 81, 100]
sum:     385
even:    [2, 4, 6, 8, 10]
```

Type `NEW first`, enter the four lines, press Ctrl-S and Ctrl-Q, then `RUN first`.

### Sending a file from the desktop

Typing a long program into the editor is slow. `RECV name` at the prompt takes a file off the serial line without parsing or echo. Send it in small chunks (128 bytes every 30 ms works) and end with a single `0x04` byte; any line ending is fine. `RECV name bytes` takes exactly that many raw bytes, for binary files such as p-code, and the board answers `#` for every 256 bytes stored.

### A program at power-on

`AUTORUN name` starts a program at every power-on, `AUTORUN OFF` clears it and a bare `AUTORUN` reports the setting. ESC in the first two seconds of a boot skips it, so a broken program never locks you out.

## 4. Pins

The pin verbs are the same on both families. Each is marked "Boards only" in the reference; the full list is in [Common to every board](../languages.md#common-to-every-board).

| Verb | Arguments |
|------|-----------|
| `GPIO.MODE(pin, is_output)` | 1 output, 0 input |
| `GPIO.WRITE(pin, level)`, `GPIO.READ(pin)` | drive or read a pin |
| `GPIO.PULLUP(pin [, on])` | internal pull-up on (default) or off |
| `GPIO.WATCH(pin, edge)` | raises the `"PIN"` event: 1 rising, 2 falling, 3 both, 0 stops |
| `ADC.READ(pin)` | one raw conversion: the channel on the RP2350, the pin (GPIO 1 to 10) on the ESP32-S3 |
| `ADC.TEMP` | the chip's own temperature in degrees Celsius |
| `PWM.SET(pin, hz [, duty_percent])`, `PWM.OFF(pin)` | a square wave, duty 50 by default |
| `TIMER.EVERY(ms)`, `TIMER.STOP` | raises the `"TICK"` event every ms milliseconds |
| `KEY.WATCH(on)` | raises the `"KEY"` event for each key |

On the ESP32-S3, GPIO 26 to 32 (flash), 33 to 37 (PSRAM) and 43 and 44 (console) are refused by name, because writing one takes the board down. `PIN.FREE()` lists the pins you may use.

### Blink

On an RP2350 board, `LED(on)` switches the board's own LED (on a W board it hangs off the radio chip, so the radio must be started). Board only:

```basic
FOR i = 1 TO 10
    LED(1)
    SLEEP 250
    LED(0)
    SLEEP 250
NEXT i
```

An LED on a pin of your own works on both families. Set `ledpin` to the pin your LED is on; GP2 is the output pin the demo `evpin.jdb` drives. Board only:

```basic
DIM ledpin = 2      ' the pin your LED is on
GPIO.MODE(ledpin, 1)
FOR i = 1 TO 10
    GPIO.WRITE(ledpin, 1)
    SLEEP 250
    GPIO.WRITE(ledpin, 0)
    SLEEP 250
NEXT i
```

### Read a sensor

Every board has a temperature sensor in the chip. This program samples it once a second through the timer event and appends each reading to `log.csv` in the flash store, the shape of the demo `jdlog.jdb`. Board only:

```basic
DIM samples = 0

SUB OnTick(d)
    DIM c = ADC.TEMP()
    samples = samples + 1
    PRINT samples; ": "; FORMAT$("{:.2f}", c); " degC"
    TXTWRITER("log.csv", STR$(samples) + "," + FORMAT$("{:.2f}", c) + CHR$(10), TRUE)
ENDSUB

ON "TICK" CALL OnTick
TIMER.EVERY(1000)
DO WHILE samples < 5
    SLEEP 100
LOOP
TIMER.STOP()
PRINT "log.csv has "; samples; " samples"
```

The handler runs between statements, never inside the interrupt, so it may print and write files. Handlers do not nest: a tick that arrives while one runs is dropped. `TYPE log.csv` at the prompt shows the result.

An analogue input is `ADC.READ`. On the ESP32-S3 the argument is the pin; `embedded/esp32/fs/pins.jdb` reads GPIO 4. On the RP2350 it is the ADC channel. Board only:

```basic
PRINT "GP4 reads "; ADC.READ(4)
```

### Pin events

`GPIO.WATCH` calls a handler on an edge; the handler receives `[pin, level]`. The demo `evpin.jdb` drives GP2 and watches GP3, with a wire between them. Board only:

```basic
DIM edges = 0

SUB OnPin(d)
    edges = edges + 1
    PRINT "pin "; d[0]; " is now "; d[1]
ENDSUB

ON "PIN" CALL OnPin
GPIO.MODE(2, 1)
GPIO.WATCH(3, 3)
FOR i = 1 TO 3
    GPIO.WRITE(2, 1)
    SLEEP 60
    GPIO.WRITE(2, 0)
    SLEEP 60
NEXT i
SLEEP 200
GPIO.WATCH(3, 0)
PRINT edges; " edges"
```

### PWM

A square wave for a buzzer, a servo or a dimmed LED. On a PicoCalc pin 26 is the speaker, which `hardware.jdb` sweeps. Board only:

```basic
FOR f = 400 TO 1200 STEP 100
    PWM.SET(26, f, 50)
    SLEEP 80
NEXT f
PWM.OFF(26)
```

### I2C and SPI

| Verb | Arguments |
|------|-----------|
| `I2C.SETUP(bus, sda, scl [, hz])` | opens bus 0 or 1 on two pins |
| `I2C.WRITE(bus, address, data [, hz])` | writes an array of bytes or a string, returns the count or -1 |
| `I2C.READ(bus, address, count [, hz])` | reads count bytes, an empty array on failure |
| `I2C.SCAN(bus)` | the addresses that answered |
| `SPI.SETUP(bus, sck, mosi, miso [, hz])` | opens an SPI bus; drive chip select yourself with `GPIO.WRITE` |
| `SPI.XFER(bus, data)` | full duplex, as many bytes back as went out |

On the ESP32-S3 the I2C speed belongs to the device, so you give `hz` to `I2C.WRITE` and `I2C.READ`. On the ES3C28P, bus 0 on GPIO 16 and 15 is the board's own bus (touch, codec and the four-pin connector): `I2C.SETUP(0, 16, 15)` adopts it, and bus 1 is free for pins of your choice. The connector gives 3.3 V.

A scan is the first check when you wire up a device. On a PicoCalc, bus 1 finds the keyboard controller at 31. Board only:

```basic
PRINT "i2c bus 1 devices: "; I2C.SCAN(1)
```

## 5. Screen, sound and radio

### The ES3C28P

`SCREEN 320, 240` starts the panel. The desktop drawing verbs write a framebuffer in PSRAM and `SCREENFLIP` puts it on the glass. `GFX.CONSOLE 0` gives the panel to your program, `GFX.CONSOLE 1` turns it back into the 40 by 30 text console. `TOUCH` answers `[count, x, y]`. Board only:

```basic
SCREEN 320, 240
GFX.CONSOLE 0
GFX.CLEAR 8, 12, 40
DRAWCOLOR 90, 220, 120
CIRCLE 160, 120, 40, 1
DRAWCOLOR 255, 255, 255
TEXT 16, 200, "jdBasic on ES3C28P", 1
SCREENFLIP
SLEEP 3000
GFX.CONSOLE 1
```

The card mounts with `SD.MOUNT` at `/sd`; a bare name still means the flash store (`COPY hello.jdb /sd/hello.jdb`). The microphone is `MIC ms`, answering `[peak, mean]`. See [The ES3C28P](../languages.md#the-es3c28p-28-inch-board).

### The PicoCalc

Drawing goes straight to the 320 by 320 panel as you draw. To animate without flicker, `GFX.BUFFER(x, y, w, h)` holds one rectangle in memory and `SCREENFLIP` sends only what changed. Check `SYS.LARGEST()` first: a whole screen is 51 KB. The keyboard sends 177 for ESC and 180 to 183 for the arrows. See [The PicoCalc](../languages.md#the-picocalc).

### The Fruit Jam

With a monitor on the DVI port and a USB keyboard it is a computer of its own. Drawing and the console share one 320 by 240 framebuffer, so a `PSET` is visible at once and `SCREENFLIP` does nothing; switch with `GFX.CONSOLE 0` and `GFX.CONSOLE 1` as above. The five LEDs are `NEOPIXEL.SET(i, r, g, b)` with `i` from 0 to 4, then `NEOPIXEL.SHOW`; the buttons are `BUTTON.GET(n)`. `KBD.LAYOUT("DE")` sets a German layout; put it in an `AUTORUN` program, since the board starts with US. Board only:

```basic
FOR p = 0 TO 4
    NEOPIXEL.SET(p, 0, 0, 80)
NEXT p
NEOPIXEL.SHOW()
```

See [The Fruit Jam](../languages.md#the-fruit-jam).

### Sound

`BEEP freq, ms`, `TONE freq` (`TONE 0` stops) and `PLAY score` work on every board with sound. `PLAY` plays in the background in the classic BASIC notation, and `PLAY.BUSY` says whether it still sounds. Sound starts quiet; `PLAY.VOLUME` takes 0 to 100. Board only:

```basic
PLAY.VOLUME 60
PLAY "T140 O4 CDEFGAB>C"
DO WHILE PLAY.BUSY
    SLEEP 50
LOOP
```

On the Fruit Jam `SND.OUT(1)` selects the speaker and `SND.OUT(0)` the headphone jack.

### Wi-Fi

A started radio takes about 100 KB of internal RAM, so turn it on for the job and off again with `WIFI.OFF`. `WIFI.CONNECT(ssid$, password$)` joins a network and answers 0 on success; `WIFI.AUTO()` takes the two lines of `wifi.txt` in the flash store (ssid, then password). `HTTP.GET$` and `HTTP.POST$` then fetch over http and https, and `NTP.SYNC` sets the clock. Board only:

```basic
IF WIFI.AUTO() <> 0 THEN
    PRINT "no connection"
ELSE
    PRINT "connected, ip "; WIFI.IP$()
    PRINT HTTP.GET$("http://example.com/")
ENDIF
```

The ESP32-S3 can also be its own access point with a web server on it; `embedded/esp32/fs/hotspot.jdb` is a complete example, and [The radio on the ESP32-S3](../languages.md#the-radio-on-the-esp32-s3) shows the short form.

## 6. Memory and limits

A board has a few hundred kilobytes, so memory is the limit you meet first. A `.jdb` file that runs on the desktop runs on a board until memory runs out.

| Function | Answers |
|----------|---------|
| `SYS.FREE` | the total free heap, spread over many pieces |
| `SYS.LARGEST` | the biggest single block, the number a growing array hits first |
| `SYS.STACK` | `[size, deepest use]` of the C stack |
| `SYS.MEM()` | ESP32-S3: internal RAM and PSRAM, free, total and largest, on one line |
| `SYS.HEAP$` | RP2350: the heap in detail |
| `SYS.DF`, `SYS.FREEDISK` | the flash store as a line, and as a number |

An array element costs about 24 bytes on both families: 8 MB of PSRAM holds roughly 340000, the 351720 bytes of a PicoCalc about 14000. Reducers such as `SUM` copy the array, so check `SYS.LARGEST` before a large allocation. Board only:

```basic
DIM n = 10000
PRINT SYS.FREE(); " free, largest block "; SYS.LARGEST()
IF SYS.LARGEST() > n * 24 * 2 THEN
    DIM a = IOTA(n)
    PRINT SUM(a)
ELSE
    PRINT "not enough room for "; n; " elements"
ENDIF
```

More numbers are in [Watching the memory](../languages.md#watching-the-memory). The stack, history and buffer sizes are fixed when the image is built; both READMEs list the build settings.

## 7. Compiled p-code, lessons and demos

### P-code

A board has no native compiler, but the desktop can compile a program to p-code for it:

```
jdbasic --pcode prog.jdb
```

This writes `prog.jdpb` beside the source. Send it with `RECV prog.jdpb bytes` and start it with `RUN prog.jdpb`. The board skips lexer, parser and compiler, so a program starts much sooner (on a Fruit Jam, 2.53 s from source against 0.17 s as p-code), but it does not use less RAM while it runs. A file from a different jdBasic build is refused at load. The working loop: write and test on the desktop, `--pcode`, copy, `RUN`. See [Compiled programs: p-code on disk](../languages.md#compiled-programs-p-code-on-disk).

### Lessons

The Train jdBasic video lessons are in a `lessons` folder on every board. `TYPE lessons/readme.txt` lists them and `RUN lessons/hello1` starts the first. Four lessons have a board edition (graphics, modules, HTTP and native). [embedded/lessons/README.md](../../embedded/lessons/README.md) describes the pack and how to put it on a board.

### Demos

[embedded/pico/demos/README.md](../../embedded/pico/demos/README.md) lists the board demos and which board each one runs on: games, a WiFi analyser, a web server, a clock, the jdPlot function plotter (`jdm.jdb`) and the temperature logger `jdlog.jdb` with its viewer. The ESP32-S3 copies are in `embedded/esp32/fs/`.

## 8. Where next

- [On a board: RP2350 and ESP32-S3](../languages.md#on-a-board-rp2350-and-esp32-s3): every board verb, per board.
- [The prompt and the flash store](../languages.md#the-prompt-and-the-flash-store): the full command set and the editor keys.
- [embedded/pico/README.md](../../embedded/pico/README.md) and [embedded/esp32/README.md](../../embedded/esp32/README.md): build settings, memory measurements and the hardware details of each board.
- [The Tour](../tour.md): the language itself, which the board runs unchanged.
