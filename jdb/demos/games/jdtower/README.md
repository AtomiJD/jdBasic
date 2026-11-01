# jdTower

A neon tower defence in jdBasic for the phone and the desktop. Portrait
screen, vector art with bloom, 20 levels with several waves each, eight
tower types with an upgrade path that branches at tier 3.

## Decisions

| Topic | Decision |
|---|---|
| Tracking | no jdTrakr project; this file is the project memory |
| Screen | portrait, 480 x 864, played with taps |
| Upgrades | tiers 1-3 straight, then branch A or B with two tiers each |
| Progress | a small server keeps players, stars and a leaderboard |
| Paths | one path at first, two entries from level 10 on |
| Sound | shot effects and quiet background music; nothing else yet |
| Look | the neon of jdbasic.org and the Kaiku music videos: additive lines, bloom from mip targets |

## Files

| File | Role |
|---|---|
| `sim.jdb` | the rules, `EXPORT MODULE SIM`. No graphics, no network |
| `jdtower.jdb` | the client: menus, rendering, input, sound |
| `server.jdb` | login, progress and leaderboard over HTTP |
| `genlevels.jdb` | writes `data/levels/level_NN.json` from a seed per level |
| `simtest.jdb` | a bot plays every level headless and reports the outcome |
| `musiccheck.jdb` | renders every level's MUSIC loop, `<level> wav` writes it to tmp/ |
| `makeicon.jdb` | renders the app icons into web/ |
| `sim_test.jdb` | rule checks, gated by `tests/jdtower/sim_rules.jdb` |
| `web/`, `deploy.sh` | landing page, manifest, icons, service worker; deployment to vvss.jdbasic.tech/tower/ |
| `data/towers.json` | tower stats per tier and branch |
| `data/enemies.json` | enemy kinds |
| `data/lang.json` | UI strings, de and en |
| `data/levels/` | one JSON file per level: paths, build slots, waves, music |

The level files are the source of truth once written. `genlevels.jdb` is
how they were made; editing a level by hand is fine, re-running the
generator overwrites it.

## Rules in short

- Enemies walk a path of tile waypoints; fliers take the straight line
  from the path's start to its end.
- Towers stand on fixed build slots. Building costs the tier-1 price,
  every upgrade its own price; selling returns 80% of everything spent.
- Credits come from kills, from launching a wave, and from calling the
  next wave early. A RELAY on branch B pays credits per wave launch.
- A leaked enemy costs lives. Stars: 3 at 90% of the lives left or more,
  2 at 50% or more, 1 for any win.

## Running

From the repo root (the client finds its data next to itself):

    build\jdBasic.exe jdb\demos\games\jdtower\jdtower.jdb [name pin] [fps]
    build\jdBasic.exe jdb\demos\games\jdtower\jdtower.jdb shot [level]   -> tmp/shot.png
    build\jdBasic.exe jdb\demos\games\jdtower\jdtower.jdb shotmenu       -> tmp/shot.png
    build\jdBasic.exe jdb\demos\games\jdtower\jdtower.jdb selftest       scripted taps through the real UI

`simtest.jdb` and `genlevels.jdb` read and write `data/` relative to the
working directory, so run them from inside `jdb/demos/games/jdtower`:

    ..\..\..\..\build\jdBasic.exe simtest.jdb [from] [to]
    ..\..\..\..\build\jdBasic.exe genlevels.jdb [level]

The server keeps `players.json` and `sessions.json` in its working
directory:

    jdbasic server.jdb [port] [host]          default 8082 on 0.0.0.0

On the desktop the client talks to `http://127.0.0.1:8082`; served from a
host it uses `<origin>/tower/api`. Without a login it plays as a guest and
keeps progress in `progress.json` next to the game (desktop only).

## Browser and deployment

The client runs in the WebAssembly playground (`wasm/index.html`) as
`play/?prog=jdtower&fs=1[&auth=..&name=..][&fps=1]`; `web/index.html` is
the landing page that trades name and PIN for a session token.

The runtime must be from 2026-10-27 18:30 or later: the bloom uses
`GFX.NEWTARGET`, `GFX.TARGET` and `GFX.DRAWQUAD`, the vector art
`GFX.LINES`. It is built on cortex (`~/cc_wasm/cc`, `./build_wasm.sh`)
and copied into `wasm/`.

`deploy.sh` ships to vvss.jdbasic.tech under `/tower/`: `--setup` once
(systemd unit `jdtower` on 127.0.0.1:8082, nginx route `/tower/api/`),
`--wasm` when the runtime changed, plain for game and data updates.

Live since 2026-11-01 at https://vvss.jdbasic.tech/tower/. The score
server runs on the box's jdBasic 1.2 build in `~/jdbasic-1.2` (the
26 October refresh, commit `bd2eb677`, built with `GFX=0 NET=0 HTTP=1
SQLITE=1`); `players.json` and `sessions.json` live in `~/tower`.

## Music

Each level file ends in a `MUSIC` block: `BPM` and four patterns in the
sequencer's mini-notation, played in a loop on fixed voices - `PAD`
(chords), `BASS`, `ARP` and `DRUMS` (the built-in synth kit: `bd sd hh oh
cp ...`). Edit the notes there; a pattern the sequencer refuses falls back
to the menu loop for that track, and `musiccheck.jdb` names it before
anyone hears it. The generator composes the blocks from a key, one of six
progressions and three grooves per level; boss levels drive harder.
