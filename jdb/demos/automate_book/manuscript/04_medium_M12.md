<!-- pagebreak -->

## M12 Meeting Cost Meter

### The chore

Jonas leads a team of six. His calendar is full, and every quarter
someone asks him why the project is late. He suspects the answer is in
his calendar: the weekly call that once took thirty minutes and now
takes an hour, the status meeting with eight people where two would do,
the review that repeats every second Tuesday although nobody remembers
why. He has never added it up. Adding up a month of calendar entries by
hand takes an evening, and by the time it is done the next month has
started.

### What you get

Every Friday afternoon the Meeting Cost Meter reads your calendar for
the last four weeks and answers three questions. How many hours went
into meetings, week by week and month by month? What did that time
cost, your own time and the time of everyone in the room? Which meeting
series take the most of it? The answer is printed as a short report and
saved as a workbook with one sheet for the weeks, one for the months
and one for the series, so you can sort, filter and put it in front of
whoever asked.

### Before you start

The recipe reads a calendar file in the iCalendar format (`.ics`). Every
calendar program can write one:

- **Outlook**: *File*, *Save Calendar*, choose a date range of at least
  the last month and *Full details*.
- **Google Calendar**: *Settings*, *Import and export*, *Export*.
- **A shared calendar with a link**: download the `.ics` file from the
  link.

Save the file in your work folder as `calendar.ics`, or tell the recipe
where it is. Chapter 5 shows how to fetch it automatically from the
calendar server; for now, a fresh export once a month is enough.

### The program

The program reads the settings, picks the weeks to look at, and leaves
the counting to the module:

<!-- include recipes/medium/M12_meeting_cost/meeting_cost.jdb -->

The module turns calendar entries into hours and the hours into a
report and a workbook:

<!-- include recipes/medium/M12_meeting_cost/meetcost.jdb -->

### How it works

The recipe is built from four small steps, each in a function of its
own. Reading them in order is a good way to see how a medium recipe is
put together.

1. **From a file to meetings.** `ICAL.PARSEFILE` reads the calendar, and
   `ICAL.EXPAND` turns it into one row per meeting in the time range.
   This is the important part: a weekly call is stored in the file once,
   with a rule that says *every week*. `EXPAND` applies the rule, so a
   call that met four times becomes four rows. `MEETINGS` then keeps
   what it needs from each row: the title, the start, the length in
   hours, and the number of people invited. All-day entries, such as a
   public holiday or an offsite, are left out, since they are not
   meetings in the sense of this recipe.

2. **From meetings to totals.** `SUMMARY` walks the list once and adds
   every meeting to three maps at the same time: one keyed by week, one
   by month and one by title. A map is the natural tool here: the key is
   the label you want in the report, and `AddTo` creates the entry the
   first time a key turns up. *Person hours* are the hours of a meeting
   times the people in it; an hour with six people costs six hours of
   work.

3. **Weeks the way calendars count them.** `WEEK$` uses `DT.ISOWEEK`,
   the ISO 8601 week that European calendars print: weeks start on
   Monday, and week 1 is the week with the first Thursday of the year.
   That is why 1 January 2027 belongs to week 53 of 2026. Counting weeks
   yourself goes wrong at exactly these edges, so leave it to the
   library.

4. **From totals to answers.** `TOP_SERIES` takes the hours of each
   title with a `SELECT` and sorts the titles by them. It does that
   with `GRADE`, which answers the order in which the values would be
   sorted; negating the hours turns *smallest first* into *largest
   first*, and a second `SELECT` turns the positions back into titles.
   `REPORT$` and `WRITE_XLSX` then present the same summary twice,
   once as text and once as a workbook. `REPORT$` makes the lines per
   week and per month with a `SELECT` each, and `TAKE` keeps the
   first `top` series.

Each function takes what it needs as parameters and returns its result.
None of them reads settings or prints anything. That is what makes the
module testable: the test gives it a calendar file and a fixed range and
checks the numbers.

### Run it

```
jdbasic meeting_cost.jdb
Since 2026-09-28
Meetings: 7.5 hours, 450 EUR of your time
Everyone in them: 23.0 hours, 1380 EUR

By week
  2026-W41     2.5 h
  2026-W42     1.0 h
  2026-W43     3.0 h
  2026-W44     1.0 h

By month
  2026-10      7.5 h

Largest series
     4.0 h  Weekly team call (4 times)
     2.0 h  Budget review (once)
     1.5 h  Miller project status (once)

Workbook: C:\Users\jonas\Documents\AutomateWork\meeting_cost.xlsx
```

This is the small calendar the recipe's test uses. On a real calendar
the list of series is longer, and the weekly rows show at a glance
which weeks were the full ones. With `--dry-run` the recipe prints the report and writes no
workbook.

### Schedule it

The wizard plans the meter for Friday at 15:00, which leaves time to
look at the numbers before the next week is planned. Remember to export
a fresh calendar file before, or let Chapter 5 do that for you.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[meeting_cost]`:

```toml
[meeting_cost]
calendar = "~/Documents/AutomateWork/calendar.ics"
hourly_rate = 60
currency = "EUR"
weeks = 4
top = 5
workbook = "~/Documents/AutomateWork/meeting_cost.xlsx"
```

`hourly_rate` is the cost of an hour of work. Ask your controlling team
for the rate they use. Without one, pick a round figure you can defend
and say so when you show the numbers; the point is the comparison
between weeks and series, not the exact amount.

Beyond the settings, three changes in the code are worth knowing:

- **Leave out meetings that are not meetings.** Focus time blocked in
  the calendar is counted as a meeting. To skip every entry whose title
  starts with `Focus`, add one condition in `MEETINGS`, right after
  `IF NOT ICAL.ALLDAY(cal, ev) THEN`:

  ```basic
  IF STARTSWITH(r{"summary"}, "Focus") THEN CONTINUEFOR
  ```

- **Count only meetings with others.** Change the line
  `IF people < 1 THEN people = 1` into
  `IF people < 2 THEN CONTINUEFOR`. Entries with nobody invited, the
  reminders you set for yourself, then drop out.

- **Group by person instead of by title.** `SUMMARY` adds every meeting
  to a map keyed by its title. Use the first attendee instead
  (`ICAL.VALUES(cal, ev, "ATTENDEE")[0]`, kept in `MEETINGS` under a new
  key), and the series list becomes a list of the people you spend the
  most meeting time with.

### When it goes wrong

- **"Set meeting_cost.calendar first"**: the wizard did not know where
  your calendar file is. Enter the path in `work.conf`.
- **No meetings at all**: the export covered a different range. Check
  that the file reaches back the number of `weeks` the recipe looks at.
- **A series is counted under two titles**: someone renamed it. The
  recipe groups by title, so "Team call" and "Weekly team call" are two
  series. Rename them in the calendar or in the workbook.
- **Times are an hour off**: the export uses a time zone the recipe
  reads as UTC. Export again with the time zone included (Outlook does
  this with *Full details*).

> **Balance dividend**
> About 15 minutes a week of looking at the calendar, and the first
> honest answer to the question where the team's time goes.
