# jdVOID match protocol

What a client has to speak. This is the document a client in another engine
is written against, and the one the server has to keep honest.

Verified against the running server on 2026-08-07. Where a field is proposed
rather than present it says so.

## Shape of everything

Base URL is the site origin plus `/api` (nginx proxies it to the match
server on 127.0.0.1:8081). Bodies are JSON sent as `text/plain`, because
that is what the browser client can post without a preflight. The server
reads `request{"BODY"}` and parses it itself.

Every answer is a JSON object with `OK`:

```json
{ "OK": 1, ... }
{ "OK": 0, "MSG": "msg_noroom" }
```

`MSG` is a key, not a sentence. `GET /lang` returns the table that turns it
into text, so a client never ships translations of server errors.

**Proposed, not present:** a `V` field carrying the protocol version in every
answer. Without it a non-jdBasic client cannot tell a changed payload from a
broken one.

## Identity

Two different tokens, and mixing them up is the first mistake a new client
makes.

| Token | From | Lives | Carries |
|---|---|---|---|
| `AUTH` | `POST /login` | the player, across matches | the collection, the stats |
| `TOK` | `POST /join` | one seat in one room | the right to act in that match |

A name plus a PIN claims an account on first use. Every later login has to
match the PIN. Five wrong tries lock the name for a minute.

Names: 1 to 16 characters from letters, digits, space, `_`, `.`, `-` and the
German umlauts. Checked server side, so a client cannot widen it.

Rooms are created by name. `ROOMKEY$` lowercases, keeps `a-z0-9-_` and cuts
at 24 characters, so `Main Room!` and `mainroom` are the same room.

## Endpoints

### Getting in

**`POST /login`** &rarr; the account

```json
request   { "NAME": "Atomi", "PIN": "1234" }
answer    { "OK": 1, "AUTH": "<uuid>", "NAME": "Atomi", "CLAIMED": 1 }
errors    msg_badname msg_badpin msg_wrongpin msg_locked msg_badreq
```

`CLAIMED` is 1 only on the login that first set the PIN.

**`POST /join`** &rarr; a seat

```json
request   { "AUTH": "<uuid>", "ROOM": "main", "DECK": ["DROID", ...] }
answer    { "OK": 1, "PID": 0, "TOK": "<uuid>", "ROOM": "main" }
errors    msg_login msg_full msg_noroom
```

`PID` is the seat, 0 or 1 today. `DECK` may be omitted, then the player's
active deck is used.

**`POST /bot`** &rarr; fill the other seat with the computer

```json
request   { "TOK": "<seat token>" }
answer    { "OK": 1 }
```

### Playing

**`GET /state?room=<key>&tok=<seat token>`** &rarr; everything

```json
{ "OK": 1, "PID": 0, "WAITING": 0,
  "NAMES": ["Atomi", "Airxzi"],
  "PROFILES": [ { "NAME": ..., "WINS": ..., ... } ],
  "ST": { ...the world, see below... } }
```

This is the whole loop. Poll it, draw what comes back. The server has no
simulation thread: it works out how many 0.1 second steps it owes since the
last call, runs them, and answers. A room nobody polls costs nothing.

Called without a valid `tok` it still answers with the state, which is how
watching a match works.

**`POST /play`** &rarr; act

```json
request   { "TOK": "...", "KIND": "DROID", "X": 4.0, "Y": 20.0 }
recall    { "TOK": "...", "KIND": "TELEPORTER", "X": ..., "Y": ...,
            "CX": ..., "CY": ..., "R": 3.0 }
answer    { "OK": 1, "MSG": "" }
refused   { "OK": 0, "MSG": "msg_elixir" }
```

A refusal is not an error. The rules said no, and `MSG` says why:
`msg_elixir` not enough, `msg_zone` outside the deploy half, `msg_nohand`
not among the first four, `msg_tower` on a station footprint, `msg_over`
match finished, `msg_unknown` no such card, `msg_nosel` a recall arrived
without a selection, `msg_nocatch` the drawn loop caught no ship.

**The two shapes above are the wart.** A recall carries a selection and a
plain drop does not, so every caller has to know which cards are which.
`ARCHITECTURE.md` proposes one shape for both:

```json
proposed  { "TOK": "...", "A": { "K": "PLAY", "KIND": "DROID", "X": 4, "Y": 20 } }
```

**`POST /emote`** `{ "TOK", "ID" }` where ID is 0 to 5. Rate limited to one
per 1.5 seconds per seat, else `msg_slowdown`.

**`POST /rematch`** `{ "ROOM" }`. Clears the finished match and the recording
with it.

### Collection

**`GET /cards`** &rarr; `{ "OK": 1, "CARDS": { "DROID": { ... } } }`

The whole content table: stats, traits, colour, two-letter icon, rarity and
the `SHAPE` figure. This is the single source. A client that hardcodes card
numbers will drift the first time a card is balanced.

**`GET /profile?auth=<uuid>`** &rarr; `{ "OK": 1, "PROFILE": { ... } }`
collection, levels, decks, last chest.

**`POST /upgrade`** `{ "AUTH", "KIND" }` &rarr; `{ "OK": 1, "LVL": 3 }`.
Errors `msg_nocards`, `msg_maxlvl`, `msg_unknown`.

**`POST /decks`** `{ "AUTH", "DECKS": [[...]], "ACTIVE": 0 }` &rarr;
`{ "OK": 1, "DECKS": [...] }`. Error `msg_baddeck`.

### Public reading, no token

| Endpoint | Answer |
|---|---|
| `GET /rooms` | `{ OK, ROOMS: [ { KEY, N, STATE, LIVE } ] }` |
| `GET /players` | `{ OK, PLAYERS: [ { NAME, WINS, LOSSES, DRAWS, PLAYED, CROWNS, CARDS, UPGRADES } ] }` |
| `GET /player?name=` | one player in detail |
| `GET /matches?limit=` | `{ OK, MATCHES: [ { AT, ROOM, A, B, CA, CB, WINNER, SECS } ], TOTAL }` |
| `GET /cardstats` | `{ OK, MATCHES, CARDS }`, how each card actually performs |
| `GET /lang` | `{ "de": { ... }, "en": { ... } }`, no `OK` wrapper |

`GET /lang` being the one endpoint without the wrapper is an inconsistency,
not a design.

### Admin, key required

`POST /wipe /resetpin /drop /reset /forceend`, each taking `{ "KEY": ... }`
in the body or `?key=` in the query. Without it, HTTP 403 and
`{"OK":0,"MSG":"admin key required"}`. The key lives in `admin.txt` next to
the server and never in the repo.

## The world object

`ST` is the state map straight out of the rules module. No serialisation
step, no schema: `JSON.STRINGIFY$` on the map the simulation works on.

```
TICKNO    int      0.1 second steps since the match began
T_LEFT    float    seconds on the clock, negative in overtime
PHASE     string   "play" | "over"
WINNER    int      -1 while playing, else the seat
CROWNS    [int]    per seat
ELIXIR    [float]  per seat, 0 to 10
QUEUES    [[str]]  each seat's hand, first four are playable
CARDLVL   [map]    kind -> level, per seat
UNITS     [unit]
TOWERS    [tower]
SPELLS    [spell]  in flight, land on their tick
EVENTS    [event]  last 40, cosmetic only
```

A unit: `ID OWNER KIND X Y HP MAXHP DMG CD`, plus `BUILD TTL SPAWNCD SH`
when the card has those traits.

A tower: `OWNER KIND X Y HP MAXHP CD AWAKE`, kind `PRINCESS` or `KING`.

An event: `T` in `SHOT HIT BOOM CAST DIE SPAWN FALL EMOTE`, with `X Y`, the
source `FX FY` where it has one, `D` for damage, `TICKNO` and `EID`.
**Events are decoration.** A client that drops all of them plays correctly
and looks dull. Nothing in the outcome depends on them.

The board is 18 wide and 30 tall in tile units. Seat 0 defends the high `Y`
end. A client that shows seat 1 mirrors the board itself, the server does
not do it.

## Writing a client in another engine

The client never runs the rules. It polls, it draws, it posts actions. That
is the whole job, and it is why a Godot client is a protocol exercise and not
a port of `arena.jdb`.

What that client owes:

1. **Log in, then join.** Keep the two tokens apart.
2. **Poll `/state`.** 100 ms is what the browser client uses. The server is
   idle between calls, so the rate is a client-side choice.
3. **Draw from `ST` only.** Interpolating unit positions between polls is
   fine and is what the browser client does. Never advance the simulation:
   the server is the only clock.
4. **Fetch `/cards` once** and take names, colours and figures from there.
5. **Treat `OK:0` as an answer**, not a failure. Most of them mean the rules
   said no.

What it must not do: assume the field set is closed. Traits add keys to
cards, and cards are data that changes between deploys. Read what you need
and ignore the rest.
