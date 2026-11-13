# Wunschwelt: where we stand and what comes next

Written at the end of the session of 2026-11-11, for a start with a fresh
context. `README.md` beside this file describes how the game is built; this
file is the state of the work. Board: jdTrakr project `marketing`, epic #993.

## What runs

Live at https://vvss.jdbasic.tech/wunschwelt/ (a sub-path of the games site
on the Hostinger box):

- a village for up to 40 players in the browser (WASM) and in a window
- Marta the innkeeper, played by Qwen2.5-7B on cortex (port 8082), reached
  from the box through the reverse tunnel on `127.0.0.1:18082`
- the wish board: what a player wishes for while talking to Marta is judged
  and worded by the model and written on the board beside the tavern door
- live patches: a patch goes through a preflight on a copy of the live
  world and then into the running world, for server and every client
- the world, its players and the patches that came over HTTP survive a
  restart of the service

Live state at the end of the session: version 5 (two game files, then the
patches cat, well, statue), 8 restarts counted, both wishes on the board
done, rule errors 0.

## How to work on it

```
build/jdBasic.exe jdb/wunschwelt/test_slice.jdb      19 checks
build/jdBasic.exe jdb/wunschwelt/test_persist.jdb    18 checks
build/jdBasic.exe jdb/wunschwelt/test_wishes.jdb     19 checks
build/jdBasic.exe jdb/wunschwelt/test_catalog.jdb    24 checks
build/jdBasic.exe jdb/wunschwelt/test_plot.jdb       12 checks
build/jdBasic.exe jdb/wunschwelt/test_builder.jdb    23 checks

cd jdb/wunschwelt            (WW_HOST=user@address of the box must be set)
./deploy.sh --check          stage and preflight, publish nothing
./deploy.sh                  new game or kernel: preflight, publish, restart
./deploy.sh --wasm           the same and ship the runtime from wasm/
./deploy.sh --patch <file> <title>     one patch into the running world
./deploy.sh --wishes         read the live board
./deploy.sh --wish <id> <open|building|done|declined>
./deploy.sh --reset-world    forget the saved world and its patches
```

One run of a wish: `--wish <id> building`, write `patches/NNN_name.jdb`,
try it on a local world with `tools/patch.jdb` and look at it through
`tools/devsite.jdb` in the browser, `--patch`, `--wish <id> done`.

A local world with the model:

```
$env:WW_PATCH_KEY = "..."; $env:WW_LLM_URL = "http://cortex:8082/v1/chat/completions"
build\jdBasic.exe jdb\wunschwelt\server.jdb
build\jdBasic.exe jdb\wunschwelt\tools\devsite.jdb     http://127.0.0.1:8180/wunschwelt/
```

## Done on 2026-11-13, not deployed yet

Live since 2026-11-12 evening: looks, music, chronicle (#1007-#1009), the
calmer village theme, music asked for without the model, questions kept off
the board (live version 11). Committed after that, not on the box:

- the builder (`game/040_builder.jdb`, see README): open wishes are built
  by the rules from the model's plan; checked with the real model on a
  local world (sea, land in the west with road, forest and hut, bridge,
  bench and lantern, a fisher, a dragon refused, animals in colours)
- wishes said during the model's pause are judged after it; the nearest
  person hears before any animal
- animals with colours, patterns, hats and walk, sit and cute poses; the
  head line shows `Ort x,y` and the builder builds at a point
- a talk window instead of overlapping bubbles
- music: eight genres made by MELODY from a seed, scenes for every piece;
  the deploy now ships `melody.jdb` from `jdb/demos/sound`

The live world will start building its open wishes right after the deploy
(Loni's frog). `./deploy.sh` (no `--wasm`) is enough. Heard by no one
yet: the generated music; it was rendered offline and seen running in the
browser (style and scene in the bottom line).

Looked at: True Pixel (sorceress.games) makes no art itself. It turns a
still or a video (frames at 30 fps) into pixel art: background off
(chroma key or CorridorKey), one palette for all frames (auto or presets),
optional dither, temporal smoothing against flicker, downscale, export as a
sheet. For us: animate first (a short video of a walking figure from an
image or video model), then convert with a shared palette, instead of
asking an image model for each frame on its own, which drifts.

## Done on 2026-11-12, live since that evening

- marketing #1003: the ground from the biome generator (`game/005_terrain.jdb`),
  trees, stones, flowers and roofed houses (`game/006_scenery.jdb`),
  `tools/tilegen.jdb`, `tools/mapview.jdb`
- marketing #1004: figures from sheets (Vally, Moehrchen, Marta as a fox),
  `tools/sheets.jdb`, `tools/charactergen.jdb`, the page loads `?assets=`
  (`wasm/index.html`), deploy ships `assets/*.rgba` and `assets.txt`
- marketing #1005: the wish catalog and the strict judge, `test_catalog.jdb`

Deployed with `./deploy.sh` (live: version 7, rule errors 0; the cat, the
well and the statue run on the new ground). Checked: the four tests, the
window client, the browser through `tools/devsite.jdb` and the live page
(ground, houses, the three sheets, walking). Not checked: the judge with the
real model on cortex. The live world keeps its saved map; the new kinds
(sand, deep water, bush, stone) come with the first patch that lays them.

Traps: the browser runtime reads no PNG (`IMG_Load failed`); pictures go
there as `.rgba`. `tools/devsite.jdb` and a running local server read their
files at start: restart both after a change. The world file `000_world.jdb`
runs before `005`/`006`, which works because a call finds a SUB of a later
chunk by name when it runs.

## Traps met on the way

- The jdBasic on the box is the 1.2 refresh build, older than `build/`. A
  builtin that works locally can be missing there (`LEFTW$`, `LENW`). The
  preflight catches this for the server rules, not for the client code.
- A browser keeps `jdbasic.js` and `jdbasic.wasm` apart in its cache, and
  the site's service worker serves the runtime cache-first. The page asks
  for both under one mark of the runtime; after a runtime change, test in a
  browser that had the page open before.
- `deploy.sh --setup` writes the systemd unit and is only needed when the
  unit changes; it restarts the service.
- A new file under `game/` shifts every version number by one; the tests
  count from the number of game files.
- Each extension point of the first patch has one body. Things drawn in the
  world enter their painter with `ClientPainter(id$, Painter@)` instead.
- A patch body sees no global: the world comes as `w` or `c`, top-level
  statements reach it through a SUB that calls `JDB.GLOBAL_GET`.
- The Chrome tool's typing gives no text events for small letters; the live
  checks sent key events from a script. Real keyboards work in the browser.
- A commit command that holds a personal name or the address of the box is
  blocked by the commit hook; run such commands apart from `git commit`.
- A wish is text a stranger typed. It is read as data, never as an
  instruction; one test wish asked for admin rights and was declined.

## Not checked

- typing umlauts on a real keyboard in the Windows window (`ON "TEXTINPUT"`
  passed its test with posted characters and was typed in the browser)
- Linux and macOS builds of the text input event
- the wish board's panel after it moved to the left edge

## Next, in this order

1. **Safety net for live patches.**
   - rollback: take one stored patch out again (today only `--reset-world`)
   - a client without a window that runs the client code of every patch
     once, as part of the preflight; both failures of 2026-11-11 were in
     client code
2. **Phone** (planned for the weekend): callers talk to Marta through
   Twilio, the wish lands on the same board; Telegram and Discord later on
   the same entry. Needed from the keeper: the Twilio number, its
   credentials on the box, a yes to a public webhook route.
3. **The show.**
   - a public page of the board
   - bots from the farm as villagers and as tasters of a patch
   - content from the local model: rumours, events, new villagers
   - a full rehearsal with a stopwatch, from the call to the wish fulfilled

Small things on the side: the
other two games on the site still ship their page without the runtime mark;
jdbasic #1001 (on Linux a second server starts on a port in use).

## Board

- marketing: #993 epic (doing); #997, #999, #1000, #1002 on review
- jdbasic: #998 `ON "TEXTINPUT"` on review; #1001 open

## Git

Everything of this session is pushed (2026-11-11, up to the commit that
brought this file). The CI run for that push was not awaited: look at it
first. The build tree for the WASM runtime on cortex (`~/cc_wasm/cc`) stands
at an earlier commit of the session and carries a branch `ww-textinput` from
a direct push; `git pull --ff-only` brings it level.
