<!-- pagebreak -->

## Bonus: M16 Macro Player

### The chore

Jonas's team orders its supplies through an old order program. It has
no import and no interface for other programs, so every order is typed
in by hand: open a new order, enter the supplier, the article and the
person who ordered it, save. Twenty orders a week, each one the same
dozen clicks and key strokes in the same places.

### What you get

You write down the clicks and key strokes once, in a small text file
called a macro. The Macro Player plays it: it brings the window to the
front, types, presses keys and clicks where you said. It writes a log
line per step and stops at the first step that goes wrong, so it never
types into the wrong window.

### Before you start

The setup wizard has run, and the folder of your macros exists:
`~/Documents/AutomateWork/macros` unless you chose another one. Copy
the sample `new_order.txt` from the recipe's folder into it and change
it for your program. To find the points your macro clicks, start
`jdbasic macro_player.jdb --where`, point at each place in turn, and
read its position on the screen.

> **Watch out**
> While a macro plays, the Macro Player takes over your keyboard and
> your mouse. Keep your hands off both until it is done. To stop it at
> once, hold Esc, or push the mouse into the top-left corner of the
> screen; it checks both before every single step. Never run a macro on
> a locked screen or one others can see or use, and ask your IT
> department first: some companies forbid programs that type and click
> for you.

### The program

The program finds the macro you name, checks it and plays it:

<!-- include recipes/medium/M16_macro_player/macro_player.jdb -->

The module knows the folder of macros, the command line and the log:

<!-- include recipes/medium/M16_macro_player/macros.jdb -->

A macro is plain text, one command per line:

<!-- include recipes/medium/M16_macro_player/macros/new_order.txt -->

The shared module `inputkit.jdb` reads it. Its `PARSE` checks every
line and answers the steps, or a list of mistakes with their lines:

<!-- include recipes/lib/inputkit.jdb from="EXPORT FUNC PARSE" to="ENDFUNC" -->

### How it works

1. `NAME_ARG$` takes the first word on the command line that is no
   option, and `PATH$` turns it into a file of the macro folder. A
   name with a folder in it is refused, so a macro always comes from
   your folder.
2. `LOAD` reads the file and `INPUTKIT.PARSE` checks it. A command it
   does not know, a click without two numbers, a pause of more than a
   minute or a key that closes a window, such as Alt+F4, is a mistake.
   With one mistake in the file, nothing is played.
3. `COUNTDOWN` gives you a few seconds to take your hands off.
4. `PLAY` carries out the steps one after the other. `window` brings a
   window to the front by its exact title, `type` types the rest of
   the line, `key` presses a key or a combination, `click` clicks at a
   point of the screen, `wait` pauses.
5. Every step writes a line to `macro_player.log` in the macro folder.
   The first step that fails, such as a window that is not open, ends
   the run, and its line says why.

### Run it

Look at the steps first:

```
jdbasic macro_player.jdb new_order --dry-run
would line 3: bring 'Orders - Main Menu' to the front
would line 4: press ctrl+n
would line 5: wait 800 ms
would line 6: type 'Miller Ltd' (10 characters)
would line 7: press tab
would line 8: type 'Copy paper, 10 packs' (20 characters)
would line 9: press tab
would line 10: press tab
would line 11: type 'Jonas Example' (13 characters)
would line 12: click at 640, 520
would line 13: wait 300 ms
would line 14: press ctrl+s
12 steps. Run without --dry-run to play them.
```

Then open the order program and play it for real with
`jdbasic macro_player.jdb new_order`. `--list` shows the macros of your
folder. The log tells what happened:

```
2026-10-02 09:35:12  new_order  line 13: wait 300 ms  dry run
2026-10-02 09:35:12  new_order  line 14: press ctrl+s  dry run
```

### Schedule it

A macro plays when you start it, because it needs the program open and
your hands off the keyboard. Give each macro a shortcut on the desktop
that runs `jdbasic macro_player.jdb new_order`, or a button in the Work
Cockpit (X06).

### Make it yours

The settings are the `[macro_player]` part of `work.conf`:

```toml
[macro_player]
folder = "~/Documents/AutomateWork/macros"
countdown = 5
```

- **A macro per chore.** Write one file per chore, such as
  `new_order.txt` and `close_month.txt`, and play each by its name.
- **Waits after dialogs.** A program needs a moment to open a dialog.
  Put a `wait 500` after every step that opens one; the macro is no
  faster than the program.
- **Keys before clicks.** `key tab` and `key ctrl+n` work wherever the
  window is. A click needs the window in the same place every time, so
  prefer keys where the program offers them.
- **Your own data.** The Form Filler (E16) builds a macro from a list
  of values; the same idea works for a list of orders, one macro per
  row of a sheet.

### When it goes wrong

- **"no window 'Orders - Main Menu'"**: the title is not exactly the
  one in the title bar, or the program is not open. Copy the title
  letter by letter; a version number in it counts.
- **The macro has mistakes**: the player names each line and stops
  before it plays anything:

```
jdbasic macro_player.jdb broken --dry-run
The macro has mistakes:
  line 1: unknown command 'clik'
  line 2: 'q' is not ctrl, shift, alt or win
```

- **Clicks land in the wrong place**: the window moved, or the display
  scaling of Windows changed. Run `--where` again and correct the
  points, or use keys instead of clicks. The Desktop Robot (X16)
  counts its points from the window, so they survive a moved window.
- **"Esc is held: stopped."**: you pressed Esc, or a key got stuck.
  Finish the order by hand; a macro has no undo.

> **Balance dividend**
> About 30 minutes a week for twenty orders, typed the same way every
> time.
