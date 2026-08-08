# The 128 game - design

A plan built on the probe next door, not on hopes. Every mechanic below is
quoted in **whole-grid passes**, because that is the only currency this engine
has.

## The budget, first

The probe measured **15 ns per cell per pass**. At 128x128 one pass is
**0.25 ms**.

Sim rate and frame rate are decoupled: the rules run at **20 Hz**, the picture
at 60. That gives the tick **50 ms**, which is **about 200 passes**. The probe
tick spends 80. Roughly 120 passes are left to buy mechanics with.

| what | passes | where the number comes from |
|---|---|---|
| belts, the mover | 56 | measured, `passcost.jdb` |
| machines and ports | 32 | measured, same |
| mining and depletion | 8 | estimate |
| ownership flood | 12 | estimate, a dilate along own build |
| pressure diffusion | 24 | estimate, two CONVOLVE passes plus masking |
| decay on flipped cells | 10 | estimate |
| **sum** | **142** | 63 left of 205 |

Painting is charged per **frame**, not per tick, and measures 14 passes. At 60
frames against 20 ticks that is the larger bill, and still not the problem.

That table is the design document. A mechanic that does not fit does not go
in, and nobody has to argue about it. `passcost.jdb` regenerates the measured
rows against a reference pass it times itself, so the table cannot quietly go
stale as the rules grow.

## The core

**One grid, shared by everyone.** Not four private boards with a scoreboard.
The factory is not a thing you build next to the fight, it *is* the fight.

Three sentences:

1. Goods delivered to your port are the score.
2. Ore is finite and richest in the middle, so the factory has to grow towards
   the enemy.
3. Where two networks touch, the border moves towards whoever is delivering
   more, and the loser's buildings on flipped cells decay.

That third one is the whole design. The front is resolved by **throughput, not
damage**. Your production rate is literally your army. Nobody is ever wiped
out by one lucky strike, and optimising your layout is the aggressive move,
which is the thing a factory player wants to be rewarded for.

### Ownership and pressure, concretely

Two grids on top of the probe's state:

* `owner` - 0 none, 1..4 a player. Spreads from your port along cells you have
  built on. A dilate masked by "is built and not enemy owned".
* `press` - one signed field, team A positive, team B negative. Your port
  emits at a rate proportional to your **current delivery rate**, measured over
  the last few seconds. It diffuses with a 3x3 CONVOLVE, conducted well by your
  own belts and badly by open ground.

A cell flips to the team whose pressure is larger. A flipped cell stops
carrying for its old owner and its building loses integrity; when integrity
hits zero the cell clears. So a border moves as a slow tide, never as a
deletion.

Two consequences worth having:

* Long thin belt runs into contested ground are cheap to build and easy to
  lose. Thick blocks of your own construction conduct pressure and hold.
  Geometry becomes a real decision.
* A player who stops producing starts losing ground automatically. There is no
  turtling.

## 2v2

This is where the shape of the map earns its keep.

```
  +---------------------------+
  |  P1                   P2  |     team A along the north edge
  |                           |
  |          the middle       |     richest ore, everyone's problem
  |                           |
  |  P3                   P4  |     team B along the south edge
  +---------------------------+
```

Teammates sit **adjacent, not diagonal**, so a team owns an edge and the front
is the horizontal middle. Familiar, readable, and it gives each team a
backfield.

The part that makes it a real 2v2 rather than two solo games sharing a
scoreboard:

**The recipe chain crosses the team.** Each corner carries a different ore
mix. Tier 3 needs an input only your partner's corner produces. To reach tier
3 at all, the two of you have to physically **connect your networks along your
own edge**. That connection is the team's spine.

From this one rule the whole 2v2 dynamic falls out:

* Cooperation is spatial and visible, not a chat agreement.
* Cooperating creates an attack surface that did not exist before. The enemy
  team now has a strategic objective that is not "hit their base".
* The two teammates specialise naturally, because their ore differs, and they
  have to talk about who widens and who deepens.
* A team that never connects is stuck on tier 2 and will lose on score, so the
  risk is not optional.

Attacking the spine is not a combat action. You push pressure at it, which
means you build towards it and out-deliver them locally. Defending it means
thickening your own construction there instead of expanding. That is a real
resource decision made in belts.

### The other counts

* **1v1** - same map, two opposite corners, the other two are neutral rich
  fields that either side may take.
* **Solo** - the pressure comes from a creep that spreads out of the middle and
  is only pushed back by delivery rate. Same maths, no second player. Cheap,
  because the field already exists, and it is where new players learn.
* **2v2v2** is not planned. Three teams on a square map has no good geometry.

## Not getting boring

The failure mode of every builder is the solved layout. Five defences, in
order of how much they actually do:

1. **The opponent edits the map.** With one shared grid no layout is optimal
   twice. This is built into the core and it is worth more than the other four
   together.
2. **Ore depletes.** A patch runs dry, so a base that stops moving starves.
   No "build once, then idle".
3. **Recipe draft.** Each team drafts 4 of about 12 recipes before the match,
   the same way a deck is drafted in jdVOID. Different recipe sets want
   genuinely different layouts, and it is the cheapest replay value on this
   list because it is data, not code.
4. **Tiers.** Tier 3 scores several times tier 1 but needs a longer chain, so
   the standing mid-game question is widen or deepen, and both are correct at
   different moments.
5. **Events** - a solar storm every 90 s slows belts in the open but not
   covered ones. Optional garnish. Only if the other four are not enough.

## How I would build it

The order matters more than the content, and it is the order jdVOID taught us.

**Rules module first, headless, deterministic.** `factory.jdb` as an
EXPORT MODULE with JSON-serialisable state, exactly like `arena.jdb`. Every
mechanic gets its assertion before it gets a pixel.

**Commands, not state.** A match is a seed plus an ordered command log. The
grid is far too big to ship per frame, but "belt at (r,c) facing east" is
nothing. jdVOID already proves the pattern replays identically.

**Server shape as it stands.** `server.jdb` catches the sim up lazily on every
request, no background thread, rooms, PIN login. Reuse it whole.

**Painter separate from rules**, the way `art.jdb` is separate.

**Balance protocol from the first number.** It paid for itself in jdVOID and
it costs nothing to start.

### Phases, each with a gate

| | | gate |
|---|---|---|
| **P0** | probe promoted to a module, tests, pass table | `--check` runs as a suite, the pass table exists and is honest |
| **P1** | ore, mining, recipes, port, score | a scripted layout delivers a known count in a known time, asserted |
| **P2** | building: place, remove, **drag a run** | a command log replays identically |
| **P3** | ownership, pressure, front, decay | inside budget, and a scripted duel produces a deterministic border |
| **P4** | rooms, 1v1 over the net, spectate | two clients, one match, identical end state |
| **P5** | 2v2, crossed chains, the spine, team score | a full 2v2 played end to end |
| **P6** | recipe draft, ore variety, balance protocol | a second match feels different from the first |

**The real gate is after P2.** That is when there is a factory you can build
and watch run, and that is when we find out whether it is fun. Everything from
P3 on is only worth building if the answer was yes. Do not build the network
layer before the toy is fun.

## Risks, named

* **The lurch.** From the probe: a packed belt drains one cell every two ticks
  instead of moving in lockstep. Players will build dense lines and will see
  it. This has to be decided in P1, not discovered in P5.
* **128 is fixed.** It comes out of the budget, not out of taste. The design
  has to make it feel big, which it can, because most cells stay empty ground
  and the interesting part is the middle.
* **Input on a grid is tedious.** Placing belts one cell at a time is not a
  game. Drag-to-draw is a P2 requirement, not polish.
* **No NATIVEC in this build.** Every number here is an interpreter number. If
  the native compiler lands, everything gets easier. Do not plan on it.
* **Pressure can feel arbitrary** if a player cannot see why the border moved.
  The field has to be visible on the map from the first day it exists, not
  added later as a debug overlay.
