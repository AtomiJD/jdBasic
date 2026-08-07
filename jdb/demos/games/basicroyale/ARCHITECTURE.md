# jdVOID architecture, and the game kit it is becoming

Two audiences. Somebody changing jdVOID needs the first half. Somebody
starting a second game, or writing a client in another engine, needs the
second half and `PROTOCOL.md`.

Everything under "Today" was measured against the source on 2026-08-07.
Everything under "Target" is a proposal and does not exist yet. The split is
kept explicit on purpose: a document that blurs the two stops being usable
the moment somebody trusts it.

## Today

| File | Lines | Role | Generic? |
|---|---|---|---|
| `arena.jdb` | 862 | the rules. No graphics, no network, no files | contract, jdVOID content |
| `server.jdb` | 1374 | rooms, auth, persistence, match log, bot, routes | ~60% game-free |
| `royale.jdb` | 2312 | the client, WebAssembly in a browser | jdVOID |
| `art.jdb` | 205 | SDF rasteriser, shapes in, sprites out | fully generic |
| `duel.jdb` | 143 | what a duel is: two cards, real rules | generic given a rules module |
| `duels.jdb` | 141 | one card against all the others | generic given a rules module |
| `sandbox.jdb` | 332 | the visual bench | generic given a rules module |
| `replay.jdb` | 168 | replays recordings, reports first divergence | generic |
| `whatif.jdb` | 162 | one changed number against real matches | generic |
| `web/sdf.js` | 120 | `art.jdb` ported to JS, byte-identical | generic |
| `web/designer.html` | 966 | figure and value editor | generic given a shape grammar |
| `cards.json` | 19 cards | the content: stats, traits, figures | jdVOID |

### The one thing that is already right

`royale.jdb` does not import `ARENA`. Not once. The client renders whatever
state the server hands it and knows nothing about the rules. That is what
makes a client in another engine a protocol exercise rather than a port.

### The one thing that is wrong

There are two action paths into the rules, `PLAYCARD` and `PLAYWARP`, and
every caller has to know which is which. `replay.jdb` decides with
`IF MAP.EXISTS(p, "CX")`. So does `whatif.jdb`. So does the server. A third
action kind would add a third branch in each of them.

### Where the server reaches into the rules

Twice, and both are in game-specific code:

- `ARENA.TOWERBLOCK` in `BOTTURN`, deciding where the bot may drop.
- `ARENA.ADDEVENT` in `HEMOTE`, pushing an emote into the event list.

Neither belongs to the generic half. Both move with the game code.

## The two contracts

### Contract A: the rules module

A game ships `rules.jdb` with `EXPORT MODULE RULES`. Same name in every game
directory, because `IMPORT` resolves relative to the importing script, and a
fixed name is what lets one copy of a tool serve every game.

```
NEWGAME(cfg)           -> state       JSON-serialisable all the way down
SIMSTEP(st)            -> state       one fixed time step
ACT(st, seat, action)  -> "" | reason the ONLY way in
SEATS()                -> n           how many players a match holds
SETDATA(m) / DATA()    -> table       cards, towers, waves, whatever
```

`ACT` taking an action map instead of one function per action kind is the
change that makes the rest generic. After it, the server, the replayer and
the what-if tool never learn what kinds exist.

Today's `PLAYCARD(st, pid, kind$, x, y)` becomes
`ACT(st, seat, {"K":"PLAY","KIND":"DROID","X":4,"Y":20})` and `PLAYWARP`
becomes another `"K"`. Both stay as thin wrappers so nothing breaks on the
day of the change.

### Contract B: the protocol

`PROTOCOL.md`, next to this file. That document, not this one, is what a
Godot or Unity client is written against.

## Target layout

```
jdb/gamekit/            the kit, one copy of truth
  gk_room.jdb           rooms, seats, lazy catch-up, lobby
  gk_auth.jdb           PIN, sessions, lockout
  gk_store.jdb          JSON files with debounced writes
  gk_match.jdb          history, recordings, checkpoints
  gk_art.jdb            the rasteriser (today's art.jdb)
  gk_duel.jdb           the bench (today's duel.jdb)
  web/kit.css           the shared palette
  web/sdf.js            the rasteriser in the browser
  kit_check.sh          diffs every game's copies against the kit

jdb/demos/games/jdvoid/
  rules.jdb             today's arena.jdb, with ACT
  content.json          today's cards.json
  game.jdb              chests, decks, bot, emotes: the jdVOID half of server.jdb
  server.jdb            wiring only, roughly 150 lines
  client.jdb            today's royale.jdb
  gk_*.jdb              copies, checked by kit_check.sh
```

### Why copies and not a shared module

`jdweb.jdb` and `tmpl.jdb` already live twice, in this repo and in jderg, and
they are byte-identical. The copy model is what those two projects already
run on and it survives a deploy that touches one project and not the other.

What it lacks is a drift alarm. `kit_check.sh` is that alarm, and it is the
same idea as `sdf_check.js`: state the thing that must stay equal, then check
it instead of hoping.

## Decisions

**Seats and board become configuration.** `NEWGAME` hardcodes an 18x30 board
and exactly two players. A tower defence has one seat and its own board.
Both move into the `cfg` map that `NEWGAME` already takes but ignores.

**The recording format follows `ACT`.** A recorded play is
`{"T":tick,"P":seat,"A":{...}}`. The reader accepts the old shape too, or
every match recorded so far is lost to `whatif.jdb`.

**Static pages stay static.** The six web pages are served by nginx without
waking the match server. `tmpl.jdb` would invert that for no gain. The 284
duplicated CSS lines get a shared `kit.css` instead.

**Persistence stays JSON.** Seven players in one file with debounced writes.
The threshold where this stops being right is roughly a thousand players, or
the first sorted query over the whole table. The path from there is
`ergdb.jdb` from jdeRG, which already wraps SQLite with central escaping.

**Polling stays.** `GET /state` every 100 ms carries a Godot client as well
as it carries the browser one. WebSockets would be a second transport to keep
correct for no measured problem.

## Phases

Each phase leaves the game playable and the suites green.

| # | Content | Gate |
|---|---|---|
| 0 | this file and `PROTOCOL.md`, no code | reviewed |
| 1 | `ACT` as the single action path, recording format follows | every recording replays identically |
| 2 | split `server.jdb` into kit modules and `game.jdb` | endpoints answer byte-identically against the running instance |
| 3 | seats and board into `cfg` | a one-seat room starts |
| 4 | tools import `RULES`, `kit_check.sh` | `duels.jdb` runs unchanged in two game directories |
| 5 | `kit.css`, six pages de-duplicated | pages unchanged by screenshot |

Phase 1 carries the only real risk. The recording format changes, and the
reader has to take both shapes or the balance history is gone.

## Open questions

- **Does a second game want the chest economy?** It is currently welded to
  the match end in `RECORDMATCH`. If it stays jdVOID-only it belongs in
  `game.jdb`, and `gk_match.jdb` needs a hook rather than a call.
- **Bot as kit or as game?** `BOTTURN` reads the board and picks a card, so
  it is jdVOID. What is generic is the seat that a bot occupies and the tick
  on which it is asked to decide.
- **Path finding.** A tower defence needs it and `arena.jdb` has none. It is
  game code, but if two games want it, it is the first new kit module.
