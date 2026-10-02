<!-- pagebreak -->

## E12 Birthday Reminder

### The chore

Mia keeps the birthdays of her team, a few clients and her family in a
spreadsheet. She means to look at it every Monday. Most weeks she
forgets, and twice this year she noticed a birthday when the cake was
already on the table. Typing forty birthdays into the calendar by hand
is a job she has put off for two years.

### What you get

Every morning the Birthday Reminder reads your contact list and prints
the birthdays and anniversaries of the next two weeks, the nearest first,
with the age the person turns. It also writes a calendar file with a
yearly event for every date on the list. Import it once into Outlook or
Google Calendar, or subscribe to it, and the dates appear in your
calendar every year without further typing.

### Before you start

Save your contacts as `contacts.csv` in your work folder, with a header
row and one person per line. Excel saves this format with *Save As, CSV
(Comma delimited)*:

```
name,birthday,anniversary
Ann Miller,1985-10-03,2012-06-15
Larry Leap,1992-02-29,
Jan Newyear,1979-01-02,
```

Write the dates as year, month and day with dashes. Leave the
anniversary empty when there is none.

### The program

The program reads the settings, prints the list and writes the calendar
file:

<!-- include recipes/easy/E12_birthday_reminder/birthday_reminder.jdb -->

The module reads the contacts, finds the next date of each birthday and
anniversary, and builds the calendar with the ICAL library:

<!-- include recipes/easy/E12_birthday_reminder/birthdays.jdb -->

### How it works

1. `CONTACTS` reads the CSV file with `CSVREADER`. All three columns are
   read as text, so a date such as `1985-10-03` stays exactly as written.
2. `NEXT_DATE` puts the day and month of a date into this year. If that
   day has passed, it takes next year. A birthday on 29 February falls on
   the 28th in years that have no 29th.
3. `UPCOMING` keeps the dates within the next `days` days and orders them
   with `GRADE`, the nearest first. The age is the year of the next date
   minus the year of birth.
4. `ENTRY$` writes one line for each, such as "Sat 03 Oct  Ann Miller
   turns 41 (in 2 days)".
5. `CALENDAR` writes an all-day event for each date that repeats every
   year. For 29 February the rule says "the last day of February", which
   is the 29th in leap years and the 28th in all others.

### Run it

```
jdbasic birthday_reminder.jdb
Sat 03 Oct  Ann Miller turns 41 (in 2 days)
Mon 12 Oct  Tom Berger turns 30 (in 11 days)
4 yearly events in C:\Users\mia\Documents\AutomateWork\birthdays.ics
```

With `--dry-run` only the list is printed.

### Schedule it

The wizard plans the reminder for 8:00 every morning, so the list is the
first thing you see when the day starts.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[birthday_reminder]`:

```toml
[birthday_reminder]
contacts = "~/Documents/AutomateWork/contacts.csv"
calendar = "~/Documents/AutomateWork/birthdays.ics"
days = 14
```

- **More notice for gifts**: `days = 30` shows a month ahead.
- **One list for the team**: put `contacts.csv` on the team drive and
  point `contacts` at it, so everyone works from the same list.

### When it goes wrong

- **A person is missing**: check the date in the CSV file. It has to be
  written as `1985-10-03`; `03.10.1985` is not read as a date.
- **The age is odd**: the year of birth is unknown and was entered as the
  current year. Leave the year as it is and ignore the age, or enter the
  real one.
- **The calendar shows every date twice**: the file was imported twice.
  Subscribe to it instead of importing it, or delete the first import.

> **Balance dividend**
> About 10 minutes a week, and no forgotten birthday at the office.
