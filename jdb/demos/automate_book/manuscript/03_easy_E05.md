<!-- pagebreak -->

## E05 Break Reminder

### The chore

When Mia is deep in a spreadsheet, two hours pass without her standing
up. In the evening her neck tells her. She has tried a timer on her
phone; it rings in meetings and she switches it off for good.

### What you get

During your working hours, a small message asks you to stand up, stretch
and drink some water, every 50 minutes. Before work, after work and at
the weekend it stays quiet. One click on OK and it is gone.

### Before you start

The setup wizard has run and the Break Reminder is switched on with your
working hours.

### The program

The program is started every ten minutes. It decides whether a break is
due and only then shows the message:

<!-- include recipes/easy/E05_break_reminder/break_reminder.jdb -->

The decision is in a module of its own, so it can be tested without a
single message box:

<!-- include recipes/easy/E05_break_reminder/breaks.jdb -->

### How it works

1. The program remembers the time of the last reminder in a small text
   file in your work folder.
2. `WORKING` checks the time of day against your working hours, and the
   weekday: `FORMAT_DATE(when, "%w")` is 0 for Sunday and 6 for
   Saturday.
3. `DUE` answers yes when no reminder came yet today, or when the last
   one is at least `every_minutes` old.
4. When a break is due, the program writes the new time first and then
   shows the message with `MSGBOX`. The 64 makes it the friendly kind
   with an information sign.

The scheduler starting the program every ten minutes is simpler than a
program that runs all day: nothing to keep alive, nothing to restart
after the laptop slept.

### Run it

```
jdbasic break_reminder.jdb --dry-run
A break is due. Run without --dry-run to be reminded.
```

### Schedule it

The wizard plans the Break Reminder every ten minutes. It decides on its
own whether it is time, so the schedule does not need to know your
working hours.

### Make it yours

```toml
[break_reminder]
start = "08:30"
end = "17:30"
every_minutes = 50
workdays_only = true
message = "Time for a break: stand up, stretch, drink some water."
```

- **Longer stretches**: `every_minutes = 90`.
- **Your own words**: change `message`; the reminder that makes you
  smile is the one you follow.
- **Shift work**: set `workdays_only = false` and your hours.

### When it goes wrong

- **No reminder at all**: run it with `--dry-run` during your working
  hours. "No break due" before 50 minutes have passed is right.
- **A reminder during a presentation**: Windows' focus assist hides most
  messages; switch it on while you present.
- **Reminders right after another**: two schedules start the program.
  Remove one in the wizard.

> **Balance dividend**
> No minutes saved, a few given back to your body: about eight short
> breaks a day.
