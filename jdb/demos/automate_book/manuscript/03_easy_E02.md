<!-- pagebreak -->

## E02 Morning Launcher

### The chore

Every morning Mia opens the same things in the same order: the mail in
the browser, the project folder, the planning sheet, the team chat. On
Mondays the planning tool comes first, on Fridays the week report.
Twenty clicks before the first real piece of work, and on a busy morning
she forgets one and notices it only when somebody asks.

### What you get

When the workday starts, the Morning Launcher opens everything on your
list: programs, folders, files and web pages. Each weekday can add its
own things to the list you open every day.

### Before you start

The setup wizard has run and the Morning Launcher is switched on. The
wizard asks you for the first few things to open; you can add more
later in `work.conf`.

### The program

The program reads your lists, asks the launcher for today's plan and
opens it:

<!-- include recipes/easy/E02_morning_launcher/morning_launcher.jdb -->

The launcher builds the plan. It knows the weekdays and which program
opens a target on your system:

<!-- include recipes/easy/E02_morning_launcher/launcher.jdb -->

### How it works

1. `WORKCONF.VALUE` reads the `[morning_launcher]` part of `work.conf`
   as one map of lists.
2. `FORMAT_DATE(NOW(), "%w")` gives the weekday as a number, 0 for
   Sunday, and `DAY_KEY$` turns it into the key of that day's list,
   such as `mon`.
3. `PLAN` puts the `every_day` list first and the day's own list after
   it, and opens each target only once, even when both lists name it.
4. Every target is opened by the program that opens things on your
   system: Explorer on Windows. Explorer starts a program, shows a
   folder, opens a file with the program it belongs to and a web
   address in your browser.
5. `START` calls `OS.EXEC` with the opener and the target as an
   argument list. A target is never joined into a command line, so a
   folder called `R&D` opens as a folder and runs nothing else.

### Run it

```
jdbasic morning_launcher.jdb --dry-run
would open https://mail.example.com
would open C:\Work\Projects
would open C:\Tools\planner.exe
3 things would open. Run without --dry-run.
```

### Schedule it

The wizard plans the Morning Launcher for every workday at 08:00, or for
the moment you log in, whichever you pick on the page "Schedule".

### Make it yours

```toml
[morning_launcher]
every_day = ["https://mail.example.com", "~/Documents/Projects"]
mon = ["C:/Tools/planner.exe"]
fri = ["~/Documents/Reports/week.xlsx"]
```

- **Add a day**: the keys are `mon`, `tue`, `wed`, `thu`, `fri`, `sat`
  and `sun`.
- **Open a file**: name it with its path; it opens in the program it
  belongs to.
- **Open a program**: name its `.exe` file or a shortcut (`.lnk`) from
  the start menu.

### When it goes wrong

- **Nothing opens**: run with `--dry-run`. An empty plan means the lists
  in `work.conf` are empty for today, or the section is misspelled.
- **A program does not start**: open its path in Explorer by hand. If
  that fails too, the path is wrong; copy it from the program's
  shortcut.
- **Everything opens twice**: the scheduler and your login both start
  the launcher. Keep one of the two in the wizard.

> **Balance dividend**
> About 10 minutes a week, and a calmer first quarter of an hour.
