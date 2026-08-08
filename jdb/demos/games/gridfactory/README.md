# gridfactory - a probe, not a game

One question: does a factory simulated as **whole-grid array operations**
carry a full picture of belts, junctions and machines inside a 30 fps budget,
when it also has to be drawn?

Nothing here is a game. It is a belt layout, a mover, a painter and a
stopwatch.

```
jdbasic gridfactory.jdb --check       the mover against a scalar walk
jdbasic gridfactory.jdb --bench       the timing sweep
jdbasic gridfactory.jdb --shot        one picture to tmp/gridfactory.png
jdbasic gridfactory.jdb --size 128    live view
```

## The answer

Yes, at 128x128. Measured in the plain interpreter with everything drawn:

| Gitter | Zellen | Waren | sim | malen | PLOTRAW | gesamt | fps |
|---|---|---|---|---|---|---|---|
| 64x64 | 4 096 | 386 | 4.96 | 0.87 | 0.57 | 6.4 | 156 |
| 128x128 | 16 384 | 1 793 | 19.21 | 3.00 | 2.50 | 24.7 | 40 |
| 256x256 | 65 536 | 7 754 | 77.64 | 12.65 | 8.98 | 99.3 | 10 |
| 512x512 | 262 144 | 31 889 | 315.75 | 50.83 | 43.48 | 410.1 | 2 |

Milliseconds per frame. 30 fps is a 33.3 ms budget.

16 384 cells with 1 793 goods in flight at 40 fps is a real factory region,
and it costs nothing per good: the tick has no idea how many there are.

Drawing is not the wall. Painting plus PLOTRAW is 21 ms of the 99 at 256x256,
one fifth. The simulation is the whole cost.

### The cost law

About **30 ns per cell per whole-grid pass**, and a tick is about **40
passes**. That is the only number needed to size a layout: cells times 40
times 30 ns.

## What the mover does

Direction 1 north, 2 east, 3 south, 4 west, one value per cell. Goods are a
second grid, tier as the value. A tick reads all four directions against **one
snapshot** of occupancy, so a good that turns a corner cannot be picked up a
second time by a later direction in the same tick. A claim grid keeps two
goods off one cell where belts merge.

`--check` proves it: a single good on a serpentine with three corners lands on
exactly the cell a plain scalar walk reaches after 60 ticks, and a completely
packed belt loses and duplicates nothing over 30 ticks.

## Open: the lurch

With snapshot occupancy a packed belt drains one cell every two ticks. A real
belt moves in lockstep: when the head steps forward, the whole queue behind it
does too. Getting that needs the free cell to propagate backwards along the
run inside one tick, which is a scan, not a pass. Either a bounded number of
extra passes or a runtime primitive. Nothing in this probe depends on it.

## Runtime findings

Three things in the array layer return a wrong answer instead of an error.
All three were found by building this.

* `ROTATE(array, shift_vector)` honours **axis 0 only**. `ROTATE(m, [0, 1])`
  returns the matrix unchanged rather than turning the columns. A column turn
  has to go through `TRANSPOSE(ROTATE(TRANSPOSE(m), [k, 0]))`.
* `SHIFT(array, shift_vector, fill)` is wrong for 2D. `SHIFT(3x3, [1, 0], 0)`
  shifts the flat buffer by one element instead of moving a row.
* Gather by index matrix silently broadcasts. With `pal = [100, 200, 300]`,
  `pal[matrix]` returns `pal[0]` in every cell instead of gathering, and does
  not raise.

The transpose detour costs about 13 ms of the 78 ms tick at 256x256, so fixing
the first one buys roughly 17 percent. The native compiler is the larger
lever and is untested here: this build has no NATIVEC.

## Layout

A serpentine. Belt rows every 8 rows running in alternating directions, joined
at the ends by a riser, machines every 12 cells, sources every 48, and one
sink at the far end so the line keeps flowing instead of freezing solid. It is
one connected path over the whole grid, which is the densest a belt layout
gets and therefore the honest worst case.
