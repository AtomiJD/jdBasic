jdBasic on the ES3C28P, the 2.8 inch ESP32-S3 display board.

TYPE readme.txt shows this.  DIR lists the store.  RUN name starts a
program, EDIT name opens it, LIST name prints it.  A name without an
extension gets .jdb added if that is what exists.

  selftest   every part of the board, each checked against something
             other than its own opinion - start here
  panel      the screen: bars, circles, text
  console    the screen as 40 by 30 characters, with colours
  touch      finger painting, colours along the top
  bounce     panel, sound and touch at once
  tune       PLAY with the classic note notation
  vu         a level meter off the microphone
  sdcard     the card at /sd next to the flash store
  wifiscan   what is on the air, drawn per channel
  hello      the smallest thing that proves the board runs
  primes     a sieve
  bench      three loops, timed
  mem        what the heap has left
  mandel     the set, drawn in text
  pins       the GPIO verbs
  hotspot    the radio as an access point

The panel is 320 by 240.  SCREEN starts it, every primitive writes a
framebuffer in PSRAM, and SCREENFLIP puts it on the glass.  Drawing
verbs are the desktop ones: DRAWCOLOR, PSET, LINE, RECT, CIRCLE, TEXT,
GFX.CLEAR.

GFX.CONSOLE 1 turns the panel into a text console; everything printed
then goes to both the glass and the serial line.  GFX.CONSOLE 0 gives
the panel back to a program that wants to draw.

TOUCH answers [count, x, y] in screen coordinates.  BEEP, TONE and PLAY
make sound; MIC ms answers [peak, mean].  SD.MOUNT puts the card at /sd.
WIFI.SCAN gives a row a network: [name$, dBm, channel, open].

SCREENFLIP sends the whole frame, 150 KB, 32 ms.  SCREENFLIP y, rows
sends only that band, which is what a game wants: twenty sprites drawn
and a 40-row band sent is 7 ms, so the panel is not the limit.

The board has no keyboard.  Keys come over USB, which is why EDIT works
from a terminal today.
