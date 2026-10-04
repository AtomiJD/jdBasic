<!-- pagebreak -->

## M05 Shift and Vacation Planner

### The chore

Jonas leads a team of nine. The plan of who works the early shift, who
is on vacation and who is at a training lives in an Excel file on the
team drive. Everyone has their own copy of the truth: Ann writes her
vacation into Outlook, Ben looks at the sheet once a month, and Cem
asks in the chat whether Dora is in on Friday.

Twice a month Jonas finds out too late that three people are away on
the same day. Keeping the sheet and a dozen calendars in line costs him
about half an hour every week, and the clashes cost more.

### What you get

The sheet stays the one place where the plan is made. Every morning the
planner reads it and writes a calendar file next to it. Everyone in the
team subscribes to that file once, in Outlook, in Google Calendar or on
the phone, and from then on sees the team plan beside their own
appointments. When the sheet changes, the calendar changes with it.

On every run the planner also checks the plan and names the days on
which more people are away than the team can spare.

### Before you start

The plan is an Excel file with one sheet and one row per entry. The
first row holds the column names, the rest holds the entries:

| Name | Kind | From | To | Note |
|---|---|---|---|---|
| Ann | Vacation | 2026-10-19 | 2026-10-23 | |
| Ben | Vacation | 2026-10-20 | 2026-10-21 | |
| Cem | Training | 2026-10-21 | | Excel course |
| Dora | Early shift | 2026-10-19 | 2026-10-23 | |

*From* and *To* can be real Excel dates or text such as `2026-10-19`
or `19.10.2026`. An entry without *To* lasts one day. Rows without a
name or a start date are left out, so notes below the table do no harm.

The calendar file has to go to a place everyone can reach: a shared
folder that is published on the intranet, a SharePoint library or a
folder your calendar program can subscribe to. Ask IT which one your
office uses; Outlook and Google Calendar both subscribe to an `.ics`
file by its address.

### The program

The program reads the settings and the sheet, asks the module for the
calendar and the clashes, and writes or shows the result:

<!-- include recipes/medium/M05_shift_planner/shift_planner.jdb -->

The module does the work. It turns cells into days, rows into entries,
entries into calendar events, and counts who is away on which day:

<!-- include recipes/medium/M05_shift_planner/shifts.jdb -->

### How it works

1. `XLSX.READ` answers every sheet of the workbook as a list of rows,
   each row a list of cells. Text comes back as text, a date as the
   number Excel stores for it: the days since the end of 1899.
2. `SHIFTS.DAY$` turns any of those into `YYYY-MM-DD`. A number is
   added as days to 1899-12-30, which is where Excel's count starts. A
   text is accepted when it looks like `2026-10-19` or `19.10.2026`;
   the regular expressions in `REGEX.FINDALL` check that. Everything
   else becomes `""`, which `ENTRIES` reads as "no date".
3. `SHIFTS.ENTRIES` skips the header row and builds one map per row
   with a name and a start. It also keeps the row number, so a warning
   can point to the line in the sheet.
4. `SHIFTS.CALENDAR` makes one all-day event per entry with
   `ICAL.ADDALLDAY`. The number of days comes from `DT.DIFF`, plus one,
   because an entry from Monday to Friday covers both days.
5. Each event gets a UID, the name calendar programs use to recognise
   an event. `Uid$` makes it from the name, the kind and the start
   with `CODEC.SHA256$`, so the same entry has the same UID on every
   run. Without that, every subscriber would see the vacation twice
   after the next update.
6. `SHIFTS.CLASHES` lowercases the kinds that mean away with a
   `SELECT` and keeps the absences among the entries with a `FILTER`.
   A loop walks through every day of every absence and collects the
   names per day in a map. A second `FILTER` keeps the days with more
   names than `max_away`, a `SELECT` turns each into a map with "day"
   and "names", and the program prints them.

### Run it

With `--dry-run` the planner reads the sheet, prints the warnings and
the entries, and writes nothing:

```
jdbasic shift_planner.jdb --dry-run
4 entries in plan.xlsx
too many away on 2026-10-21: Ann, Ben, Cem
  2026-10-19 to 2026-10-23  Ann: Vacation
  2026-10-20 to 2026-10-21  Ben: Vacation
  2026-10-21 to 2026-10-21  Cem: Training
  2026-10-19 to 2026-10-23  Dora: Early shift
Nothing written. Run without --dry-run to write S:/Team/team.ics
```

Without the switch it writes the calendar file and tells you where.

### Schedule it

The wizard plans the planner for 07:00 every day, before the team
starts. If the sheet changes often during the day, change the schedule
to `every 2 hours`.

### Make it yours

The settings are the `[shift_planner]` part of `work.conf`:

```toml
[shift_planner]
plan_file = "C:/Users/jonas/Team/plan.xlsx"
calendar_file = "S:/Team/Calendar/team.ics"
sheet = "Plan"
title = "Sales team"
away_kinds = ["Vacation", "Sick", "Training"]
max_away = 2
```

Three changes in the code that teams ask for:

- **Early shifts count as away from the office.** Add the kind to
  `away_kinds` in `work.conf`; no code needed. Kinds match in any
  case, so `early shift` in the sheet counts too.
- **A reminder the evening before a vacation.** In `CALENDAR`, after
  the line with `ICAL.ADDALLDAY`, add an alarm for the people who
  subscribe:

  ```
  IF LCASE$(e{"kind"}) = "vacation" THEN
      DIM alarm = ICAL.ADDALARM(cal, ev, 12 * 60)
  ENDIF
  ```

  The number is minutes before the start; twelve hours before midnight
  is the noon of the day before.
- **One calendar per person.** In the program, before the line that
  writes the calendar, filter the entries for a name and write a second
  file:

  ```
  DIM mine = FILTER(LAMBDA e -> e{"name"} = "Ann", entries)
  ICAL.WRITEFILE(SHIFTS.CALENDAR(mine, "Ann"), "ann.ics")
  ```

### When it goes wrong

- **"The plan has no sheet called Plan"**: the sheet in your workbook
  has another name. Set `sheet` in `work.conf` to the name on its tab.
- **An entry is missing from the calendar**: its row has no name, or a
  start that is neither an Excel date nor written like `2026-10-19` or
  `19.10.2026`. A date such as `Oct 19` is text Excel did not
  recognise; type it again as a date.
- **Subscribers see old data**: calendar programs fetch a subscribed
  file on their own timetable, Outlook every few hours, Google up to
  once a day. The file itself is current after each run.

> **Balance dividend**
> About 30 minutes a week of keeping calendars in line, and the
> clashes found when there is still time to talk about them.
