<!-- pagebreak -->

## M15 Timesheet for the Boss

### The chore

On the first working day of every month, Jonas owes his department head
a timesheet: the hours of the last month, week by week and per
project, signed. He tracks his time with the Time Tracker of E07, so
the numbers exist. Still, he opens the log in Excel, filters the month,
builds a pivot table, copies it week by week into the company's Word
template, adds up the totals, prints, signs, scans and mails it. That
is an hour of work on a document whose content was finished the
moment the month ended.

### What you get

Once a month the recipe reads the Time Tracker's log and writes last
month's timesheet as a Word file:

- a table for every week that has work in it, one row per project and
  one column per day, with the totals of each day and each project,
- a table with the totals per project for the whole month,
- lines for your signature and your boss's approval.

It also writes the mail that carries the timesheet to your boss and
puts it into the outbox, by the outbox rule of this chapter. You look
at it, and send it with `--send` when you are ready.

### Before you start

The Time Tracker (E07) has been running for a month. The recipe
finds its log on its own: `log.csv` in the folder you gave E07 in the
wizard, the `folder` of the `[time_tracker]` part of `work.conf`.
The `[mail]` part holds your mail server, as the outbox rule
describes, and the recipe's own part names your boss:

```toml
[timesheet]
boss = "Anna Berg <anna.berg@example.com>"
```

Without `boss` the recipe still writes the timesheet, and only the
mail is left out.

### The program

The program decides which month to write, writes it once and puts the
mail into the outbox:

<!-- include recipes/medium/M15_timesheet/monthly_timesheet.jdb -->

The module turns the log into weeks, tables and the Word file:

<!-- include recipes/medium/M15_timesheet/timesheet.jdb -->

### How it works

1. **From the log to blocks of work.** The log holds one line per
   start or stop. `EVENTS` reads it with `CSVREADER`, every column as
   text, and `SESSIONS` turns the lines into blocks with a project, a
   start and a stop. It follows the rules of the Time Tracker: a start
   while another project runs ends that one, and a block without a stop
   is still running. `Hours` counts a running block until now, so a
   forgotten `stop` shows up as a long day instead of a missing one.

2. **Which month.** `LAST_MONTH$` takes the first day of the current
   month and goes back one day; the month of that day is the one
   before. That works in January as well, where *the month before* is
   December of the previous year.

3. **Weeks that start in another month.** A month rarely starts on a
   Monday. `MONDAY$` finds the Monday of the week a day belongs to,
   with the number `FORMAT_DATE` gives for `%w`: 0 for Sunday, 1 for
   Monday and so on. `WEEKS` starts at the Monday of the week with the
   first of the month and steps forward seven days at a time while the
   Monday is still in the month. October 2026 starts on a Thursday, so
   its first week begins on 28 September.

4. **Only the days of the month.** `WEEK_HOURS` adds the hours of every
   block to its project and its day of the week, but only for days in
   the month. The September days of that first week stay empty, so the
   work on them is not counted twice, once in September's sheet and
   once in October's. `WEEK_ROWS` turns the hours into a table: a
   header with the day and the date (no date for the days outside the
   month), a row per project, and the totals.

5. **The document.** `DOCUMENT` writes the title, your name, a heading
   and a table for every week that has work in it, the totals of the
   month from `TOTAL_ROWS`, and the two signature lines. It answers the
   hours of the month, which the mail mentions.

6. **The mail.** `MESSAGE` writes a short text to your boss and
   attaches the file. The program puts it into the outbox with
   `OUTBOX.PUT$`. Nothing in this recipe sends anything; `--send` sends
   what is in the outbox, after it has asked for your password.

7. **Once a month.** The program writes a timesheet only if the file
   does not exist yet. Run it as often as you like: after the first
   time in a month it answers that the work is done.

### Run it

```
jdbasic monthly_timesheet.jdb --dry-run
Timesheet September 2026, Jonas Weber
  Admin: 2.25 hours
  Miller offer: 12.00 hours
  Team: 2.00 hours
would write C:\Users\jonas\Documents\AutomateWork\timesheets\
            Timesheet 2026-09.docx
would put a mail to Anna Berg <anna.berg@example.com>

jdbasic monthly_timesheet.jdb
Wrote C:\Users\jonas\Documents\AutomateWork\timesheets\
      Timesheet 2026-09.docx (16.25 hours)
In the outbox: C:\Users\jonas\Documents\AutomateWork\outbox\
               timesheet 2026-09.eml
Send it with: jdbasic monthly_timesheet.jdb --send

jdbasic monthly_timesheet.jdb
The timesheet for 2026-09 is done already.
```

Open the `.eml` file in the outbox to see the mail as your boss will
get it, with the timesheet attached. `--month 2026-08` writes the sheet
of another month and overwrites a sheet that is there already, which is
what you want after you corrected the log.

### Schedule it

The wizard plans the recipe for Monday at 08:00, every week. The wizard
has no schedule for *once a month*, and the recipe does not need one:
on the first Monday of a month the sheet of the month before does not
exist yet, so it is written; on every other Monday the recipe sees the
file and ends. By the first Monday the month is over, and the log of it
is complete.

Sending stays with you. The mail waits in the outbox until you run
`jdbasic monthly_timesheet.jdb --send` yourself.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[timesheet]`:

```toml
[timesheet]
log = ""
folder = "~/Documents/AutomateWork/timesheets"
boss = "Anna Berg <anna.berg@example.com>"
```

The name on the sheet is your name from the `[user]` part, and the
sender of the mail comes from the `[mail]` part.
An empty `log` means the Time Tracker's own log, so moving E07 to
another folder in the wizard moves M15 with it. Set `log` only when
the log lives somewhere the Time Tracker does not write.

Three changes in the code are worth knowing:

- **Round to quarter hours.** Many companies book time in quarters of
  an hour. In `Hours`, replace the line with `RETURN DATEDIFF` by two
  lines that round every block up to the next quarter:

  ```basic
  DIM minutes = DATEDIFF("N", CDATE(s{"start"}), CDATE(stop$))
  RETURN CEIL(minutes / 15) / 4
  ```

- **Leave out private time.** If you track lunch or a doctor's
  appointment as a project of its own, drop it before anything is
  counted. In the program, add this line after the one that sets
  `work`:

  ```basic
  work = FILTER(LAMBDA s -> s{"project"} <> "Private", work)
  ```

- **Your company's words.** The text of the mail is in `MESSAGE`, the
  signature lines are at the end of `DOCUMENT`. Change `"Approved:"`
  to `"Department head:"`, or add a line with your personnel number:

  ```basic
  DOCX.PARAGRAPH(doc, "Personnel number: 4711")
  ```

### When it goes wrong

- **"No time log at"**: the Time Tracker has not written a log yet, or
  you keep it somewhere else. Leave `log` empty to use the one in E07's
  folder, or set it to the file; `jdbasic time_tracker.jdb status` in
  E07 shows whether the Time Tracker sees its log.
- **"No work logged in 2026-09"**: the log has no block that starts in
  that month. Check that the Time Tracker ran, and the month you gave
  with `--month`.
- **A day with 15 hours**: a `stop` was forgotten, and the block ran on
  until the next start. Correct the line in `log.csv` as E07 describes,
  then write the month again with `--month`.
- **"No [timesheet] boss in work.conf, so no mail."**: add `boss` to
  the settings, then run with `--month` to write the sheet and the mail
  once more.
- **The mail does not go out**: `--send` reports every message it could
  not send, and the reason. The message stays in the outbox until the
  next try.

> **Balance dividend**
> About 15 minutes a week, counted over the month: an hour of
> copying on the first working day, and a timesheet that adds up
> without anyone checking.
