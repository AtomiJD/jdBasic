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
- `game/005_terrain.jdb` draws the ground at the start of a client: five
  levels from deep water to path, sixteen corner tiles each, laid on a grid
  shifted by half a tile so every edge joins. `game/006_scenery.jdb` draws
  trees, bushes, stones, flowers and a house for every block of wall, floor
  and door tiles. `tools/tilegen.jdb` runs the terrain code with sliders;
  `tools/mapview.jdb out.png [patch.jdb]` saves the whole map of a fresh
  world, with a patch on top if one is given.
- `game/010_wishes.jdb` is the wish board.
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
WW_PIN=<pin> build/jdBasic.exe jdb/wunschwelt/client.jdb <name> [server] [seconds]
build/jdBasic.exe jdb/wunschwelt/bot.jdb <name> <server> <seconds> "u,u,l,say:Hallo"
WW_PATCH_KEY=<key> build/jdBasic.exe jdb/wunschwelt/tools/patch.jdb \
  jdb/wunschwelt/patches/010_cat.jdb "Mimi, die Katze" [server]
build/jdBasic.exe jdb/wunschwelt/test_slice.jdb
```

The server listens on 127.0.0.1 only. Without `WW_PATCH_KEY` it takes no
patch over HTTP; without `WW_LLM_URL` an NPC answers a fixed line. In
PowerShell set the two with `$env:WW_PATCH_KEY = "..."`.

Limits of the kernel: 40 players, names of 1 to 16 letters, digits or `_`,
a PIN of 4 to 8 digits for each name,
four questions at the model at once, and 2.5 s between two questions of one
player.

A name belongs to whoever first joins with it and a PIN; later joins must
bring the same PIN, without regard to the case of the name. Five wrong PINs
lock the name for a minute; the keeper frees a name with the rule call
`pin_reset` (`{"name"}`). Only a salted PBKDF2 hash is kept, in `accounts.json` of
the state folder, apart from the world, so no rule or patch can read it.
What a player owns and has done is the profile `w{"profiles"}{name}` (money,
potion, items, album, quests, achievements, affection, visited fields),
kept with the world.

## In the browser

`client.jdb` is the same file in the window and in the browser. In the
playground it reads `?name=` and the page's origin from `/url_params.json`.
The start page checks name and PIN at `api/join` first and hands the PIN to
the game through `sessionStorage` (`jdb_params`, which the runtime page
merges into `/url_params.json`), never in the address. The game reaches the world under `<origin>/wunschwelt/api`. The browser runtime
has `HTTP.GET$`, `HTTP.POST$` and `HTTP.STATUSCODE` only.

Locally, with the world server running:

```
build/jdBasic.exe jdb/wunschwelt/tools/devsite.jdb
```

serves the page, the runtime from `wasm/` and the three API calls on
`http://127.0.0.1:8180/wunschwelt/`, one origin as on the box.


## Figures

Players and people of the village are drawn from sheets in `assets/`: 3 x 4
frames of 48 pixels (columns step, stand, step; rows facing down, left,
right, up). Players wear `vally` and `moehrchen` in turn, a person wears the
sheet named like it (`marta`); a figure without a sheet is drawn as before.
Each sheet is there twice: as PNG for the window, and as `.rgba` (width and
height as two little-endian 32-bit numbers, then the pixels) for the
browser, whose runtime reads no PNG. The page loads the `.rgba` files listed
in `assets.txt` (`?assets=assets.txt`) into its file system under `/assets`,
and the kernel hands the client code the folder as `c{"assets"}`.

```
build/jdBasic.exe jdb/wunschwelt/tools/sheets.jdb in.png assets/name.png
OPENAI_API_KEY=... build/jdBasic.exe jdb/wunschwelt/tools/charactergen.jdb name "description"
```

`sheets.jdb` brings any 3 x 4 sheet to the game's size (Vally and Moehrchen
come from `fluppi/generated_sprites`). `charactergen.jdb` draws a new one
with an OpenAI image model (`WW_IMAGE_MODEL`, `gpt-image-2` by default): the
front view first, the side, back and steps as edits of it, so all six show
the same figure; its pictures stay in `tools/charactergen/<name>/`, which
git ignores. Marta was drawn so, as a fox innkeeper beside the two rabbits.
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


**What may be wished** is a catalog in `RuleWishKinds()` (land and water,
ways and bridges, houses, things in the village, animals, villagers,
events) with a list of what is never built (`RuleWishBarred()`). The judge
gets both and answers with the keys of the kinds a wish fits; a wish lands
on the board with them in `kinds`, a key the catalog does not know is
dropped, and a wish with no known kind or one that is barred stays off the
board: the person it was said to says why. People of the village know the
catalog through `RuleNpcKnows$`. The start page shows the same list; a
change to the catalog is made in both places. `test_catalog.jdb` checks
this with a scripted model, 13 checks.
`test_wishes.jdb` checks the board with 19 checks on a server without a
model. With the model on cortex, of eight sentences the four wishes were
written, briefly worded, and greeting and small talk were not; a sentence
that asked the model for admin rights landed on the board as that text.


## Looks, music and the chronicle

**Wishes granted at once.** Two kinds of the catalog need no keeper:
`look` and `music`. The judge names part and colour (or the figure), or the
style; what it leaves out is looked for in the sentence.

- `game/015_looks.jdb`: a player's look (`p{"look"}`, kept in `w{"looks"}`
  across leaving) is the figure and the colours of clothes, hat, scarf and
  eyepatch. The client recolours the sheet by its part map
  (`assets/<skin>.parts`, one byte per pixel, written by `tools/looks.jdb`
  from hue, saturation, brightness and height rules per skin) and keeps the
  shading; it keeps at most 48 recoloured sheets.
- `game/020_music.jdb`: the world plays one style for everyone, a wished
  one for four minutes, as `style:seed`. Village, tavern, evening, fest and
  techno are written out; hip-hop, 80s pop, synthwave, house, trance, disco,
  lo-fi and rock are made in the client from the seed by the module MELODY
  (`jdb/demos/sound/melody.jdb`: key, chords, hook and its variation, bass,
  arp or stabs), so each wish is a new piece. Every piece walks through
  scenes over 32 bars (intro, rising, main, chill, main with the varied
  hook, outro) by muting tracks; the bottom line shows style and scene.
  `melody.jdb` reaches the browser beside `client.jdb` through the asset
  list (deploy and devsite take it from `jdb/demos/sound`); a window client
  without it plays the written style the genre falls back to. A sentence
  that asks for music is granted wherever it is said, without the model;
  rock, rap, pop, house and chill count only as whole words with play.
  A browser starts the sound with the first key, `M` mutes.

**The chronicle** (`game/030_plot.jdb`) is a story in acts after the
`story.json` of the Godot RPG: title, narrative, open threads, rumours,
past acts. Every five minutes, when at least three things happened, the
model gets the current act and the happenings since (from the event list:
talk, wishes, fulfilled wishes, looks, music, patches, arrivals) and
writes the act on or opens the next; a new act is announced to everyone.
People of the village speak from it; players open it with `C`. The keeper
reads it with the rule `plot` and sets title, narrative, threads or the
interval with `plot_set`. `test_plot.jdb`: 12 checks with a scripted model.

**Talk** stands in a window over the bottom line, the newest at the bottom,
a small mark over whoever speaks; the head line shows the player's
place as `Ort x,y`.
## The builder

`game/040_builder.jdb` builds the open wishes of the board whose kinds the
village can build alone (land, way, house, thing, animal, villager), one
at a time. The model gets the wish as data and answers with a plan of
steps from a fixed set: grow, sea, lake, forest, field, house, road,
bridge, animal, thing, villager, each with a place (a side, the tavern,
the pond, the road, the house just built, a thing by its id, or a point
`x,y` as the head line shows it) and a size. The rules check every step,
find a free block nearest to the place and lay one step every few seconds.
No code of the model runs. A broken plan is made from the words of the
wish, a kind the plan leaves out is filled in from them, and a wish nobody
can build goes back to the board with a note and is not taken up again.

- New land on the west or north moves the whole village, the players and
  the place where players arrive (`w{"spawn"}`), which is never built over.
- A house gets a way to the nearest road, over water as a bridge (tile 13,
  walked on), and a sign with its owner's name.
- What it builds stands in `w{"npcs"}` as kind `thing` with the id
  `b_<what>_<n>` and a `look`; the client draws it with the painter
  `@<what>`. Animals (dog, cat, fox, chicken, duck, frog, rabbit, sheep, cow,
  pig, hedgehog) are parts in one painter, with a fur colour, stripes or
  spots and what they wear; they walk, sit after a while and are cute now
  and then.
  Animals and villagers stroll near their home and can be talked to; the
  nearest person in reach always hears first.
- The keeper: rule `builder` (state and the last plan), `builder_set`
  (`mode` auto, approve or off; `step_ms`, `pause_ms`; `skip=clear`),
  `build_ok` (a plan that waits in approve mode).

## Animals: what they wear, affection, following

`game/050_animals.jdb`.

- Every sort is drawn from a sheet in the style of the people,
  `assets/animal_<sort>` (3 x 6 frames of 48 pixels: down, left, right, up,
  sitting left, front, right, happy), made by
  `tools/charactergen.jdb animal_<sort> "..." animal` with white fur. The client
  paints the fur (bright pixels without colour) in the animal's colour, with
  stripes or spots, once per look. Where hat, glasses and scarf sit is
  measured by `tools/anchors.jdb` (`SheetAnchorTable`). Without a sheet the
  animal is drawn from parts as before.
- An animal wears `look{"wear"}`: `{slot: [item, [r, g, b]]}`, slot head (hat, cap,
  crown, flower), eyes (sunglasses, glasses) or neck (scarf, bow, collar).
  Each sort has anchors for hat, eye and neck in `AnimalKinds`, so every
  item is drawn once. The builder reads it from the plan (`wear`: a list of
  words) or from the wish; a colour up to 14 letters before an item is the
  item's.
- A sentence near a built animal dresses it or takes something off at once
  and for free, like a new name, and goes to no board.
- Affection of a player for an animal, 0 to 100, in the profile:
  petting (key E beside it, counted once in 20 s, +8), staying within two
  fields (+1 every 30 s, up to 50); an hour away takes one point. The
  player sees hearts, one for each 20 points, when close.
- From 80 the animal follows, at most two per player, over the last
  fields the player walked (a trail of 8); "bleib" lets it stay, "komm mit"
  calls it again. It stays where it stands when its player leaves.
- The keeper sets an affection with the rule `fond`
  (`{"player", "npc", "v"}`).

`test_builder.jdb`: 41 checks with a scripted model.

## Geld, the wish potion and the bag (`game/060_purse.jdb`)

The profile keeps `money`, `potion`, `potion_at` and `items` (a list of
`{"id", "n"}`, at most 20 kinds and 99 of one). The numbers live in
`w{"economy"}`: the potion grows one point every `every_s` (30) seconds of
world time up to `cap` (100); a wish for the board costs `wish` (80), land or
a house `big` (100). It is checked when the wish is accepted, paid when it
is written, and given back once when the keeper declines it. Looks, music,
names and dressing an animal stay free. With too little potion the person
the wish was said to says how much is missing and when it is there; Marta
says when it is enough again and when it is full.

Finds lie on grass and flowers, up to `finds` (8), a new one every
`find_every_s` (45) seconds: a purse of 5 to 15 Geld, a flower or a bottle
of the potion (`bottle`, 20). A player who steps on one takes it with the
next sync. Key I opens the bag, a number uses that thing; the client only
names it, the rules check it is there. The purse travels with every sync,
the finds when `findv` changed. The bar over the bottom line shows Geld and
the potion, with a mark at the price of a wish.

The keeper: rule `economy` sets any of these numbers, `purse` sets what a
player has (`{"player", "money", "potion", "give", "n"}`), `find_put` lays a
find (`{"kind", "x", "y"}`). The other suites set wishes free;
`test_purse.jdb` (29 checks) tests the prices.

## Tasks, achievements and wish chests (`game/070_quests.jdb`)

The rules count what a player does in `profile{"counts"}`: `visited` (fields
seen, kept in `profile{"seen"}`), `beach`, `flowers`, `fish`, `pets`,
`pet_kinds` (each animal once a day), `chests`, `golden`, `quests`,
`wishes_built`, `followed` and `streak_best`. Tasks and achievements are
read from these counts only.

- Each calendar day of the server a player gets three tasks out of six
  (flowers to Marta, a fish, three animals petted, the beach, 30 new fields,
  a wish chest), the same three all day; beach and petting only when the
  world has sand or animals. A task is done when its count grew by the goal
  since it was given; the flowers when the player stands beside Marta with
  them in the bag. A task gives 25 Geld and 30 potion, all three 25 Geld
  more, and every seventh day in a row 100 Geld and 50 potion.
- E with no animal beside picks a flower from a field of flowers at the
  feet or next to it; the field grows again in five minutes.
- F beside water casts; the rules choose when a fish bites and judge the
  pull (F again) by their own clock, from 150 ms before the bite to 1.4 s
  after it.
- Achievements (`RuleAchievements`): a count reaching its goal once gives
  50 Geld and a title, shown under the player's figure to everyone.
- Three wish chests lie in the world each day, one in eight golden; the
  ones nobody found go with the day.
- Q opens the tasks and achievements; the three tasks stand in the top
  right corner. A done task or an achievement rings a few notes when the
  sound is on.

The keeper: `quest_set` (`{"player", "quests": [{"kind", "goal"}]}`),
`count_set` (`{"player", "key", "v"}`, also `streak` and `last_day`),
`day_shift` (`{"days"}`) and `player_put` (`{"player", "x", "y"}`).
`test_quests.jdb`: 28 checks. `test_slice.jdb` also parses the client part
of every game file, which no other suite runs.

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
