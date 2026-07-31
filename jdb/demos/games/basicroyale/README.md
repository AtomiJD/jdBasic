# BASIC Royale

Two-player Clash-style arena duel. The whole match state is one
JSON-serializable map owned by the ARENA module (arena.jdb) - the offline
harness, the match server and the phone client all share it.

- `arena.jdb` - rules: board, cards, PLAYCARD validation, 0.1 s SIMSTEP.
- `game.jdb`  - offline harness, both sides via mouse on one screen.
  `jdbasic game.jdb` to play, `jdbasic game.jdb shot` renders 12 s of a
  scripted opening to `tmp/royale.png`.

Balance lives in ARENA.CARDS() - tweak per jdb_eval while a match runs.
