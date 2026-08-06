# jdVOID

A two-player arena duel in the Clash Royale tradition, written entirely in
jdBasic. It runs in a phone browser through the WebAssembly build, against a
jdBasic match server that owns the simulation.

Live at <https://vvss.jdbasic.tech>.

```
  phone browser                      Hostinger box
  +----------------------+           +---------------------------+
  | index.html (landing) |           | nginx  vvss.jdbasic.tech  |
  |   name / room / lang |  https    |   /        -> /var/www    |
  +----------+-----------+ --------> |   /api/    -> 127.0.0.1   |
             |                       +-------------+-------------+
             v                                     |
  +----------------------+                         v
  | play/index.html      |           +---------------------------+
  |  jdbasic.wasm        |  /api/*   | server.jdb (systemd)      |
  |  royale.jdb (client) | <-------> |   IMPORT ARENA            |
  |  art.jdb             |   JSON    |   rooms, chests, profiles |
  +----------------------+           +---------------------------+
```

The whole match state is one JSON-serializable map owned by `arena.jdb`. The
offline harness, the server and the client all share that module, so there is
exactly one set of rules in the codebase.

## The files

| File | What it is |
|---|---|
| `arena.jdb` | The rules. Board, deploy zones, targeting, collision, the 0.1 s `SIMSTEP`. No I/O, no graphics. |
| `cards.json` | Every card stat. The single source of truth for balance, loaded into `ARENA.SETCARDS`. |
| `server.jdb` | Match server. Rooms, seats, chest economy, card levels, profiles, admin endpoints. |
| `royale.jdb` | The client. Menu, deck editor, battle view, spectator mode, emotes, sound, drag deploy, i18n. |
| `art.jdb` | The rasteriser. Signed-distance shapes into RGBA buffers at startup; the card figures come from `cards.json`, only the two station classes live here. |
| `lang.json` | UI strings and card descriptions per language. Served by the server, so a new language needs no client change. |
| `balance.jdb` | Headless duel harness. Every troop card fights every other one for equal elixir. |
| `standoff.jdb` | Headless too, but the attackers walk in from across the board - which is where reach decides. |
| `push.jdb` | One attacker walks a lane at a defended station and the answer lands on it when it arrives - what a big card costs the defender. |
| `warp_test.jdb` | Assertions for the recall card: what the loop catches, what it refuses, what it charges. |
| `art_test.jdb` | Assertions for the card figures: every card has one, and a row still means what its constructor meant. |
| `pull_test.jdb` | Assertions for the tractor beam: what it hauls, how far, and what it leaves alone. |
| `spawn_test.jdb` | Assertions for hatcheries: a building that spawns, and a brood of more than one card. |
| `splash_test.jdb` | Assertions for splash: who a blast catches around its target, and who it leaves alone. |
| `replay.jdb` | Replays `replays.jsonl`: check every recorded match, or walk one of them card by card. |
| `replay_test.jdb` | Plays a match out while recording it, replays the recording, and compares both down to the tower hit points. |
| `traits_test.jdb` | Assertions for shield, death effects, charge, heal and slow - each defined at runtime, so it doubles as the worked example. |
| `game.jdb` | Offline harness, both sides on one screen. Predates the server, useful for rule work. |
| `artsheet.jdb` | Renders every sprite onto one sheet for a quick look. |
| `makeart.jdb` | Derives `web/hero.png`, `icon.png` and `social.png` from `keyart.png`. |
| `makecards.jdb` | Renders every card's ship into `web/cards/` as a transparent PNG at four times the in-game size. |
| `deploy.sh` | Ships everything to the Hostinger box and restarts the service. |
| `web/` | Landing page, player page, card sheet, balance history, admin console and the key art derivatives. |

Server-side state next to `server.jdb`: `stats.json` (profiles, collections,
decks, PIN hashes), `matches.json` (the match log), `sessions.json` (live
tokens), `replays.jsonl` (one line per match: both decks, the card levels, the seed and every card that landed - appended, never rewritten) and `admin.txt` (the admin key, never committed).

## The game

The board is 18 by 30 tiles with a river at row 14.5 and bridges at columns 3.5
and 14.5. Each side has two princess stations and a king. The king sleeps until
he is hit or until one of his princesses falls. A station holds its own tile:
ships cannot be dropped onto a footprint, on either side of the river. Spells
reach it, ships have to walk around it.

The simulation ticks ten times a second and the client asks for a snapshot
about five times a second, but it draws thirty frames. Every ship keeps the
position it was last drawn at and the one the newest snapshot puts it on, and
the frames in between walk from one to the other over the measured gap between
two answers - so movement is smooth without the server having to tick faster or
the phone having to poll harder. A late answer parks the ships instead of
sliding them past their target.

Each card carries a colour in `cards.json`, and the sprite baker paints it into
the accent slot of that card's figure. The team colour stays in the hull lights
and the health bar takes the owner's colour outright, so a glance still says
whose ship it is while the accent says which card.

During a match either side can send one of six emotes, drawn from discs and
strokes so they need no emoji font, at most one per seat every 1.5 seconds. A
third person can follow a running match from the lobby without taking a seat.

If nobody turns up at all, the waiting screen offers the free seat to the
server itself. The machine plays out of the same hand and elixir as a person
and decides on the sim clock, not on how often a client polls. A match against
it hands out no chest and touches neither record nor match log, so practising
alone cannot farm cards, and it gets no profile either, so it stays out of the
standings.

A match runs 180 seconds. Elixir fills at one per 2.8 seconds up to a cap of 10,
and doubles once the clock passes zero. Overtime lasts 60 seconds. Whoever has
more crowns wins; if the crowns are level after overtime, the side with more
remaining station health takes it, and only a perfect tie is a draw.

Each player brings six cards, four of which are in hand at any moment. Cards
carry traits rather than special cases in the code:

- `FLY` ignores the river and can only be hit by `HITS: "A"` cards.
- `ONLYTOWERS` walks past enemy troops and only stops for stations.
- `BUILD` never moves and holds the tile it was placed on.
- `TTL` decays the unit over time, which is what makes buildings temporary.
- `SPAWNS` / `SPAWNRATE` / `SPAWNN` hatch a brood on the unit's own clock.
  `SPAWNS` is one card or a list of them, dealt out round robin, and it
  combines with `BUILD` and `TTL` into a hatchery that holds its tile and
  wears out.
- `SHIELD` is a pool in front of the hull that never comes back: one big swing
  spends it whole, a swarm chips through it.
- `DEATHDMG` / `DEATHRADIUS` blow up on death, `DEATHSPAWN` / `DEATHN` leave a
  brood behind - a card can do both.
- `CHARGE` / `CHARGEMUL` build a run-up over open ground and spend it on the
  first thing reached, then fight normally until the next run.
- `HEAL` on a spell patches the caster's own ships, never past their hull.
- `SLOW` / `SLOWMUL` cost pace for a while instead of stopping a ship dead.
- `SPLASH` is a blast radius on a hitter: everything hostile it could target
  on its own takes the same damage around whatever it hits. Stations are too
  big to be caught by a neighbour's blast.
- `SPELL` with `RADIUS` and any of `DMG`, `STUN` or `PULL` - `PULL` hauls
  what it catches that many tiles toward the middle, never past it, and
  `SETTLE` is the swing the hauled ships owe afterwards. Buildings hold.
- `WARP` is played by circling your own ships and tapping where they land -
  the loop the finger draws travels with the request as a middle and a radius,
  and `SETTLE` is the swing the arrivals owe before they fight again.

Seventeen cards ship in `cards.json`. Level 1 to 5, each level adds eight percent
to hit points and damage.

## Adding a card

A card is data. `cards.json` carries its stats, its accent colour **and its
figure**, `lang.json` carries the two description strings, and that is the whole
list - no code, and no new client, because the client is handed the same table
over `/cards` at startup.

```json
"TELEPORTER": {
  "SHAPE": [
    ["DISC", 20, 19, 13, "ACCENT", 0.9],
    ["DISC", 20, 19, 9.5, "DARK", 0],
    ["BOXR", 20, 12, 5, 5, 2, 0, "TEAM", 0.9]
  ],
  "COST": 3, "...": "..."
}
```

A row is the shape constructor it replaces: `["DISC", x, y, r, slot, glow]`,
`["BOXR", x, y, halfw, halfh, corner, rot, slot, glow]`, `["BAR", x1, y1, x2,
y2, r, slot, glow]`. Figures are authored on a 40x40 grid. The colour slot is
one of `TEAM` (the owner's colour), `HULL`, `DARK`, `LIGHT` or `ACCENT` (the
card's own `COLOR`). An unreadable row draws a plain disc rather than stopping
the bake, so a typo costs a wrong figure, not a dead client.

After adding one, run `jdbasic makecards.jdb` for the card sheet's PNG and
`jdbasic art_test.jdb` to check the figure parses. `jdbasic artsheet.jdb` shows
the whole roster on one screen; it reads the roster from `cards.json` too.

## The economy

Every finished match hands both players the same chest: five common cards, two
strong ones and one wildcard. The loser is not punished, which matters when the
opponent is your own son. A player's first contact with the server also hands
out a starter chest, so a new name can level a card before the first battle.

Levelling a card costs `2^level` copies, so 2, 4, 8 and 16. Wildcards fill any
gap. All of it lives in `stats.json` next to the server, keyed by player name.

## Running it locally

You need a jdBasic build with `HTTP` and `GFX`. From this directory:

```bash
# terminal 1 - the match server on all interfaces, port 8081
jdbasic server.jdb 0.0.0.0 8081

# terminal 2 - a client
jdbasic royale.jdb Atomi 1234

# terminal 3 - the second client
jdbasic royale.jdb Airxzi 4321
```

Name and PIN are the arguments a hand-started client takes; the form is
`jdbasic royale.jdb <name> <pin> [room]`, and the first launch under a name
claims it. Both clients join room `main` and the match starts as soon as the
second seat is taken. Phones on the same network can play against the desktop
as long as the client's `SRV$` points at the machine's LAN address.

Other entry points:

```bash
jdbasic game.jdb            # offline, both sides on one screen
jdbasic game.jdb shot       # renders a scripted opening to tmp/royale.png
jdbasic balance.jdb         # the duel matrix, takes a few minutes
jdbasic artsheet.jdb        # all sprites on one sheet
jdbasic royale.jdb shot     # one frame of the client to tmp/client.png
```

`balance.jdb` gives every card the same elixir budget and lets the survivors
decide the matchup. Read its output with the harness in mind: a stationary
building never walks into fire, so it scores better there than in a real match,
and a spawner's value accrues over a longer game than the harness simulates.

It has one blind spot that matters for anything with `BUILD`: both sides start
3.5 tiles apart, so nobody is ever out of anybody's reach. `standoff.jdb` closes
it by dropping the attackers eleven tiles away and letting them walk in, which
is when a gunner that out-reaches a turret gets to shoot it for free. Run both
after a reach change.

## Running it in a browser

The client is the same `royale.jdb`. The WebAssembly runtime in `wasm/` loads it
over HTTP into its virtual filesystem, along with the `art.jdb` it imports.

```
play/?prog=royale&fs=1&name=Atomi&room=main&lang=de
```

- `prog` names the program to fetch and run.
- `fs=1` hides the editor chrome and gives the canvas the whole page.
- `name` and `room` are read by the client from `/url_params.json`.
- `lang` picks the language, falling back to the browser's.

The page also passes `__origin`, and the client turns that into its API base, so
the same file works on localhost and in production without a rebuild. Private
addresses are ignored there on purpose.

The landing page at the site root collects name, PIN, room and language. It
trades the PIN for a session token itself and forwards only `auth=`, so the PIN
never reaches the game URL or a server log. That is the link to share.

`players.html` is the public board: standings, one player's collection, the
match log, and the direct tally between any two names. `?a=&b=` deep-links a
head-to-head.

`cards.html` is the card sheet for people who are not in a match: it reads
`/cards` and `/lang`, so it says exactly what the running server plays with and
speaks whatever languages `lang.json` carries. The ship on each card comes from
`web/cards/`, which `makecards.jdb` renders from the same figures the game
draws - so a change to `art.jdb` or to a card colour needs that one command
before the next deploy, or the page shows yesterday's ships. `balance.html` is the history of
buffs and nerfs, rendered from `balance.json` next to it - one entry per change,
newest first, each with the reason. Both are linked from the landing page.

Adding a balance entry is a block in `balance.json`: `date`, the commit in
`build`, `title` and `note` per language, and one `changes` row per number that
moved (`kind` is `buff`, `nerf` or `new`). Write it in the same commit that
changes `cards.json`, or the page and the game drift apart.

To rebuild the runtime:

```bash
./build_wasm.sh            # in the repo root, produces wasm/jdbasic.{js,wasm}
```

## The server

`server.jdb` binds host and port from the command line. Behind a reverse proxy
pass `127.0.0.1` so only the proxy can reach it:

```bash
jdbasic server.jdb 127.0.0.1 8081
```

It keeps every room in memory and only writes `stats.json` to disk. Rooms are
created on demand by name, which is how two pairs play at the same time without
seeing each other.

The simulation is lazy. Nothing runs on a timer; `/state` catches the room up to
wall-clock time before answering. A room nobody polls costs nothing.

`/rooms` catches every room it reports up for the same reason, or a match that
ran out of time would keep claiming it is being played, and it reports what
each room is doing: free, waiting, ready, play or over. A decided room whose
seats have been quiet for a minute goes back to free, so the lobby does not
fill up with matches nobody is in any more. The client refreshes the list every
few seconds while the menu is on screen.

### Identity

A name belongs to whoever claimed it first, with a 4 to 8 digit PIN. Only the
salted SHA-256 of the PIN is stored; five wrong tries lock the name for a
minute. `/login` trades name and PIN for a session token, and that token stands
in for the PIN on every later request, so the PIN travels exactly once. Tokens
live in `sessions.json` and survive a restart, which matters because a deploy
restarts the service.

`/join`, `/profile`, `/upgrade` and `/decks` resolve the player from the token
and ignore any name in the request body, so a seat cannot be taken under
someone else's name. A client whose token the server rejects clears it and says
so, rather than silently falling back to an empty profile.

### Seats

A seat belongs to a name. Reconnecting under the same name always gets the seat
back with the match intact, which is what a locked phone or a reloaded tab needs.
A stranger may only take a seat that has been silent for 60 seconds, and never
while a match is being played.

### Decks

Every profile carries three named deck slots plus the active one, validated
server-side as six distinct cards that exist. They live with the profile, so
they survive a reload and follow the name to another device. The client sends
the active deck when it takes a seat.

### Endpoints

Everything answers JSON. `room` selects the room and defaults to `main`.
`AUTH` is the session token, in the body for POST or as `?auth=` for GET.

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | Plain-text banner with the room list. |
| POST | `/login` | `{NAME, PIN}` claims a name or proves ownership, returns `AUTH`. |
| POST | `/join` | `{AUTH, ROOM, DECK}` takes a seat, returns `PID` and a seat token. |
| GET | `/state?room=&tok=` | The full match state, after catching the sim up. Works without a token, which is what a spectator uses. |
| POST | `/play` | `{TOK, KIND, X, Y}` deploys a card. |
| POST | `/emote` | `{TOK, ID}` sends one of six emotes, one per seat every 1.5 s. |
| POST | `/bot` | `{TOK}` gives the free seat to the machine, if you are the one sitting there. |
| POST | `/rematch` | Same two players, new match. |
| GET | `/cards` | The card sheet. |
| GET | `/lang` | All languages from `lang.json`. |
| GET | `/profile?auth=` | Own record, collection, wildcards, last chest and decks. |
| POST | `/decks` | `{AUTH, DECKS, ACTIVE}` stores the deck slots. |
| POST | `/upgrade` | `{AUTH, KIND}` levels a card and applies it to a running seat. |
| GET | `/rooms` | Open rooms with their occupants and a `LIVE` flag. |

Three more are public, and are what `players.html` reads:

| Method | Path | Purpose |
|---|---|---|
| GET | `/players` | Standings: record, crowns, collection size, upgrades. |
| GET | `/player?name=` | One player's collection with levels. |
| GET | `/matches?limit=` | The match log, newest first. |

Six endpoints are sensitive and fail closed without the admin key:

| Method | Path | Purpose |
|---|---|---|
| POST/GET | `/reset` | Empties a room. |
| POST/GET | `/drop` | Removes a room entirely. |
| POST | `/forceend` | Ends a stuck match. |
| POST | `/wipe` | `{NAME}` deletes a player profile. |
| POST | `/resetpin` | `{NAME}` frees a name's PIN so its owner can set a new one. |
| GET | `/leaderboard` | Every profile with record, crowns and whether the name is claimed. |

The key is read from `admin.txt` next to the server at startup and is never
committed. Without that file the six endpoints stay closed for everybody. Pass
the key as `?key=` or as `KEY` in the JSON body:

```bash
curl -s "https://vvss.jdbasic.tech/api/rooms"
curl -s -X POST -d '{"KEY":"...","ROOM":"main"}' https://vvss.jdbasic.tech/api/reset
curl -s "https://vvss.jdbasic.tech/api/drop?room=test&key=..."
```

`web/admin.html` is the same thing with buttons. It keeps the key in
localStorage and talks to `/api/` relative to itself.

## Deploying

`deploy.sh` does the whole round trip. It expects an ssh key at
`~/.ssh/jdtrakr_deploy` and a `deploy` user on the box.

```bash
./deploy.sh          # game files, client and landing page, restart the service
./deploy.sh --wasm   # also ship the 10 MB runtime and the editor vendor bundle
```

The runtime only needs shipping when jdBasic itself changed. Day-to-day work on
the game is the plain form, which moves a few hundred kilobytes.

### The box

```
~/royale/                 server.jdb, arena.jdb, cards.json, lang.json
                          admin.txt (chmod 600), stats.json
~/royale/web/             landing page, admin console, key art
~/royale/web/cards/       one PNG per card, from makecards.jdb
~/royale/web/play/        client, wasm runtime, vendor bundle
/var/www/vvss/            what nginx serves, copied from ~/royale/web
```

`royale.service` is a plain systemd unit:

```ini
[Unit]
Description=jdVOID match server
After=network.target

[Service]
Type=simple
User=deploy
WorkingDirectory=/home/deploy/royale
ExecStart=/home/deploy/jdBasic/build/jdbasic /home/deploy/royale/server.jdb 127.0.0.1 8081
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

nginx terminates TLS, serves the static files and proxies `/api/` to the match
server. Three details are load-bearing:

```nginx
location /api/ {
    proxy_pass http://127.0.0.1:8081/;
    proxy_http_version 1.1;
    proxy_set_header Connection "";
}

location ~* \.(wasm)$ {
    types { application/wasm wasm; }
    default_type application/wasm;
}

location ~* \.jdb$ {
    default_type text/plain;
    add_header Cache-Control "no-store";
}
```

`.jdb` must not be cached, or a deploy leaves players on yesterday's client. The
`wasm` MIME type is what lets the browser stream-compile the runtime.

The Content-Security-Policy needs `wasm-unsafe-eval` for the runtime and `blob:`
for the editor's workers:

```
script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval' blob:; worker-src 'self' blob:
```

Certificates come from certbot in the usual way:

```bash
sudo certbot --nginx -d vvss.jdbasic.tech
```

### First-time setup

```bash
ssh deploy@<box>
mkdir -p ~/royale/web/play
head -c 24 /dev/urandom | base64 > ~/royale/admin.txt && chmod 600 ~/royale/admin.txt
sudo install -d -o www-data -g www-data /var/www/vvss
sudo systemctl enable --now royale
```

Then run `./deploy.sh --wasm` once from the repo. The service needs a jdBasic
build with `HTTP` on the box; `GFX` is not required for the server.

## Balance work

Card stats live in `cards.json` only. The server loads it at startup, serves it
to the client over `/cards`, and the client draws its sprites and card sheets
from the same numbers. A balance change is an edit plus a restart.

The offline loop is:

```bash
jdbasic balance.jdb     # where does the new card sit
# edit cards.json
jdbasic balance.jdb     # did it move
./deploy.sh             # ship it
```

`ARENA.CARDS()` is also reachable from the jdBasic MCP workshop, so stats can be
poked while a match runs.
