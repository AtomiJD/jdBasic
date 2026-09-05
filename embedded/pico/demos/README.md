# The demos, and where they run

One language, three boards: the Fruit Jam (FJ, 320x240 over DVI, USB
keyboard, sound), the PicoCalc (PC, 320x320 panel, its own keyboard) and
the ESP32-S3 display board (ESP, 320x240 panel, keys over the serial
line, touch). The drawing verbs, the console, the network verbs and the
file store read the same everywhere; what a program can use beyond that
is the board's own hardware.

`x` runs there, `o` uses only verbs that board has but has not been run
on it yet, blank means it needs something the board does not have. The
ESP32 copies live in `../../esp32/fs`, which becomes the flash store at
build time.

| demo | FJ | PC | ESP | what it is |
|---|---|---|---|---|
| basic.jdb | x | x | o | the language itself, in a dozen lines |
| maze.jdb | x | x | o | the C64 one-liner |
| petscii.jdb | x | o | o | the graphics half of the console font |
| keycode.jdb | o | x | o | what a key actually sends on this board |
| events.jdb | o | x | o | timer and key events between statements |
| evreent.jdb | o | x | o | handlers do not nest |
| evpin.jdb | o | x | o | pin edges reach a handler (wire GP2 to GP3) |
| hardware.jdb | o | x | o | ADC, PWM, I2C |
| jdlog.jdb | o | x | o | autonomous temperature logger, meant for AUTORUN |
| jdshow.jdb |  | x |  | draw what jdlog collected, on the plotter |
| jdm.jdb, jdmini.jdb, jdmload.jdb |  | x |  | jdPlot, the function plotter (below) |
| kreise.jdb | o | x | o | rings and rays |
| sprites.jdb | o | x | o | the same sprite on a desktop and on the panel |
| grafik.jdb | x | x |  | buffered drawing in a band |
| melodie.jdb | x | x | o | a tune, in one line |
| musik.jdb | x | x | o | the tune plays while the program works |
| funk.jdb | o | x | o | join the network, show the address, fetch a page |
| webserver.jdb | o | x | x | the board serves its own page |
| webtest.jdb | o | x | o | the board talks to its own server |
| bbs.jdb | x | o | o | jdBBS, the board's browser |
| wifiscan.jdb | x |  | x | the WiFi analyser, q leaves |
| clock.jdb | x | x | x | NET CLOCK: time, weather, five colour schemes; the PicoCalc runs it as p-code (`jdbasic --pcode`, see below) |
| mandel.jdb | x |  |  | Mandelbrot with the pad, ships as p-code (`.jdpb`) |
| bisect.jdb | x |  |  | which half of the pad is doing it |
| drift.jdb | x |  |  | STELLAR DRIFT, the game: pad, sound, waves, boss |
| boot.jdb | x |  |  | AUTORUN: the USB keyboard speaks German |
| hello.jdb | o | o | x | the board, its two memories and the store |
| primes.jdb | o | o | x | a sieve as a mask, no inner loop |
| mem.jdb | o | o | x | where an array lives, what an element costs |
| bench.jdb | o | o | x | three shapes of work, timed |
| console.jdb | o |  | x | the panel as a text console |
| panel.jdb | o | o | x | the panel, drawn with the common verbs |
| tune.jdb | o | o | x | the PLAY score notation |
| hotspot.jdb |  |  | x | the board as its own network with a page |
| pins.jdb |  |  | x | free pins, temperature, a square wave, a bus scan |
| sdcard.jdb |  | o | x | the card at /sd next to the flash store |
| touch.jdb |  |  | x | finger painting |
| vu.jdb |  |  | x | a level meter on the microphone |
| bounce.jdb |  |  | x | panel, codec and touch at once |
| selftest.jdb |  |  | x | every part of the board against something else |

Getting a file onto a board: `RECV name` at the prompt, then send the
file over the serial line (128 bytes every 30 ms, end with a single
0x04). The Fruit Jam also reads `.jdpb` files that `jdbasic --pcode`
wrote on a desktop, which start at once instead of being parsed.

---

# jdPlot - a function plotter for the PicoCalc

`jdm.jdb` turns the PicoCalc prompt into a plotting calculator. You load it
once, it defines its verbs in the running VM, and the prompt comes back. From
then on any array you build can be drawn:

    x = IOTA(64, 0) / 8
    PTITLE$ = "sin"
    PLOT SIN(x)

The library draws straight into display RAM. It never asks for a screen
buffer, because a whole 320x320 panel at four bits a pixel is 51 KB and the
board does not have that to spare.

The same file runs on the desktop, where it opens a window instead. `SYS.FREE()`
inside a TRY decides which host it is on.

---

## Getting it onto the board

The board cannot compile the whole library in one go. It throws `std::bad_alloc`
somewhere above 3 KB of source in a single chunk, and the limit is the shape of
the code rather than its size: the lexer's token vector and the syntax tree are
alive at the same time, and the heap cannot serve a block that large however
much of it is free. So the library ships as one file per small group of
functions.

### 1. Cut the library into parts

    cd pico/demos
    ./mkparts.sh

That writes `parts/` from `jdm.jdb`:

    plt1.jdb        1795 bytes   P_TRANS P_RANGE
    plt2.jdb         702 bytes   P_SERIES
    plt3.jdb        1577 bytes   the verbs
    plt4.jdb        1171 bytes   P_GRID
    plt5.jdb        1105 bytes   pens, ink, tick labels
    plt6.jdb         552 bytes   P_LEGEND
    plt7.jdb         334 bytes   P_FRAME
    plt8.jdb         907 bytes   P_CURVE
    pltinit.jdb      536 bytes   the defaults
    LOAD.txt                     the load order

Comments and blank lines are stripped: the lexer drops them anyway, and on the
board they are the difference between fitting and not.

### 2. Send each file over the wire

At the prompt, `RECV <name>` takes a file straight off the serial line and ends
on Ctrl-D. It parses nothing and echoes nothing, so a program arrives at the
speed of the link - much faster than typing into the editor, which redraws a
whole recoloured line per keystroke.

    > RECV plt1.jdb
    receiving plt1.jdb, end with Ctrl-D
    <paste or send the file, then Ctrl-D>
    1795 bytes

Repeat for all nine. Two things matter when a terminal sends the bytes for you:

- Send in small chunks with a short pause between them. `RECV` gives up after
  about three seconds of silence once data has started, so keep the gaps well
  under that. 128 bytes every 30 ms works.
- Finish with a single `0x04`. The byte count it prints back is the check.

Any line ending is fine, `RECV` normalises to `\n`.

### 3. Load them

`mkparts.sh` also writes `jdm_boot.jdb`, which is the same nine loads as one
303-byte program:

    > RUN jdm_boot.jdb
    jdPlot ready

That is the short way, and it is what an unattended board uses. `EXECUTE` was
not an option for this until recently: it keeps the outer chunk, the source
string and the tokens alive at once, and used to fail where a plain `RUN` of
the same file succeeded. It became usable when the load peak came down.

Or paste the contents of `LOAD.txt` at the prompt, one line at a time:

    RUN plt1.jdb
    RUN plt3.jdb
    RUN plt4.jdb
    RUN plt5.jdb
    RUN plt8.jdb
    RUN plt2.jdb
    RUN plt6.jdb
    RUN pltinit.jdb
    RUN plt7.jdb

`jdPlot ready` means it is in. The order is not cosmetic: it is by size,
largest first, because the whole heap is only available to the first load and
every load leaves it more broken up. Each part declares the globals its own
functions read inside a branch that never runs, so no part can wipe what
another already set and the order is free to be chosen for memory alone.

The files stay in flash. After a power cycle you re-run the nine lines; you do
not re-send them.

---

## Driving it from the prompt

    PLOT ys                 a new chart, x is 0..n-1
    PLOTXY xs, ys           a new chart with explicit x
    PLOTADD ys              another series on the same chart
    PLOTADDXY xs, ys
    PLOTR                   redraw with the current settings
    PLOTSTYLE n, style$, col
    PLOTNAME n, name$       a name switches the legend on
    PLOTHELP                the list, on the device

Series are numbered from 1. Styles are `line`, `dot`, `bar` and `step`. Colours
are 0 to 7, picked to land on the panel's sixteen-entry palette: green, cyan,
yellow, orange, magenta, red, white, blue.

The settings are plain globals. Assign one and call `PLOTR`; there is no setter
to remember.

| | |
|---|---|
| `PTITLE$` | chart title |
| `PXLAB$` `PYLAB$` | axis captions, the y one drawn upright |
| `PGRID` | grid on (1) or off (0) |
| `PLEG` | legend on (1) or off (0) |
| `PBG` | black (0) or white (1) |
| `PLOGX` `PLOGY` | logarithmic axis |
| `PAUTO` | autoscale (1), or 0 with `PXMIN` `PXMAX` `PYMIN` `PYMAX` |

One syntax note: a literal array wants the call in parentheses, because a bare
verb followed by a bracket reads as an index.

    PLOT([3, 7, 4, 9])      correct
    PLOT [3, 7, 4, 9]       "Cannot index into NONE"

---

## Five functions

All five run on the board with the library loaded. Type them at the prompt, or
send them as a file and `RUN` it.

### 1. Damped oscillation

    DIM t = IOTA(120, 0) / 10
    PTITLE$ = "damped oscillation"
    PXLAB$ = "t"
    PYLAB$ = "y"
    PLOTXY t, SIN(t * 2) * EXP(0 - t / 4)

### 2. A parabola against a cubic

Two series on one chart, each named so the legend appears.

    DIM x = (IOTA(81, 0) - 40) / 10
    PTITLE$ = "parabola and cubic"
    PXLAB$ = "x"
    PYLAB$ = "f(x)"
    PLOTXY x, x * x
    PLOTADDXY x, x * x * x
    PLOTNAME 1, "x2"
    PLOTNAME 2, "x3"

### 3. Exponential decay on a log axis

A decay is a straight line once the y axis is logarithmic, which is the whole
point of having one.

    DIM n = IOTA(50, 0)
    PTITLE$ = "exponential decay"
    PXLAB$ = "half lives"
    PYLAB$ = "counts"
    PLOGY = 1
    PLOTXY n, 10000 * (0.75 ^ n)
    PLOTNAME 1, "N(t)"
    PLOTSTYLE 1, "dot", 5

Set `PLOGY = 0` again before the next chart, or it stays logarithmic.

### 4. The gaussian

    DIM g = (IOTA(101, 0) - 50) / 12.5
    PTITLE$ = "gaussian"
    PXLAB$ = "sigma"
    PYLAB$ = "density"
    PLOTXY g, EXP(0 - (g * g) / 2) / SQR(2 * PI)
    PLOTNAME 1, "phi"

### 5. A Lissajous figure

Parametric: both coordinates come from the same parameter, which is what
`PLOTXY` is for.

    DIM u = IOTA(300, 0) / 47.75
    PTITLE$ = "lissajous 3:2"
    PXLAB$ = ""
    PYLAB$ = ""
    PLEG = 0
    PLOTXY SIN(u * 3), SIN(u * 2)

---

## What it costs

Measured on a PicoCalc, freshly powered:

| | free |
|---|---|
| bare prompt | 120376 |
| library loaded, all nine parts | 87000 |
| after a 300-point two-coordinate plot | 61568 |

**About 300 points per series is the working budget.** 400 fails with
`MIN: std::bad_alloc` while `SYS.FREE()` still reports 56 KB, because what runs
out is one contiguous block rather than the total. `SYS.LARGEST()` reports the
largest block the heap will actually hand over, and that is the number to watch.

A plot is not animation. Every shape goes straight to the panel as it is drawn,
so you watch the curve appear. `SCREENFLIP` is a no-op with no buffer open.

---

## Leaving it running

`AUTORUN <name>` records a program to start at power-on. The board then needs
no terminal at all: it comes up, runs it, and whatever that program leaves in
the VM is still there afterwards. Two seconds of ESC at boot cancels, so a
misbehaving autorun never locks the board out.

`jdlog.jdb` in this directory is the shape of it. Set it once:

    > AUTORUN jdlog.jdb
    autorun: jdlog.jdb

**It does not load the plotter.** Nobody is watching the panel while the board
is away, and the library costs 33 KB and several seconds of loading that the
logging job has no use for. The logger samples the board's own temperature
sensor and appends to `log.csv` on the flash store. The first sample is taken
before the timer starts, so the screen says something within a second of
power-on instead of after the first interval.

Looking at it is a separate job, for when you are back:

    > RUN jdmload.jdb        the plotter, as one program
    jdPlot ready
    > RUN jdshow.jdb         draws the tail of log.csv
    240 samples drawn

That split is the whole point. Collecting has to survive being alone for days;
drawing only has to work while someone is looking.

`AUTORUN OFF` clears it, `AUTORUN` on its own reports what is set. Two seconds
of ESC at boot cancels a run, which is the way back in if a program misbehaves.

### Stopping it

Any of ESC, Ctrl-C or `q` stops the logger.

**The PicoCalc's ESC key sends 177**, not the 27 a serial terminal sends. The
keyboard controller has its own block up there: the arrows are 180 to 183, HOME
is 210, END is 213. A handler that only tests for 27 works perfectly over the
wire and never fires on the device, which is a good way to lose an afternoon.

`keycode.jdb` prints what any key actually sends. Run it, press the key, then
hold any key to finish:

    > RUN keycode.jdb
    press keys - hold one to finish
    code 177

The pieces that make a program like this work unattended:

- **`TIMER.EVERY(ms)` with an `ON "TICK"` handler.** Handlers run between
  statements, never inside the interrupt, so a handler may draw and write
  files. They do not nest: a tick arriving while one is still running is
  dropped rather than queued, so a slow handler cannot build a backlog it will
  never work off.
- **Take the first sample before starting the timer.** Otherwise nothing
  happens for a whole interval, and on a minute timer that reads as a board
  that has hung.
- **Write to flash as you go.** A power cut then costs nothing that was
  already sampled.
- **`KEY.WATCH(TRUE)` with an `ON "KEY"` handler**, so there is a way back in.
- **A rolling window if you keep data in memory.** `TAKE(0 - n, arr)` keeps
  the tail; without it the array grows until the heap gives out. `jdshow.jdb`
  does this when it reads the file back, which is why the logger itself does
  not have to.

---

## On the desktop

The same `jdm.jdb` runs unsplit:

    ./build/jdBasic.exe pico/demos/jdm.jdb

It opens a 320x320 window at `PWSCALE` magnification (2 by default) instead of
drawing into display RAM. Everything else behaves the same, which makes the
desktop the cheap place to get a chart right before sending it over.

`jdmini.jdb` in this directory is the cut-down version: one SUB, a frame, a
title, the range extremes and the curve, 670 bytes stripped. It loads in one
piece and needs none of the part-splitting above.
