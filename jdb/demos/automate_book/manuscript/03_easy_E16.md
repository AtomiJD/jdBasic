<!-- pagebreak -->

## Bonus: E16 Form Filler

### The chore

Twice a week Mia books a trip in the travel portal of her company. The
form asks the same things every time: her name, her staff number, the
cost centre, her manager. Only the destination and the dates change.
She types the same five fields again and again, and once a month she
mistypes the staff number and the booking comes back.

### What you get

You click into the first field of a form and start the Form Filler.
After a short countdown it types your values into the fields, one after
the other, with Tab between them, the way you would. You check the
form, add what is new this time, and send it yourself.

### Before you start

The setup wizard has run. The Form Filler is a recipe you start by
hand, so the wizard plans no time for it. You need the values of your
form, in the order the form asks for them. To find that order, click
into the first field and press Tab a few times: the cursor jumps from
field to field the way the Form Filler will type.

> **Watch out**
> While it types, the Form Filler takes over your keyboard. Whatever
> window is in front gets the key strokes, so keep your hands off the
> keyboard and the mouse until it is done. To stop it at once, hold
> Esc, or push the mouse into the top-left corner of the screen. Never
> start it on a screen that others can see or use, and ask your IT
> department first: some companies forbid programs that type for you.
> Never put a password into the list. `work.conf` is a plain text file
> that anyone with access to your folder can read.

### The program

The program reads your values and the countdown, builds the key
strokes, and either shows them or types them:

<!-- include recipes/easy/E16_form_filler/form_filler.jdb -->

The module turns your values into a macro, a short list of commands
such as `type Mia Example` and `key tab`:

<!-- include recipes/easy/E16_form_filler/formfill.jdb -->

The typing itself happens in the shared module `inputkit.jdb` in the
folder `recipes/lib`, which the wizard copies with the other shared
modules. Chapter 4 shows its macro language in the Macro Player (M16),
and Chapter 5 its emergency stop in the Desktop Robot (X16).

### How it works

1. `WORKCONF.VALUE` reads the `[form_filler]` part of `work.conf`: the
   values, the title of a window, the countdown and whether to press
   Enter at the end.
2. `FORMFILL.MACRO$` writes one `type` line per value and a `key tab`
   line between two values. An empty value types nothing, so its
   Tab skips a field you want to leave as it is.
3. `FORMFILL.JOB` hands the macro to `INPUTKIT.PARSE`, which checks
   every line and turns it into a step.
4. With `--dry-run` the program prints the steps and ends. Without it,
   `INPUTKIT.COUNTDOWN` gives you a few seconds to click into the
   first field, and `RUN_STEPS` types the values.
5. Before every value and every key the module looks at Esc and at
   the position of the mouse. Either one stops the run with a message,
   and nothing more is typed.

### Run it

With five values in `work.conf`, the third one empty, the dry run
shows every key stroke before anything happens:

```
jdbasic form_filler.jdb --dry-run
would line 1: type 'Mia Example' (11 characters)
would line 2: press tab
would line 3: type '4711' (4 characters)
would line 4: press tab
would line 5: press tab
would line 6: type 'CC 3100 Sales' (13 characters)
would line 7: press tab
would line 8: type 'Anna Berg' (9 characters)
8 steps. Run without --dry-run to type them.
```

Open the form and start the program without `--dry-run`. The command
prompt now has the focus, so click into the first field of the form
during the countdown.

### Schedule it

The Form Filler has no schedule: a form is there when you open it, not
at a fixed time. Start it from a command prompt, from a shortcut on
the desktop that runs `jdbasic form_filler.jdb`, or with a button of
the Work Cockpit (X06).

### Make it yours

The settings are the `[form_filler]` part of `work.conf`:

```toml
[form_filler]
values = ["Mia Example", "4711", "", "CC 3100 Sales", "Anna Berg"]
window = ""
countdown = 5
submit = false
```

- **More fields.** Add values to the list, in the order of the form.
  An empty value `""` leaves a field as it is.
- **The right window first.** Put the exact title of the form's window
  into `window`, as its title bar shows it, and the Form Filler brings
  that window to the front before it types. The cursor still has to be
  in the first field.
- **Send the form.** `submit = true` presses Enter after the last
  value. Leave it off until you have watched a few runs.
- **More time.** `countdown = 10` gives you ten seconds to click into
  the form.

### When it goes wrong

- **A value lands in the wrong field**: the form's Tab order is not the
  order you see. Press Tab through the form by hand and count; add an
  empty value for each field you want to skip.
- **The text goes into another window**: the window that was in front
  when the countdown ended got it. Click into the form during the
  countdown, or name the form in `window`.
- **Nothing arrives, and there is no message**: the form belongs to a
  program that runs as administrator. Windows does not let a normal
  program type into it, and it does not say so. Start that program
  normally, or fill its form by hand.
- **"Esc is held: stopped."**: you pressed Esc, or a key got stuck.
  Run it again.

> **Balance dividend**
> About 15 minutes a week for two forms of five fields, and the staff
> number that is always right.
