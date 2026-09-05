# Embedded ports: consistency plan

What the three boards (Fruit Jam, PicoCalc, ESP32-S3 / ES3C28P) still need
before the article and the video can claim "one program, every board".
Ordered by what the reader or viewer sees first.

Status 2026-09-05: points 1 to 7 done, see the notes under each.

## 1. Network builtins on the ESP32

`HTTP.GET$`, `HTTP.STATUS` and `NTP.SYNC` exist only in the pico tree
(`pico_wifi.cpp` and `fruitjam_wifi.cpp`). NET CLOCK therefore does not
run on the ESP32. ESP-IDF ships `esp_http_client` and SNTP.

- [x] `NTP.SYNC` in `esp32/main/esp32_wifi.cpp` (SNTP once, then taken down, clock set to local)
- [x] `HTTP.GET$`, `HTTP.POST$`, `HTTP.STATUS` over esp_http_client, https through the certificate bundle
- [x] `RUN clock.jdb` on the ES3C28P shows the time and the weather (weather line 7 s after RUN, 1.5 s when the radio is already up)

Found on the way: https failed every signature check until the
hardware bignum unit was switched off (`CONFIG_MBEDTLS_HARDWARE_MPI=n`);
the radio ran out of internal RAM when a large program was parsed, so
the interpreter now lives in PSRAM (`ALWAYSINTERNAL=0`), and a join no
longer restarts a radio that is already on the network.

Also missing on the ESP32: `HTTP.POST$`, `WIFI.DNS$`. Missing on the pico:
`WIFI.AP`, `WIFI.CLIENTS`, `SYS.MEM`. Lower priority.

## 2. Key builtins on the ESP32

`KEY.GET` and `KEY.NOW` are registered in `pico_builtins.cpp` only. On the
ESP32 they read the serial line until there is a keyboard.

- [x] `KEY.GET` (blocking) and `KEY.NOW` (-1 if none) in `esp32_builtins.cpp`
- [x] the demo index lists `GFX.KEYSTATE`, `KBD.LAYOUT`, `JOY.*` programs under the Fruit Jam only

## 3. UTF-8 console on every board

The glyph table (ä ö ü Ä Ö Ü ß § ° ´ €) and the UTF-8 decoder live in
`fruitjam_con.c`. The PicoCalc console (`picocalc_lcd.c`) and the panel
console (`es3c28p_con.c`) draw bytes, so an umlaut in a PRINT is two
wrong glyphs there. Editor and REPL in `common/` already work in code
points.

- [x] `common/jdb_glyphs.h`: the glyph table, the decoder, the cell byte scheme
- [x] used by `fruitjam_con.c`, `picocalc_lcd.c`, `es3c28p_con.c`
- [x] `PRINT "Grüße: Ärger Öl Übung 5° §3 12€"` printed on the PicoCalc and the ESP32 panel

## 4. One clock, one analyser

`pico/demos/netclock.jdb` (the p-code demo) and `pico/demos/clock.jdb`
(NET CLOCK) sit side by side. The header of netclock still says loading
the source takes the best part of a minute, which the load-time work
made false. `wifiscan.jdb` exists twice, in `pico/demos` and `esp32/fs`,
as different files.

- [x] `netclock.jdb` retired; `doc/languages.md` points at `bbs.jdb` for the p-code example
- [x] one `wifiscan.jdb`, q over `KEY.NOW`, run on both boards
- [x] one `clock.jdb`, keys over `KEY.NOW`, `SCREENFLIP` per pass, run on both boards

## 5. Demo index

`pico/demos/README.md` is about jdPlot only. There is no list of the
30 demos and no note on which board runs which.

- [x] the index with the board matrix at the top of `pico/demos/README.md`
- [x] `esp32/fs/readme.txt` points to it

## 6. Load-time and memory numbers for all boards

The measurements from the memory work (load 883 to ~150 ms, peak
308 KB to 128 KB on bbs.jdb) are Fruit Jam numbers. The same `-lt`
trace on the PicoCalc and the ESP32 gives the article one table.

- [x] the load-trace build now exists for the PicoCalc too (`build_pico.sh loadtrace`)
- [x] measured 2026-09-05, bbs.jdb (8294 bytes with a PRINT in front), host clock from RUN to the first line:

    board      free at prompt   load ms   trace ms   peak
    Fruit Jam        88632        156       129       128 KB in the PSRAM arena, +20 KB heap
    PicoCalc        211568        139       121       +148 KB heap (80 -> 229 KB used)
    ESP32       184 KB internal   219        -         everything in PSRAM

  The PicoCalc cannot load the 19 KB clock as source (std::bad_alloc);
  as p-code from the desktop it runs (the loader walked the code with a
  width table that lacked MARK_CONST, fixed with #328; GFX.CONSOLE now
  exists on the PicoCalc too).

## 7. Same boot line everywhere

The pico has a welcome page (d10bcd1). Check the ESP32 prints the same
line: version, board name, free heap. Note in the docs that the German
layout on the Fruit Jam comes from `boot.jdb` on the card (AUTORUN), not
from the image.

- [x] both welcome pages: `jdBasic 1.0   on <board>`, built date, chip at MHz, ram, board lines, store
- [x] AUTORUN, the ESC window and `boot.jdb` in `pico/README.md`

## 8. Sound

The Fruit Jam speaker is silent under CircuitPython too, so hardware.
Headphones for the video, or open the case. Start volume low on every
board.

- [x] start volumes: Fruit Jam 20, PicoCalc buzzer 60, ESP32 codec 50 (was 70); in `pico/README.md`
- [x] `SND.OUT` default (speaker) and the jack documented there too

## 9. Instrument builtins

The pico registers about 40 measurement builtins (`DVI.*`, `USB.*`,
`PSRAM.*`, `SND.PROBE`, `FS.TEST`, `ESP.*`), the ESP32 has
`GFX.PANELREG` and friends. They stay in the image and out of the docs.

Decided 2026-09-05: the article counts verbs, not instruments. Measured
with `SYS.NATIVES` and the source lists:

    board       SYS.NATIVES   instruments   verbs
    Fruit Jam        420           47        373
    ESP32            383           10        373
    PicoCalc         368           16        352   (from the source lists)

Instruments are the `DVI.*`, `USB.*`, `PSRAM.*`, `ESP.*`, `LCD.*`
families, the `FS.*`/`SD.*`/`SND.*` probes, `*.DIAG$`, `*.RAW$`,
`GFX.PANEL*`, `GFX.READBACK`, `TOUCH.ID`, `TOUCH.RAW`, `SYS.HEAP$`,
`SYS.CHUNKS`, `SYS.RESET$` and `SYS.NATIVES` itself.

## 10. One build command per board

`pico/` holds six Fruit Jam build directories. The README names exactly
one command as the image, presumably `./build_pico.sh fruitjam usb psram`.

- [x] `pico/README.md`: one command per board, the trace variants, the 1200-baud way to BOOTSEL
- [x] `esp32/README.md`: `./build.sh usbconsole` is the display board's image

## Order

1 and 2 first, then the clock runs on every board and the video has its
point. Then 3 and 4. Then 5 and 6 for the article text. 7 to 10 are
polish.

## Note: higher DVI resolution on the Fruit Jam

Not part of this plan; kept here because it came up.

The signal is already 640x480 at 60 Hz. The framebuffer is 320x240 in
RGB332, each pixel stored twice (stride 640) and each stored line sent
twice by the command list, 150 KB in SRAM. Going to a real 640x480:

- 300 KB of framebuffer, which SRAM does not have. It has to live in
  PSRAM behind the line-copy path that the `fbps` build already uses,
  at twice the stride: 640 bytes per 31.7 us line, about 20 MB/s of
  PSRAM reads next to the program.
- every drawing verb and the console draw through the uncached PSRAM
  alias, so drawing is slower per pixel and there are four times as many
  pixels; the console becomes 80x60 with an 8x8 font.
- every demo assumes 320x240, so it must be a mode (`SCREEN 640, 480`)
  with 320x240 staying the default.
- the DVI list was stable for one day. The copy chain changes again.

Anything above 640x480 needs a faster HSTX clock than the 126 MHz
system clock gives, so 800x600 or 720p means overclocking and a second
round of jitter work. Estimate for a 640x480 mode: two days plus a
day of soak; not before the article.
