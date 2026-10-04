<!-- pagebreak -->

## E06 Shutdown Ritual

### The chore

Mia's working day has no end. She closes the laptop at six, opens it
again at nine to check one thing, and lies awake with the list of what
is still open. On Monday she cannot remember what she finished on
Thursday, and the weekly call with her team lead starts with ten
minutes of scrolling through sent mail.

### What you get

At the end of every working day the Shutdown Ritual asks three short
questions: what got done, what comes first tomorrow, and how the day
was from 1 to 5. It writes the answers into a journal file, one entry
per day, and on Fridays it shows the week: how many days are written
down, the average mood, everything that got done and what is still
open. Then it tells you to close the laptop.

### Before you start

The setup wizard has run and the Shutdown Ritual is switched on. The
wizard planned it for the end of your working hours.

### The program

The program asks the questions and writes the entry:

<!-- include recipes/easy/E06_shutdown_ritual/shutdown_ritual.jdb -->

The module knows how an entry looks, how to read the journal back and
how to sum up a week:

<!-- include recipes/easy/E06_shutdown_ritual/ritual.jdb -->

### How it works

1. `INPUT` shows a question and waits for the answer. The mood question
   repeats until the answer is a whole number from 1 to 5; `MOOD`
   answers 0 for anything else.
2. `ENTRY$` turns the answers into four lines: a heading with the date
   and the weekday, the mood, what got done and what comes next.
3. `ADD` appends the entry to the journal with `TXTWRITER` and its
   third argument `TRUE`, which adds to the end of the file instead of
   replacing it. The journal is a plain text file you can open in any
   editor.
4. On the summary day `PARSE` reads the journal back into a list of
   entries, and `WEEK` keeps the ones from Monday to Sunday of this
   week with `FILTER`. Its lambda compares each day with the first
   and the last day of the week; `USE(first$, last$)` hands it those
   two values of the function.
5. `SUMMARY$` writes the look back. `SELECT` takes the mood out of
   every entry and `FILTER` drops the days without one, so the average
   is `SUM` divided by `LEN`. The same pair collects what got done.
6. `MONDAY$` finds the start of the week from the weekday number that
   `FORMAT_DATE` gives with `%w`: 0 for Sunday, 1 for Monday, and so on.

### Run it

```
jdbasic shutdown_ritual.jdb
Time to close the day.
What did you get done today? sent the Miller offer
What is the first thing for tomorrow? call the printer
How was the day, from 1 (hard) to 5 (good)? 4
Written to C:\Users\mia\Documents\AutomateWork\journal\journal.md
Done for today. Close the laptop.
```

With `--dry-run` it asks the same questions and shows the entry, but
writes nothing.

### Schedule it

The wizard plans the ritual on weekdays at the end of the working
hours you gave it. A console window opens with the first question.
You can also run it by hand at any time.

### Make it yours

```toml
[shutdown_ritual]
journal = "~/Documents/AutomateWork/journal/journal.md"
summary_weekday = 5
```

- **Another summary day**: `summary_weekday = 1` shows the week on
  Monday morning instead, which some people like better for planning.
- **A journal per year**: point `journal` at a file such as
  `journal 2026.md` and change it in January.
- **Other questions**: the three `INPUT` lines in the program are the
  questions. Change their wording, and keep the order, since the entry
  stores the answers by position.

### When it goes wrong

- **The answers go nowhere**: on Windows, write the path in `work.conf`
  with forward slashes, as above. Inside double quotes a backslash
  starts a special character in a TOML file, so `"C:\Users\mia"` does
  not reach the program as written. Single quotes keep backslashes:
  `'C:\Users\mia\journal.md'`.
- **No summary on Friday**: the summary counts the days of this week
  only. Entries from last week stay in the journal but do not appear.
- **The window closes too fast**: start the ritual from a console
  window; it stays open after the last line.

> **Balance dividend**
> About 20 minutes a week: no searching for what you did when someone
> asks, and a clear point where the working day ends.
