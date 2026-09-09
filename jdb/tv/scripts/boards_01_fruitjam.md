---
title: jdBasic on the Fruit Jam - a BASIC computer on an RP2350B
hook: Editor, pinball, a network clock and a BBS, all on the board
tags: jdBasic, basic, embedded, rp2350, fruit jam, adafruit, retro-computing
---

jdBasic running on an Adafruit Fruit Jam. A monitor on the DVI socket, a USB keyboard, and the same interpreter as on the desktop runs on the RP2350B.

## In the video

- the board's editor with the Commodore maze one-liner, Control R saves and runs
- NET CLOCK: time from NTP, weather and forecast from Open-Meteo, five colour schemes
- PINBALL: pop bumpers, drop targets, spinner, slingshots, kickback, charged plunger, flippers on the shift keys, the BOOT button as plunger
- Control C ends a program wherever it is, the prompt comes back with "Break at line N"
- STELLAR DRIFT: a shooter with a USB pad, waves, a boss every fifth wave, power-ups, high score in the flash store
- jdBBS: a browser for a markdown board, forty columns, colour per character cell

The sound of the games goes out of the headphone jack. The capture only took the picture, so the video is silent apart from the narration.

## The boards

The same port runs on a PicoCalc (RP2350, 320 by 320 panel) and on an ESP32-S3 board with a 2.8 inch panel. Programs, the editor, the lessons and the hardware verbs are the same on all three.

## Links

- Source, the boards are under embedded/: https://github.com/AtomiJD/jdBasic
- The language, with a chapter on the boards: https://github.com/AtomiJD/jdBasic/blob/main/doc/languages.md
- Train jdBasic, the lessons: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
