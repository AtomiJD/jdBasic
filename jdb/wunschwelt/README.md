# Wunschwelt

A small RPG world that is reshaped while it runs. Players walk a village
together and talk to characters a local language model plays; a change to
the game arrives as a patch and is live for the server and every client
without a restart. Board: jdTrakr project `marketing`, epic #993.

## How it is built

**The game is its patches.** Neither kernel knows a game rule.

- `server.jdb` holds the world, speaks HTTP, keeps the patch list and
  carries questions to the model.
- `client.jdb` opens the window, fetches the client code from the server and
  calls `ClientFrame(c)` once per frame.
- `game/000_world.jdb` is the first patch: the village, walking, talking,
  Marta the innkeeper, and everything the client draws.
- `game/010_wishes.jdb` is the second: the wish board.
- `patches/` holds later patches (`010_cat.jdb`: a cat that strolls).

A patch file has two parts, `'@server` and `'@client`. Each part is jdBasic
source that the kernel runs with `EXECUTE`.

## Rules for a patch

These come from what `EXECUTE` can and cannot do (measured in `spikes/`):

- Rules are **top-level SUBs and FUNCs**. Defining one again replaces it for
  the next call; a call that is running finishes with the old body.
- A patch body **sees no global** of the kernel. The world goes in as the
  parameter `w` (server) or `c` (client). Top-level statements of a patch
  reach it with `JDB.GLOBAL_GET("W")` or `JDB.GLOBAL_GET("C")`.
- A patch may call the kernel's services, other rules and builtins.
- The world is plain data (maps, arrays, numbers, strings), so it outlives
  any code.
- A FUNC of an imported module cannot be replaced: keep rules out of modules.
- A patch that does not parse is refused and changes nothing.

Entry points the server kernel calls: `RuleMakeWorld`, `RuleJoin`,
`RuleSync`, `RuleTick`, `RuleNpcPrompt`, `RuleNpcReply`. Extension points of
the first patch: `RuleNpcTick(w, npc, now)` (what one NPC does in a tick),
`RuleBlocked(w, x, y)`, `ClientNpc(c, row, sx, sy)` (how one NPC is drawn).
Kernel services for rules: `KNow()`, `KEmit(w, event)`,
`KAskNpc(w, npc_id$, player$, text$)`, and `KAsk$(w, kind$, who$, data,
messages, options)` for any other question to the model; its answer comes
back through `RuleAnswer(w, kind$, data, text$)` or `RuleAnswerFailed`.
Further extension points, empty in the first patch: `RuleWake(w)` (every
start), `RuleSaid(w, p, npc, said$)`, `RuleExtra(w, p, msg, answer)`,
`RuleAdmin(w, name$, args)`, and on the client `ClientAsk(c, msg)`,
`ClientExtra(c, r)`, `ClientThing(c, row, sx, sy)` (an NPC of the kind
`thing`) and `ClientOverlay(c, cam_x, cam_y)`. Each has one body: a patch
that defines one again takes it over.

## Running it

From the repository root:

```
WW_PATCH_KEY=<key> WW_LLM_URL=http://cortex:8082/v1/chat/completions \
  build/jdBasic.exe jdb/wunschwelt/server.jdb [port] [seconds]
build/jdBasic.exe jdb/wunschwelt/client.jdb <name> [server] [seconds]
build/jdBasic.exe jdb/wunschwelt/bot.jdb <name> <server> <seconds> "u,u,l,say:Hallo"
WW_PATCH_KEY=<key> build/jdBasic.exe jdb/wunschwelt/tools/patch.jdb \
  jdb/wunschwelt/patches/010_cat.jdb "Mimi, die Katze" [server]
build/jdBasic.exe jdb/wunschwelt/test_slice.jdb
```

The server listens on 127.0.0.1 only. Without `WW_PATCH_KEY` it takes no
patch over HTTP; without `WW_LLM_URL` an NPC answers a fixed line. In
PowerShell set the two with `$env:WW_PATCH_KEY = "..."`.

Limits of the kernel: 40 players, names of 1 to 16 letters, digits or `_`,
four questions at the model at once, and 2.5 s between two questions of one
player.

## In the browser

`client.jdb` is the same file in the window and in the browser. In the
playground it reads `?name=` and the page's origin from `/url_params.json`
and reaches the world under `<origin>/wunschwelt/api`. The browser runtime
has `HTTP.GET$`, `HTTP.POST$` and `HTTP.STATUSCODE` only.

Locally, with the world server running:

```
build/jdBasic.exe jdb/wunschwelt/tools/devsite.jdb
```

serves the page, the runtime from `wasm/` and the three API calls on
`http://127.0.0.1:8180/wunschwelt/`, one origin as on the box.

## On the box

`deploy.sh` (needs `WW_HOST=user@address`) puts the game on the Hostinger
box as a sub-path of the site that already carries the other two games:
`--setup` once, then plain for a new version, `--page` for the page and the
client alone, `--patch <file> <title>` for a patch into the running world.
The unit `wunschwelt` runs as its own user without sudo and with a read-only
file system; nginx hands on `join`, `sync` and `code` and nothing else, so a
patch can only be sent from the box itself. `tools/nginx_route.jdb` writes
those routes into the site file.

Before anything is published, `deploy.sh` runs a preflight on the box: the
staged server starts twice on spare ports with the box's own jdBasic, and
`tools/preflight.jdb` plays a round against each (join, walk, a spoken line
with umlauts, the NPC's answer, two seconds of ticks), once without a model
and once with a model that cannot be reached. A rule that fails there stops
the deploy; `--check` runs only this. The box's jdBasic is older than the
one in `build/`, so a builtin that works locally can be missing there. Each
test server runs on its own copy of the live state, so the stored patches
and the saved world are tried with the new files. A patch sent with
`--patch` goes through the same preflight first, on top of the patches the
live world already has, and stays out when a step fails.

## The wish board

What a player says to a person goes to the local model a second time, with
the question whether the sentence holds a wish for the village and how to
word it in twelve words. A wish lands on the board beside the tavern door
(`w{"wishes"}`, saved with the world); whoever stands at the board reads
it, and every client sees a line when a wish arrives or moves along. Without
a model, or when it gives no usable answer, a sentence that names a wish is
taken as it stands. One player holds three open wishes at most, the board
forty.

A wish is text a stranger typed. It is shown and stored and nothing else:
no rule acts on its words, and whoever builds from the board reads it as
data. `POST /rule` hands a call of the keeper to `RuleAdmin`; it needs the
patch key and nginx does not route it.

```
./deploy.sh --wishes                      the board of the live world
./deploy.sh --wish 3 building             open, building, done, declined
build/jdBasic.exe jdb/wunschwelt/tools/admin.jdb wishes      a local world
```

`test_wishes.jdb` checks the board with 18 checks on a server without a
model. With the model on cortex, of eight sentences the four wishes were
written, briefly worded, and greeting and small talk were not; a sentence
that asked the model for admin rights landed on the board as that text.

## What survives a restart

With `WW_STATE_DIR` set (on the box: `/var/lib/wunschwelt`, readable by the
service alone) the server keeps two files there:

- `patches.json`: every patch that came over HTTP, written when it is taken.
  On a start the files under `game/` run first, then these in their order.
- `world.json`: the world, written every three seconds and at a clean end.
  After the patches have run, the saved world takes the place of the fresh
  one, players and their tokens included, and the world's clock goes on.

A client notices nothing but a moment without an answer; the head line counts
the restarts. Every answer carries `code_id`, the server's name for the
client code of all versions so far. When the game under `game/` changed
with the restart, that name differs from what the client ran, and the
client fetches everything anew and starts its code over. A file that
cannot be read is put aside (`*.unreadable.json`) and the server starts
without it. `deploy.sh --reset-world` forgets both files.
`test_persist.jdb` checks this with 18 checks.

## Machines

- **cortex**: the chat model (Qwen2.5-7B-Instruct) on port 8082, started by
  `~/wunschwelt/serve_npc.sh` in the screen session `npc_chat` (not restarted
  after a reboot). The user service `wunschwelt-tunnel` keeps a reverse
  forward to the Hostinger box, where the model answers on `127.0.0.1:18082`.
- **Hostinger box**: the unit `wunschwelt` and the page, see above. The tunnel key in
  `~deploy/.ssh/authorized_keys` may open that one forward and nothing else.

## State

Done and checked: the three spikes; the slice on one machine (server, window
client, bots, Marta through the model on cortex, a patch in mid-run);
`test_slice.jdb` with 19 checks; the client in the browser against the local
site (walking, talking to Marta through the model, the cat patch arriving
in mid-run).

On the box since 2026-11-11: https://vvss.jdbasic.tech/wunschwelt/ (checked
in Chrome: Marta through the tunnel to cortex, the cat patch sent with
`deploy.sh --patch` arriving in mid-run, typed umlauts through `ON "TEXTINPUT"`).
The site lets a browser keep the runtime for an hour, so a new runtime
reaches a returning visitor that late.

Not done: typing on a real keyboard in the browser was not tried (the check
sent key events from a script). Phone (Twilio), a public page of the board, the content lane of the local model,
canary bots on the farm, rollback (taking a stored patch out again),
rehearsals. The preflight does not run the client code.
