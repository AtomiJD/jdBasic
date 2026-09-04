# Embedded ports: consistency plan

What the three boards (Fruit Jam, PicoCalc, ESP32-S3 / ES3C28P) still need
before the article and the video can claim "one program, every board".
Ordered by what the reader or viewer sees first.

Status 2026-09-04. Head e52834d.

## 1. Network builtins on the ESP32

`HTTP.GET$`, `HTTP.STATUS` and `NTP.SYNC` exist only in the pico tree
(`pico_wifi.cpp` and `fruitjam_wifi.cpp`). NET CLOCK therefore does not
run on the ESP32. ESP-IDF ships `esp_http_client` and SNTP.

- [ ] `NTP.SYNC` in `esp32/main/esp32_wifi.cpp`
- [ ] `HTTP.GET$` and `HTTP.STATUS` there too, plain http first
- [ ] `RUN clock.jdb` on the ES3C28P shows the time and the weather

Also missing on the ESP32: `HTTP.POST$`, `WIFI.DNS$`. Missing on the pico:
`WIFI.AP`, `WIFI.CLIENTS`, `SYS.MEM`. Lower priority.

## 2. Key builtins on the ESP32

`KEY.GET` and `KEY.NOW` are registered in `pico_builtins.cpp` only. On the
ESP32 they read the serial line until there is a keyboard.

- [ ] `KEY.GET` (blocking) and `KEY.NOW` (-1 if none) in `esp32_builtins.cpp`
- [ ] document `GFX.KEYSTATE`, `KBD.LAYOUT`, `JOY.*` as Fruit Jam only

## 3. UTF-8 console on every board

The glyph table (ä ö ü Ä Ö Ü ß § ° ´ €) and the UTF-8 decoder live in
`fruitjam_con.c`. The PicoCalc console (`picocalc_lcd.c`) and the panel
console (`es3c28p_con.c`) draw bytes, so an umlaut in a PRINT is two
wrong glyphs there. Editor and REPL in `common/` already work in code
points.

- [ ] move the extra glyphs and the decoder to `common/` (next to `jdb_utf8.h`)
- [ ] use them in all three consoles
- [ ] `PRINT "Grüße"` looks the same on all three screens

## 4. One clock, one analyser

`pico/demos/netclock.jdb` (the p-code demo) and `pico/demos/clock.jdb`
(NET CLOCK) sit side by side. The header of netclock still says loading
the source takes the best part of a minute, which the load-time work
made false. `wifiscan.jdb` exists twice, in `pico/demos` and `esp32/fs`,
as different files.

- [ ] retire `netclock.jdb`, or keep it only as the p-code size example with a corrected header
- [ ] one `wifiscan.jdb` that runs unchanged on the Fruit Jam and the ESP32
- [ ] one `clock.jdb` that runs unchanged on both (needs 1 and 2)

## 5. Demo index

`pico/demos/README.md` is about jdPlot only. There is no list of the
30 demos and no note on which board runs which.

- [ ] a short index at the top of `pico/demos/README.md`: name, one line, board matrix (FJ / PC / ESP)
- [ ] `esp32/fs/readme.txt` points to it

## 6. Load-time and memory numbers for all boards

The measurements from the memory work (load 883 to ~150 ms, peak
308 KB to 128 KB on bbs.jdb) are Fruit Jam numbers. The same `-lt`
trace on the PicoCalc and the ESP32 gives the article one table.

- [ ] PicoCalc trace (also open on trakr #323)
- [ ] ESP32 trace
- [ ] one table: board, prompt free, bbs.jdb load ms, peak

## 7. Same boot line everywhere

The pico has a welcome page (d10bcd1). Check the ESP32 prints the same
line: version, board name, free heap. Note in the docs that the German
layout on the Fruit Jam comes from `boot.jdb` on the card (AUTORUN), not
from the image.

- [ ] compare the three boot screens side by side
- [ ] `boot.jdb` and AUTORUN in the Fruit Jam section of `pico/README.md`

## 8. Sound

The Fruit Jam speaker is silent under CircuitPython too, so hardware.
Headphones for the video, or open the case. Start volume low on every
board.

- [ ] check the default of `PLAY.VOLUME` on all three boards
- [ ] `SND.OUT` default documented

## 9. Instrument builtins

The pico registers about 40 measurement builtins (`DVI.*`, `USB.*`,
`PSRAM.*`, `SND.PROBE`, `FS.TEST`, `ESP.*`), the ESP32 has
`GFX.PANELREG` and friends. They stay in the image and out of the docs.
They do show up in `SYS.NATIVES`, so decide before quoting a native
count in the article whether the instruments are included.

## 10. One build command per board

`pico/` holds six Fruit Jam build directories. The README names exactly
one command as the image, presumably `./build_pico.sh fruitjam usb psram`.

- [ ] `pico/README.md`: the one command per board
- [ ] `esp32/README.md`: the one command (`./build.sh psram`)

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
