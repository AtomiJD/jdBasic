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
`KAskNpc(w, npc_id$, player$, text$)`.

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
one in `build/`, so a builtin that works locally can be missing there. A
patch sent with `--patch` does not go through the preflight yet.

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
sent key events from a script). Phone (Twilio) and the wish board, the content lane of the local model,
canary bots on the farm, rollback, keeping the world across a server
restart, rehearsals.
