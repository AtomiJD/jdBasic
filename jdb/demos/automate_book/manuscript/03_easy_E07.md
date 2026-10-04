<!-- pagebreak -->

## E07 Time Tracker

### The chore

Mia works for four projects at once, and every Friday the office asks
how her hours split between them. She rebuilds the week from her
calendar and her memory, guesses the rest, and spends half an hour on
a number nobody fully trusts.

### What you get

Two short commands mark when work on a project starts and stops. The
Time Tracker writes each of them as a line in a log file, and on
Friday it turns the week into an Excel sheet: one row per project, one
column per day, the totals at the end.

### Before you start

The setup wizard has run and the Time Tracker is switched on. The
wizard planned the weekly sheet for Friday afternoon; starting and
stopping is up to you.

### The program

The program reads the command and does one of four things:

<!-- include recipes/easy/E07_time_tracker/time_tracker.jdb -->

The module keeps the log, turns it into blocks of work and adds up the
hours:

<!-- include recipes/easy/E07_time_tracker/tracker.jdb -->

### How it works

1. `RECORD` adds one line to the log, such as
   `2026-10-01 09:00:00,start,Miller offer`. The log only ever grows,
   so nothing is lost when the program stops halfway. A name with a
   comma goes in quotes, `"Miller, offer"`, so it stays one field.
2. `EVENTS` reads the log with `CSVREADER`. The list of column types
   keeps every field as text, so a date stays a date as written.
3. `SESSIONS` pairs the events into blocks of work. A `start` while
   another project runs ends that one first, so switching projects is a
   single command.
4. `WEEK_TABLE` adds up the minutes per project and day from Monday on,
   with `DATEDIFF` in minutes. A block that still runs counts until now.
5. `SHEET_ROWS` lays the table out with a header row, the totals per
   project in the last column and the totals per day in the last row;
   `WRITE_SHEET` saves it with the XLSX library, hours in two decimals.

### Run it

```
jdbasic time_tracker.jdb start Miller offer
Started Miller offer at 09:02.
jdbasic time_tracker.jdb start Admin
Stopped Miller offer.
Started Admin at 11:30.
jdbasic time_tracker.jdb week
Admin: 0.50 h
Miller offer: 2.47 h
Total: 2.97 h
Wrote C:\Users\mia\Documents\AutomateWork\time\week 2026-09-28.xlsx
```

`status` tells you what runs and for how long. `week --dry-run` shows
the totals without writing the sheet.

### Schedule it

The wizard plans `time_tracker.jdb week` for Friday at 16:00, so the
sheet is ready before the week ends. The `start` and `stop` commands
are yours: put them on desktop shortcuts, one per project you work on
often.

### Make it yours

```toml
[time_tracker]
folder = "~/Documents/AutomateWork/time"
```

- **A shared folder**: point `folder` at a folder your team lead can
  open, and the weekly sheets land there on their own.
- **Project names with spaces** work as they are:
  `start Miller offer` starts "Miller offer".
- **Open the log in Excel**: `log.csv` is a plain CSV file. Correct a
  forgotten stop there, then run `week` again.

### When it goes wrong

- **A block that lasted all night**: you forgot `stop`. Edit the time
  of the next line in `log.csv`, or add a `stop` line with the right
  time.
- **A block over midnight** counts on the day it started.

> **Balance dividend**
> About 30 minutes a week of rebuilding your hours, and numbers you can
> stand behind when someone asks.
