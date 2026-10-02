<!-- pagebreak -->

## Changing a Program: A Monthly Schedule

Sooner or later a recipe does almost what you want. This section walks
through one real change from start to end. It is small on purpose: the
steps matter more than the code.

Jonas runs the Timesheet for the Boss (M15). His department head wants
the sheet on the first of the month, and the wizard knew schedules by
the day, the week, the hour and the minute, but none by the month. A
weekly run on Mondays worked, because the recipe writes each month
only once, but Jonas wanted the schedule to say what it means. So the
wizard had to learn a new kind of schedule: `monthly 1 08:00`, the
first day of every month at eight.

### 1. Find the place

The wizard turns a schedule such as `weekly fri 16:00` into the
arguments of `schtasks`, the Windows program that creates scheduled
tasks. Search the wizard's files for the word `weekly`; Notepad's
*Find* does it, or from a command prompt in the book folder:

```
findstr /n "weekly" wizard\wizard.jdb
```

The hits lead to one function, `SCHEDULE_ARGS` in `wizard/wizard.jdb`.
It splits the schedule into words and has one line per kind of
schedule. A new kind means a new line there. The comment above the
function lists the kinds it understands, so it changes too.

Before writing anything, find out what the other side wants. Windows
explains its own tools:

```
schtasks /Create /?
```

The help says that a monthly task takes `/SC MONTHLY` and a day of the
month with `/D`, from 1 to 31. That raises a question the code has to
answer: what about the 31st in a month that has only 30 days? The safe
answer is not to allow it. Every month has the days 1 to 28, so the
wizard accepts those and refuses the rest with a message that says
why.

### 2. Copy before you change

Copy the folder `wizard` to `wizard_before` before you touch it. If
anything goes wrong, you delete the folder you broke and rename the
copy back. People who know git commit instead; either way, there must
be a way back that takes one minute.

### 3. Write the test first

The wizard has a test, `wizard/wizard_test.jdb`, that checks every
kind of schedule without creating a single task. A change starts
there, with the checks that the new schedule has to pass. A helper
turns a schedule into the last six `schtasks` arguments, where the
schedule itself sits:

<!-- include wizard/wizard_test.jdb from="FUNC Monthly$" to="ENDFUNC" -->

<!-- include wizard/wizard_test.jdb from="FUNC LateInMonth" to="ENDFUNC" -->

and the checks name what the change must do: the first of the month,
another day, a day that some months do not have, and the line in the
plan the wizard shows on its last page:

<!-- include wizard/wizard_test.jdb from="DIM once$ = Monthly$" to="TESTKIT.EQ(plan$" -->

Run the test before you change the wizard:

```
jdbasic wizard\wizard_test.jdb
```

It stops at the first new check, with the message the old wizard gives
for every schedule it does not know. Without the line number it ends
with, it reads:

```
Error #99: WIZARD: cannot schedule "monthly 1 08:00" for E01
```

That failure is the point of this step. It proves that the test looks
at the right thing; a test that passes before the change proves
nothing about the change.

### 4. Make it pass

Two pieces of code do the work. The first checks the day of the month
and answers it as `schtasks` wants it:

<!-- include wizard/wizard.jdb from="' A day of the month" to="ENDFUNC" -->

The second is a new block in `SCHEDULE_ARGS`, next to the lines for
the other schedules:

<!-- include wizard/wizard.jdb from='IF n = 3 ANDALSO parts[0] = "monthly"' to="ENDIF" -->

`parts` holds the words of the schedule, so `monthly 1 08:00` gives
`parts[1] = "1"` and `parts[2] = "08:00"`. `IsClock` was already there
for the other schedules and checks that the time looks like a time. A
monthly schedule with a time it cannot read leaves the block and
reaches the last line of the function, which refuses it like any
schedule the wizard does not know. The comment above `SCHEDULE_ARGS`
gains `"monthly DAY HH:MM"` in its list.

Run the test again. Every check passes, the old ones included, which
shows that the new block did not disturb the others:

```
57 passed, 0 failed, 0 skipped
```

### 5. Run it dry

A passing test says the code does what the test asks. The dry run says
the whole wizard still works. Start the wizard, pick the Timesheet for
the Boss on the third page, type `monthly 1 08:00` as its schedule,
and on the last page tick *Only show what would happen* before you
press *Set up now*. The plan shows the new line:

```
schedule Timesheet for the Boss: monthly 1 08:00
```

Nothing has been created yet. When the plan looks right, untick the
box and set up for real.

### 6. Change the default

The schedule a recipe starts with comes from its `recipe.toml`. For
M15 that line now reads:

```toml
schedule = "monthly 1 08:00"
```

The wizard shows it the next time you run it, and every reader of the
book gets the new schedule as the default.

### What the change does not do

Be honest about the limits of a change, in the code and on paper. The
Task Scheduler does not catch up a task whose day passed while the
computer was off, and the first of a month falls on a weekend about
two months in seven. For a computer that is off at weekends, the old
`weekly mon 08:00` is still the better schedule for M15, and its page
in Chapter 4 says so. The job server of X01 catches up missed runs,
which is one reason Lena moved her monthly jobs there.

### The pattern

The same steps work for any change, from a new setting to a new
recipe:

1. Find the place, and find out what the other side expects.
2. Make a copy you can go back to.
3. Write the checks first and watch them fail.
4. Change the code until every check passes, the old ones too.
5. Run the program dry and read what it would do.
6. Only then let it act, and change the defaults last.

It looks slow for a change of a dozen lines. It is the reason the
change still works next year.