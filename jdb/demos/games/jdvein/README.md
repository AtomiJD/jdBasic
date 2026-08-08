# jdVEIN

A factory on a 128 by 128 grid, simulated as whole-grid array operations. The
design is in `DESIGN.md`, the measurement it rests on is the probe next door
in `gridfactory/`.

**P0 is done.** The rules are a module, they have a suite, and what a tick
costs is a number a tool prints rather than a claim in a document.

```
jdbasic vein_test.jdb            the assertions
jdbasic passcost.jdb             what a tick costs, in grid passes
jdbasic view.jdb SCHLANGE        watch a layout run
jdbasic view.jdb KETTE --shot    one picture to tmp/jdvein.png
```

## Nothing about the game is in the code

That is the point of P0, and it is what the admin and designer pages will
stand on later.

* `parts.json` - goods, belts, machines. A machine is a recipe: `IN`, `OUT`,
  `DWELL`, plus a colour and a cost. Adding a machine kind is a JSON edit and
  **costs the tick nothing**: the recipe is baked into per-cell grids when it
  is built, so the tick is the same price whether the game has one machine
  kind or forty.
* `maps.json` - layouts, either generated (`GEN`) or drawn (`STROKES`).
  A stroke is what an editor emits: a straight `RUN`, a `CELL`, a `MACH`, a
  `SRC`, a `SINK`. Both paths go through the same `PLACE`, so a hand-drawn map
  and a generated one cannot drift apart.

The test suite proves the data really drives the rules: it edits the dwell
time of a furnace in the parts table and asserts the line delivers sooner.
Nothing in `vein.jdb` knows what a furnace is.

## What a tick costs

Measured, not counted:

| Gitter | 1 Durchlauf | Baender | Maschinen + Haefen | Malen | Tick |
|---|---|---|---|---|---|
| 64x64 | 0.060 ms | 3.3 ms / 56 | 2.7 / 45 | 1.0 / 16 | 6.1 / 101 |
| 128x128 | 0.229 ms | 12.9 ms / 56 | 7.5 / 32 | 3.3 / 14 | 20.5 / 89 |
| 256x256 | 0.855 ms | 50.3 ms / 58 | 44.5 / 52 | 13.1 / 15 | 94.9 / 110 |

The mover is **56 passes at every size**, which is what a fixed-cost tick
looks like. At 20 Hz a tick on 128x128 has about **205 passes** to spend and
currently spends 89.

## What is checked

`vein_test.jdb`, 16 assertions:

* the mover lands the good on exactly the cell a plain scalar walk reaches
  after 60 ticks over three corners
* a completely packed belt loses and duplicates nothing over 30 ticks
* the recipe chain end to end: ore, furnace, ingot, press, part, port, and the
  score booked is the top tier's
* a line under load keeps delivering, and only top tier arrives
* building and unbuilding, including the four refusals
* the data drives the rules
* a state written out as JSON and read back runs on identically

## Two traps found while building this

* **`CD` is a builtin** (`CD "path"`). A grid named `cd` reads fine and then
  fails on `cd[r][c] = 0` with an out-of-bounds, which points at the index and
  not at the name. `--lint` does not catch it.
* `ROTATE` turns the row axis only and `SHIFT` is wrong for 2D, both written
  up in the probe's README. Column moves go through a transpose.

## Next

P1: ore, mining and depletion, real recipe chains, the port economy. Gate: a
scripted layout delivers a known count in a known time, asserted.
