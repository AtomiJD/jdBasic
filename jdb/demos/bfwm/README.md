# Black Forest Games - a championship scoreboard in jdBasic

A small, self-contained tournament manager written in pure jdBasic
(SQLite + ImGui). Track participants and games, record placements,
watch the standings update live, and print a certificate (PDF) for
every winner. Built for a garden "world championship" but works for
any points-based competition: a school sports day, a company summer
party, a club tournament, a birthday.

Points per game are configurable (1st / 2nd / 3rd, default 3 / 2 / 1),
so a headline event can be worth more than a side game.

## Files

- `bfwm.jdb` - the whole app in one self-contained file: four tabs (Dashboard,
  Participants, Games, Settings) plus a built-in PDF certificate generator
  (hand-rolled PDF, no external library)
- `bfwm.db` - a fresh SQLite database with the event name preset; delete it to start over

---

# Running it for your own event

This section assumes nothing. Follow it top to bottom. Nothing gets installed
and nothing is written outside the folder you unpack.

## Step 1: download the Vibe-Game Pack

Go to the [jdBasic releases page](https://github.com/AtomiJD/jdBasic/releases/latest)
and download

```
jdbasic-vibe-game-pack-windows-x64.zip
```

Unzip it anywhere you like, for example `C:\jdbasic\`. It contains
`jdBasic.exe` plus everything it needs, including the Visual C++ runtime, so
there is nothing else to install.

Open `BUILD_INFO.txt` in the unpacked folder and check the `Features:` line. It
must mention **SQLite**:

```
Features: HTTP, GFX, ImGui, FX, SQLite, MCP
```

If it does not, you have an older pack. Download the current one, the app
cannot run without SQLite.

## Step 2: put the app into the pack

Download `bfwm.jdb` and `bfwm.db` from this folder. In the unpacked pack, make a
new folder called `bfwm` and put both files in it, next to the `games` folder:

```
C:\jdbasic\
    jdBasic.exe
    games\
    bfwm\
        bfwm.jdb
        bfwm.db
```

`bfwm.db` is optional. If it is missing, the app creates an empty one the first
time it starts.

## Step 3: make a launcher you can double-click

The pack already has `run-pacman.bat` and friends. Make one for this app the
same way. Create a text file next to `jdBasic.exe`, call it `run-bfwm.bat`, and
put these three lines in it:

```
@echo off
cd /d "%~dp0\bfwm"
"%~dp0\jdBasic.exe" bfwm.jdb
```

Double-click it. A window titled "Black Forest Games" opens.

Press **F11** for fullscreen, which is what you want on a TV or a projector at
the event.

If you would rather not make a file, open a command prompt in the pack folder
and type `jdBasic.exe bfwm\bfwm.jdb` instead. It does the same thing.

## Step 4: set up your event (do this the day before)

**Settings tab.** Type the name of your event and the date. Both go on every
certificate, so write them the way they should be printed, for example
"Tannenhof Garden Olympics 2026" and "August 15, 2026". They save themselves as
you type.

**Games tab.** Add every discipline. Each one gets a number (the order you will
play them in), a name, and how many points 1st, 2nd and 3rd place are worth.
Leave the points at 3 / 2 / 1 unless a game should count for more. A grand
finale at 6 / 4 / 2 is worth double and can still turn the whole standings
around.

**Participants tab.** Add everyone. Number and name are what you need, title
and extra field are optional. The title is printed on the certificate under the
name, so it is a good place for something friendly: "Defending Champion",
"Grill Master", "Rookie of the Year". The extra field is only for you, use it
for a team or a phone number.

You can fix anything later with the **Edit** button in any row.

## Step 5: during the event

Stay on the **Dashboard**. After each game:

1. pick the game,
2. pick the participant,
3. pick the place they came in,
4. press **Record**.

The standings above re-sort instantly, so the screen is always current. Do this
three times per game (1st, 2nd, 3rd) and move on.

Recorded something wrong? The **Recent results** list at the bottom has a
**Delete** button on every line. Delete it and record it again. Points are
recalculated from scratch every frame, so nothing can drift out of sync.

## Step 6: certificates

At the end, press **Certificate** in the standings row of everyone who should
get one. Each press writes a PDF into the `bfwm` folder, named like
`certificate_1_Mara_Feldkirch.pdf`.

Open them with any PDF viewer and print them on A4, landscape. Places 1, 2 and 3
get a gold, silver and bronze medal; everyone below gets bronze-coloured, so
handing one to every participant works fine.

## Where your data lives, and starting over

Everything is in `bfwm.db` next to the script. Copy that one file to back up
your event, or to move it to another computer. To start a new event from
scratch, close the app, delete `bfwm.db`, and start again.

---

## Linux and macOS

The Vibe-Game Pack is Windows only. On Linux and macOS, build jdBasic yourself
with graphics, ImGui and SQLite:

```
GFX=1 IMGUI=1 SQLITE=1 ./build.sh
./build/jdbasic jdb/demos/bfwm/bfwm.jdb
```

SQLite needs the amalgamation: download `sqlite3.c` and `sqlite3.h` from
[sqlite.org](https://sqlite.org/download.html) into `bridges/sqlitebridge/`
first, the build tells you so if they are missing. Then check with
`./build/jdbasic --version` that the `Features:` line lists GFX, ImGui and
SQLite. Steps 2 to 6 above are the same, minus the `.bat` launcher.
