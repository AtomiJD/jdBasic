# Black Forest WM - a championship scoreboard in jdBasic

A small, self-contained tournament manager written in pure jdBasic
(SQLite + ImGui). Track participants and games, record placements,
watch the standings update live, and print a certificate (PDF) for
every winner. Built for a garden "world championship" but works for
any points-based competition.

Points per game are configurable (1st / 2nd / 3rd, default 3 / 2 / 1),
so a headline event can be worth more than a side game.

## Files

- `bfwm.jdb` - the whole app in one self-contained file: four tabs (Dashboard,
  Teilnehmer, Spiele, Einstellungen) plus a built-in PDF certificate generator
  (hand-rolled PDF, no external library)
- `bfwm.db` - a fresh SQLite database with the event name preset; delete it to start over

## Run natively (recommended)

Build jdBasic with graphics, ImGui and SQLite, then run the script.

Linux / macOS:

```
GFX=1 IMGUI=1 SQLITE=1 ./build.sh
SDL_VIDEODRIVER=x11 ./build/jdbasic jdb/demos/bfwm/bfwm.jdb
```

Windows:

```
build.bat HTTP GFX IMGUI SQLITE
build\jdBasic.exe jdb\demos\bfwm\bfwm.jdb
```

Data is stored in `bfwm.db` next to the script and survives restarts.
`F11` toggles fullscreen. Certificates are written next to the script as
`urkunde_<rank>_<name>.pdf`.

## Run it straight from GitHub (jdBasic WASM)

Because the app is a single self-contained file, it launches from one link in
the hosted jdBasic playground (which ships with GFX + ImGui + SQLite). Open:

```
https://jdbasic.org/live/index.html?prog=https://raw.githubusercontent.com/AtomiJD/jdBasic/main/jdb/demos/bfwm/bfwm.jdb&mode=run
```

The playground fetches the raw source over HTTP and runs it - nothing to
install. Swap `&mode=run` for `&mode=load` to open it in the editor first.

Browser caveats (native is better for a real event):

- SQLite lives in an in-memory filesystem, so the database resets on page
  reload; you start empty with the default event name baked into the code (the
  bundled `bfwm.db` is not loaded in the browser).
- A generated certificate PDF is written to the in-memory filesystem but has no
  download button, so print real certificates from the native build.

## Using it

1. **Einstellungen** - set the event name and date (both go on every certificate).
2. **Spiele** - add each game with its 1st / 2nd / 3rd point values.
3. **Teilnehmer** - add each participant (number, name, an optional title, a free field).
4. **Dashboard** - after each game pick game + participant + place and hit
   *Eintragen*. The **Rangliste** re-sorts by total points instantly.
5. Click **Urkunde** next to a participant to write their certificate PDF,
   showing the event, date, name, title and final placement (gold / silver /
   bronze medal).

Records can be edited or deleted at any time via the *Aendern* / *Loeschen*
buttons; points recompute automatically.
