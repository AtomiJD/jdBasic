# KING - Baustellen-Arcade

A single-screen climbing arcade game in jdBasic. A builder climbs a building
site while KING, a demolition robot gone wild, rolls cable spools and throws
tools at him; at the top waits the emergency stop. Eight levels, each with its
own mechanic, rounds that come back faster, badges, a level select, music per
level, keyboard and game pad.

The game began as Volker's HTML5 prototype. The hero, the jump physics (20 px
high, 38 frames, about 30 px running), the jump buffer, the crane level with
swinging beams and lifts that wait for them, the tool throws and the idea of
power-ups as data come from there. The robot, the spools, the button, the
other seven levels and the new mechanics are this port's own.

## Decisions

| Topic | Decision |
|---|---|
| Tracking | no jdTrakr project; this file is the project memory |
| Screen | 224 x 256 arcade pixels, window scale from `config.json` |
| Logic | fixed 60 steps per second in `sim.jdb`, deterministic per seed, no graphics |
| Data | levels, sprites, palette, font, themes, physics, items, sounds, music, texts and badges are JSON under `data/` |
| Goal | touch the emergency stop (`button`) to win; the remaining bonus is paid |
| Rounds | after the last level the first comes back faster (`config.json` `round`) |
| Profile | `profile.json` next to the game: hiscore, unlocked levels, levels won, counters, badges |

## Files

| File | Role |
|---|---|
| `sim.jdb` | the rules, `EXPORT MODULE SIM` |
| `bot.jdb` | a player that follows a level's `bot` route and jumps hazards, `EXPORT MODULE BOT` |
| `king.jdb` | the client: drawing, input, sound, menus, profile, badges |
| `sim_test.jdb` | rule checks, gated by `tests/king/sim_rules.jdb` |
| `simtest.jdb` | the bot plays levels headless and reports |
| `data/config.json` | physics, hitboxes, scores, timings, round scaling |
| `data/levels/level_NN.json` | one level each |
| `data/sprites.json` | palette, the 3x5 font and every sprite as a character raster |
| `data/themes.json` | sky, skyline, girder and ladder colours per level look |
| `data/items.json` | helmet, spring boots, stopwatch, bolt |
| `data/sfx.json`, `data/music.json` | effect voices and note lists, menu and finale songs |
| `data/achievements.json` | counters and badges |
| `data/lang.json` | UI texts, de and en |

## Rules in short

- Walk, climb ladders (broken ones only carry spools), jump. A fall of more
  than 20 px is fatal; springboards are safe and throw about 55 px up.
- KING throws what his `pattern` lists: `spool`, `spoolL`, `spoolR`, `hammer`,
  `wrench`. Spools roll downhill, fall off ends, take ladders now and then and
  vanish in a skip (`sinks`). A hammer bounces once and skids, a wrench is
  aimed and bounces up to three times.
- Jumping over a spool, tool or drone scores 100, a bolt 50, the button 500
  plus the bonus left.
- Helmet: takes one hit. Spring boots: higher jumps for a while. Stopwatch:
  everything that is out to get you, the concrete included, runs at half speed.
- Rotten planks (`kind: plank`) give way shortly after you step on them and
  grow back. Gusts (`wind`) push the hero sideways except on a ladder; a
  windsock lifts just before one. Concrete (`flood`) rises after a delay.

## Level format

Platforms are `{x1, y1, x2, y2, kind}` with y as the walking surface; `kind` is
`girder` (default), `plank` or `spring`. Everything else refers to them by
index, so a level can be edited without recomputing heights:

    "start":   { "x": 20, "on": 0 }            standing on platform 0
    "button":  { "x": 22, "on": 1 }
    "ladders": [ { "x": 40, "lo": 0, "hi": 10, "broken": 1 } ]
    "items":   [ { "type": "bolt", "x": 90, "on": 7 }, { "type": "bolt", "x": 112, "y": 110 } ]
    "king":    { "x": 120, "plat": 2, "side": 1, "pattern": ["spoolL", "spoolR"],
                 "interval": 130, "jitter": 50, "spool_speed": 0.75 }
    "drones":  [ { "x1": 40, "x2": 200, "y1": 174, "y2": 174, "speed": 0.55 } ]
    "wind":    [ { "y1": 88, "y2": 120, "force": -0.45, "period": 300, "duty": 110, "phase": 0 } ]
    "flood":   { "y": 262, "top": 60, "delay": 240, "speed": 0.05 }
    "sinks":   [ { "x1": 200, "x2": 224, "on": 0 } ]

A crane level adds `crane` and `movers` (swinging beams and lifts, see level 2).
Movers are appended after the static platforms, so a route refers to the
first swing as platform `len(platforms)`.

Every level also carries `theme`, `music` (bpm and four patterns in the
sequencer's mini-notation: pad, bass, lead, drums) and `bot`, the route the
bot follows: `walk x`, `up`, `down`, `jump dir`, `board p` (change over to
platform p when it lines up), `wait n`.

## Running

From the repo root:

    build\jdBasic.exe jdb\demos\games\king\king.jdb [en] [level N]
    build\jdBasic.exe jdb\demos\games\king\king.jdb shot N [frames]       the bot plays N -> tmp/shot.png
    build\jdBasic.exe jdb\demos\games\king\king.jdb shotscreen title|select|badges|over

Keys: arrows or WASD, Space or K to jump, Enter to pause, M for music, L for
the level select and B for badges on the title, Esc back. An Xbox pad: stick
or D-pad, A or B to jump, Start to pause. Ten seconds on the title start a
demo played by the bot.

Tests run from inside the game folder:

    ..\..\..\..\build\jdBasic.exe sim_test.jdb                  rule checks
    ..\..\..\..\build\jdBasic.exe simtest.jdb [from] [to] [god] [tries]

`god` makes the hero untouchable but not fall-proof, which proves a route
through the level. The gate (`tests/king/sim_rules.jdb`) runs both. Without
`god` and with three lives per level the bot wins all eight.
