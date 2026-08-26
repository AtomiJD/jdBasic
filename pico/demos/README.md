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

### 3. Load them, once per power-on

Paste the contents of `LOAD.txt` at the prompt:

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

## On the desktop

The same `jdm.jdb` runs unsplit:

    ./build/jdBasic.exe pico/demos/jdm.jdb

It opens a 320x320 window at `PWSCALE` magnification (2 by default) instead of
drawing into display RAM. Everything else behaves the same, which makes the
desktop the cheap place to get a chart right before sending it over.

`jdmini.jdb` in this directory is the cut-down version: one SUB, a frame, a
title, the range extremes and the curve, 670 bytes stripped. It loads in one
piece and needs none of the part-splitting above.
