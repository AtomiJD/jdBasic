# jdVEIN

A factory on a 128 by 128 grid, simulated as whole-grid array operations. The
design is in `DESIGN.md`, the measurement it rests on is the probe next door
in `gridfactory/`.

**P0 to P2 are done.** The rules are a module with a suite, what a tick costs
is a number a tool prints, the economy runs from the seam to the port, and a
match is a command log that replays to the same numbers.

```
jdbasic vein_test.jdb                    the assertions
jdbasic passcost.jdb                     what a tick costs, in grid passes
jdbasic view.jdb --log bauplan.json      replay a recorded factory and watch it
jdbasic view.jdb WERK --secs 30          run a layout for half a minute
jdbasic view.jdb WERK --shot             one picture to tmp/jdvein.png
```

Every tool finds its data next to its own source, so it does not matter which
directory it was started from. The rules run at **20 ticks a second** whatever
the frame rate is, which is the split the design rests on.

## The parts, in jdVOID's register

Anyone who plays jdVOID should recognise the shelf. **RAFFINERIE** is the same
name as the card, doing the same job one floor down: it turns something raw
into something worth more.

| | | |
|---|---|---|
| **REGOLITH** | the grey stuff scraped off the rock | worth 1 |
| **IRIDIT** | the rarer seam, it glows | worth 2 |
| **BARREN** | REGOLITH, smelted | worth 5 |
| **LINSE** | IRIDIT, refined | worth 6 |
| **ZUENDKERN** | BARREN and LINSE, coupled | worth 16 |

| | |
|---|---|
| **BAHN** | the belt. In orbit everything runs on one |
| **SCHUERFER** | works a REGOLITH seam |
| **RESONATOR** | works an IRIDIT seam |
| **SCHMELZE** | 2 REGOLITH into 1 BARREN |
| **RAFFINERIE** | 2 IRIDIT into 1 LINSE |
| **KOPPLER** | 1 BARREN and 1 LINSE into 1 ZUENDKERN |

The KOPPLER is where the 2v2 lives: it is the only machine that wants two
different branches, so it is the only reason two players have to connect their
networks.

Renaming the whole game was **one edit to `parts.json` and one to `maps.json`**
and not a line of rules. That was the point of P0. Figures in the jdVOID style
come later: on the map a machine is one cell, so art pays off in the build
palette and the part sheet, not on the grid.

## Nothing about the game is in the code

* `parts.json` - goods, belts, machines. A machine is a recipe: `IN`, `NEED`,
  optionally `IN2` and `NEED2`, then `OUT` and `DWELL`. A machine with `MINES`
  instead of inputs is a drill. Nothing else separates the two.
* `maps.json` - layouts, either generated (`GEN`) or drawn (`STROKES`):
  `RUN`, `CELL`, `MACH`, `ORE`, `SRC`, `SINK`.
* `bauplan.json` - a recorded match. Layout, a fingerprint of the rules it was
  played under, and the commands.

Adding a machine kind **costs the tick nothing**: the recipe is baked into
per-cell grids when it is built, so the price is the same whether the game has
one kind or forty.

## A match is a list, not a grid

Nothing outside the module reaches into a state. Everything a player does
arrives as a command, which is what makes a match shippable: the grid is far
too big to send, `{"T":"DRAG","R":6,"C":6,"R2":6,"C2":34}` is nothing.

```
{ "T": "PLACE", "P": 1, "R": 10, "C": 5, "K": "SCHMELZE", "D": "E" }
{ "T": "DRAG",  "P": 1, "R": 10, "C": 5, "R2": 10, "C2": 30 }
{ "T": "CLEAR", "P": 1, "R": 10, "C": 5 }
{ "T": "TICK",  "N": 40 }
```

A drag lays a run and turns the corner where it has to, because placing a belt
one cell at a time is not a game. `ACT` does a command and writes it down in
one move, so a log cannot fall out of step with the state it describes, and a
log carries a fingerprint of the parts table it was played under: replaying a
recording against changed rules says so instead of reporting a false result.

## The rules the economy follows

* A **drill only works the seam it was built for**, and a seam is finite. A
  SCHUERFER on IRIDIT digs nothing, quietly and forever.
* A machine **takes its inputs off the belt and holds them**, capped at twice
  its recipe.
* A **full machine lets goods ride past.** Deliberate: a machine must not be
  able to deadlock the line it stands on. The consequence shows at the port,
  where raw REGOLITH turns up behind a SCHMELZE that could not keep up, which
  is why the port books what it received **by good** and not just by crate.
* A recipe with two inputs **produces nothing at all** until both are there.

## What a tick costs

Measured, not counted. `passcost.jdb` times its own reference pass twice and
throws the first away, because a short reference makes the whole table wobble.

| Gitter | 1 Durchlauf | Baender | Maschinen + Haefen | Malen | Tick |
|---|---|---|---|---|---|
| 64x64 | 0.059 ms | 3.3 ms / 56 | 4.8 / 80 | 2.1 / 35 | 8.2 / 137 |
| 128x128 | 0.249 ms | 13.2 ms / 53 | 17.0 / 68 | 6.8 / 27 | 30.3 / 121 |
| 256x256 | 0.920 ms | 51.9 ms / 56 | 78.5 / 85 | 26.2 / 28 | 130.5 / 141 |

At 20 Hz a tick on 128x128 has about **199 passes** and spends 121. The front
in P3 is budgeted at 46, which fits with 32 to spare. That is not much, and it
is better to know now.

## What is checked

`vein_test.jdb`, 50 assertions across thirteen sections. The load-bearing ones:

* the mover lands a good on exactly the cell a plain scalar walk reaches after
  60 ticks over three corners
* a completely packed belt loses and duplicates nothing over 30 ticks
* four REGOLITH make exactly two BARREN, with nothing left on the board and
  nothing left in a buffer
* twelve in the ground, twelve dug, twelve in the port, and an empty seam
  stays empty
* the wrong drill on the wrong seam digs nothing
* a two-input chain delivers, and the same chain with one branch missing makes
  no ZUENDKERN
* **the gate for P2**: a whole factory built by nine commands and nothing
  else, and the log replays to the same fingerprint. A log missing its last
  move has to land somewhere different, or the check would prove nothing.
* a state written out as JSON and read back runs on identically

## Two silent failures, now loud

Both were found by starting a tool from the repo root instead of from here,
and both drew a plausible window rather than saying anything.

* Data files were read by plain name, so from another directory they were not
  found and the module fell back to its **built-in set**. That set has two
  machines and one layout. `LOADDATA` now resolves next to the running script
  and returns a message rather than falling back.
* `NEWGAME` on a layout that does not exist returned an empty 8 by 8 board
  instead of complaining, and the build commands then ran off the edge of it.
  Two cells fit. That is what the two squares were. It sets `ERR` now, and a
  `REPLAY` whose commands do not all land sets `ERR` too, because a recording
  that only half applies is not the match it recorded.

## Traps found while building this

* **`CD` is a builtin** (`CD "path"`). A grid named `cd` reads fine and then
  fails on `cd[r][c] = 0` with an out-of-bounds that points at the index and
  not at the name. `--lint` does not catch it; `SHORT` and `PI` it does.
* `ROTATE` turns the row axis only and `SHIFT` is wrong for 2D, both written
  up in the probe's README. Column moves go through a transpose.

## Next

P3: ownership and pressure. The front between two networks, resolved by
throughput rather than damage. Gate: inside budget, and a scripted duel
produces a deterministic border.

Before that is worth building, though, the honest question from the design
still stands: **P2 is the phase after which we find out whether this is fun.**
