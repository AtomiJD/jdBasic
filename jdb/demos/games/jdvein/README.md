# jdVEIN

A factory on a 128 by 128 grid, simulated as whole-grid array operations. The
design is in `DESIGN.md`, the measurement it rests on is the probe next door
in `gridfactory/`.

**P0 and P1 are done.** The rules are a module with a suite, what a tick costs
is a number a tool prints, and the economy runs end to end: a seam in the
ground, a drill, recipes with two inputs, a port that books what it received.

```
jdbasic vein_test.jdb            the assertions
jdbasic passcost.jdb             what a tick costs, in grid passes
jdbasic view.jdb WERK            watch a layout run
jdbasic view.jdb WERK --shot     one picture to tmp/jdvein.png
```

## Nothing about the game is in the code

That is what the admin and designer pages will stand on.

* `parts.json` - goods, belts, machines. A machine is a recipe: `IN`, `NEED`,
  optionally `IN2` and `NEED2`, then `OUT` and `DWELL`. A machine with `MINES`
  instead of inputs is a drill. Nothing else separates the two.
* `maps.json` - layouts, either generated (`GEN`) or drawn (`STROKES`). A
  stroke is what an editor emits: `RUN`, `CELL`, `MACH`, `ORE`, `SRC`, `SINK`.
  Both paths go through the same `PLACE`, so a hand-drawn map and a generated
  one cannot drift apart.

Adding a machine kind **costs the tick nothing**: the recipe is baked into
per-cell grids when it is built, so the price is the same whether the game has
one kind or forty. The suite proves the data really drives the rules by
halving a dwell time in the parts table and asserting the line refines more.

## The rules the economy actually follows

* A **drill only works the seam it was built for**, and a seam is finite. Put
  an ore drill on crystal and it digs nothing, quietly and forever.
* A machine **takes its inputs off the belt and holds them**, capped at twice
  its recipe. It does not sit on a good the way a plain cell does.
* A **full machine lets goods ride past.** That is deliberate: a machine must
  not be able to deadlock the line it stands on. The consequence is visible at
  the port, where raw ore turns up behind a furnace that could not keep up,
  which is why the port books what it received by good and not just by crate.
* A recipe with two inputs **produces nothing at all** until both are there.
  That is the 2v2 rule from the design, and it has a unit test: the same works
  with one branch missing delivers no finished part however long it runs, and
  the press sits on a pile it cannot use.

## What a tick costs

Measured, not counted. `passcost.jdb` times its own reference pass, so the
table cannot go stale as the rules grow.

| Gitter | 1 Durchlauf | Baender | Maschinen + Haefen | Malen | Tick |
|---|---|---|---|---|---|
| 64x64 | 0.062 ms | 3.5 ms / 57 | 4.7 / 75 | 1.9 / 31 | 8.2 / 132 |
| 128x128 | 0.243 ms | 13.3 ms / 55 | 16.3 / 67 | 5.8 / 24 | 29.7 / 122 |
| 256x256 | 1.020 ms | 52.9 ms / 51 | 78.6 / 77 | 23.1 / 22 | 131.5 / 128 |

The mover is **about 55 passes at every size**, which is what a fixed-cost
tick looks like. At 20 Hz a tick on 128x128 has about **213 passes** to spend
and currently spends 122. The machine phase doubled from P0 to P1 when
buffers, second inputs and mining arrived, and that showed up the day it
landed rather than in P5.

## What is checked

`vein_test.jdb`, 32 assertions across ten sections. The ones that carry the
most weight:

* the mover lands a good on exactly the cell a plain scalar walk reaches after
  60 ticks over three corners
* a completely packed belt loses and duplicates nothing over 30 ticks
* four ore make exactly two ingots through a furnace that wants two, with
  nothing left on the board and nothing left in a buffer
* **the seam gate**: twelve in the ground, twelve dug, twelve in the port,
  nothing still travelling, and an empty seam stays empty
* the wrong drill on the wrong seam digs nothing
* a two-input chain delivers, and the same chain with one branch missing makes
  no finished part
* a state written out as JSON and read back runs on identically

## Two traps found while building this

* **`CD` is a builtin** (`CD "path"`). A grid named `cd` reads fine and then
  fails on `cd[r][c] = 0` with an out-of-bounds that points at the index and
  not at the name. `--lint` does not catch it.
* `ROTATE` turns the row axis only and `SHIFT` is wrong for 2D, both written
  up in the probe's README. Column moves go through a transpose.

## Next

P2: building as a player does it. Place, remove, and **drag a run**, driven by
a command log. Gate: a command log replays identically. That is also the phase
after which we find out whether any of this is fun.
