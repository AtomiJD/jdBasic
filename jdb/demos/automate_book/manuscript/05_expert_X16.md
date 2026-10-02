<!-- pagebreak -->

## Bonus: X16 Desktop Robot

### The chore

Every Friday Lena moves the corrections of the week from the bookings
report into an old cost centre program. The company that wrote it is
long gone, and the program can neither import a file nor talk to
other programs. Forty corrections, each a window to open, four
fields to fill and a button to press. A macro of the kind M16 plays
would save the typing, but Lena does not trust a macro that clicks
blindly into a program that sometimes opens a different dialog.

### What you get

The Desktop Robot works the program from a job file. It waits for the
window, brings it to the front, and before it clicks it looks at the
screen: a region of the window has to match a reference picture, so a
click only happens when the right button is where it should be. Every
step may be tried again, every step writes a line to the log, and a
job that fails ends with an error the job server of X01 notices.

### Before you start

The setup wizard has run. Copy `order_entry.toml` and `new_button.png`
from the recipe's folder into `~/Documents/AutomateWork/robot`, the
folder the `job` setting names, and change the job for your program.
The points of a job count from the top-left corner of the window's
inside, so a job still works when the window moves. Measure them with
`jdbasic macro_player.jdb --where` (M16): point at the top-left corner
of the window's inside first, then at each place, and subtract the
first position from the others.

For each check, take a reference picture of the region once, while
the program shows what the robot should see. A few lines do it; the
numbers are the region of the check:

```basic
IMPORT IMG
DIM title$ = "Order Entry - Version 4"
DIM rc = OS.SCREENSHOT("C:/temp/window.png", "client", title$)
DIM shot = IMG.READPNG("C:/temp/window.png")
DIM button = IMG.CROP(shot, 12, 44, 96, 24)
IMG.WRITEPNG(button, "new_button.png")
```

> **Watch out**
> While a job runs, the robot takes over your keyboard and your mouse.
> To stop it at once, hold Esc, or push the mouse into the top-left
> corner of the screen; it checks both before every single action.
> Windows delivers key strokes and clicks only to an unlocked screen
> with you logged in, so a job needs a computer that is on, unlocked
> and nobody else's to use. Do not leave a screen unlocked for a robot
> to run. Ask your IT department first: some companies forbid programs
> that type and click for you, and some will rather give you a computer
> of its own for this.

### The program

The program reads the settings and the job, and either shows the steps
or runs them with a log:

<!-- include recipes/expert/X16_desktop_robot/desktop_robot.jdb -->

A job is a TOML file; the comment at its top lists every kind of step:

<!-- include recipes/expert/X16_desktop_robot/order_entry.toml -->

The module reads the job, compares screen regions and runs the steps:

<!-- include recipes/expert/X16_desktop_robot/robot.jdb -->

The emergency stop lives in the shared module `inputkit.jdb`. Before
every single action it asks Windows for the state of Esc and the
position of the mouse:

<!-- include recipes/lib/inputkit.jdb from="EXPORT FUNC SAFE$" to="ENDFUNC" -->

<!-- include recipes/lib/inputkit.jdb from="EXPORT SUB GUARD" to="ENDSUB" -->

### How it works

1. `ROBOT.LOAD` reads the job and checks every step: a kind it does not
   know, a region without four numbers, a missing picture or a key
   that closes a window, such as Alt+F4, is a mistake, and a job with a
   mistake does not run.
2. `wait_window` waits for a window with exactly the job's title, as
   long as `timeout_s` says, and brings it to the front.
3. Every check, click, text and key first finds the window again and
   brings it to the front, so a dialog that popped up in between
   cannot catch the robot's key strokes.
4. `check` takes a picture of the window's inside with `OS.SCREENSHOT`
   and compares the region with the reference picture. `REGION_DIFF`
   answers the mean difference of the red, green and blue values, from
   0 for the same picture to 255; a check passes up to its
   `tolerance`. A region outside the window or a picture of another
   size never passes.
5. `click` adds the window's position on the screen to the point of
   the job, so the click lands in the same place of the program
   wherever the window is.
6. A step that does not work is tried again after a second, up to its
   `tries`. A check with five tries waits up to five seconds for a
   slow dialog. When the tries are used up, the job ends.
7. Every try writes a line to `logs/desktop_robot.log`. The program
   ends with exit code 1 when the job stopped, which is what the job
   server of X01 counts as a failed run.

### Run it

Look at the job first:

```
jdbasic desktop_robot.jdb --dry-run
Job order_entry.toml for 'Order Entry - Version 4':
  step 1: wait up to 60 s for 'Order Entry - Version 4'
  step 2: check 96 x 24 at 12, 44 against new_button.png, 5 tries
  step 3: click at 60, 56
  step 4: pause 800 ms
  step 5: type 'Müller GmbH'
  step 6: press tab
  step 7: type 'Copy paper, 10 packs'
  step 8: press ctrl+s, 2 tries
8 steps. Without --dry-run the robot runs them.
```

Then open the program, start the robot without `--dry-run`, and watch
the first runs with your hands off the keyboard. `--job` runs another
job file, such as `--job ~/Documents/AutomateWork/robot/booking.toml`.

### Schedule it

Under the job server of X01, the robot is a job like any other. Add a
block to `jobs.toml`:

```toml
[[job]]
name = "orders robot"
when = "weekdays 12:30"
program = "X16_desktop_robot/desktop_robot.jdb"
attempts = 1
catch_up = false
```

One attempt, because a job that stopped halfway has entered half an
order; a second run would enter it again. `catch_up = false`, because
a robot should not start on its own when you come back to a computer
that was off at half past twelve. A failed run lands in the job state,
where the health checks of X13 find it and, with their alerts on,
tell you by mail.

### Make it yours

The settings are the `[desktop_robot]` part of `work.conf`:

```toml
[desktop_robot]
job = "~/Documents/AutomateWork/robot/order_entry.toml"
countdown = 5
```

- **A job per chore.** Keep one job file per chore next to its
  pictures, and start the others with `--job`.
- **Check before every click that matters.** A `check` step costs a
  second and saves you from a click on the wrong button. Put one
  before every click that changes data.
- **Tolerance.** A tolerance of 10 to 15 allows for a blinking cursor
  or a different colour of the window frame. If a check fails with a
  difference of 2 or 3, raise it; if it passes on the wrong screen,
  lower it and pick a region with more detail.
- **Rows from a sheet.** For many rows, build the job's text from the
  rows of a sheet and hand it to `ROBOT.PARSE_JOB`, one job per row,
  with a `check` at the start of each.

### When it goes wrong

- **"the region differs by 41.3"**: the program looks different from
  the reference picture. The display scaling, the Windows theme or a
  new version of the program changes every pixel; take the reference
  picture again.
- **"the window did not come"**: the program was not open within
  `timeout_s`, or its title changed. Check the title bar letter by
  letter.
- **"it stayed behind"**: Windows kept another window in front, which
  it does when the screen is locked or another program demands
  attention. Look at the screen and run the job again.
- **The job stopped halfway**: the log names the last step that
  worked. Finish that entry by hand before you start the robot again;
  the robot has no undo.

> **Balance dividend**
> About 60 minutes a week for forty corrections, entered the same way,
> with a log that shows each one.
