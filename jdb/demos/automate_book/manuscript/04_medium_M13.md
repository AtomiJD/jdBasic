<!-- pagebreak -->

## M13 Snippet Tool

### The chore

Jonas writes the same few texts all day. The answer to a supplier who
sent the wrong invoice. The note that a customer's request has arrived
and will be answered by Friday. The polite refusal of a meeting. Each
time he opens an old mail, copies the text, changes the name and the
date, and forgets one of them about once a week. The texts live in his
sent folder, and finding the right one takes longer than writing it.

### What you get

A small command that keeps such texts as files in one folder and fills
them in when you need them. A text holds placeholders in double braces,
such as `{{name}}` or `{{due}}`. `copy` fills them from the command line
and puts the result on the clipboard, ready to paste into a mail, a
chat or a form. `show` prints it instead, `list` shows what is there
and which placeholders each text expects, and `add` saves a new one.
`{{today}}` is always filled with the date of the day.

### Before you start

The setup wizard has run. The snippets live in a folder of their own,
`snippets` in your work folder unless you choose another one. Each
snippet is a plain text file there: `late.txt` holds the snippet
`late`. You can write them with `add` or with Notepad, whichever is
quicker; the tool does not mind.

### The program

The program describes its four commands, reads the settings and does
what the command says:

<!-- include recipes/medium/M13_snippet_tool/snippet_tool.jdb -->

The module knows the folder and the placeholders:

<!-- include recipes/medium/M13_snippet_tool/snippets.jdb -->

### How it works

1. **Four commands, one description.** `CLI.NEW` starts the description
   of the program, and `CLI.CMD` adds a command with its own arguments
   and options. `CLI.MANY` declares an option you may give more than
   once: `--set name=Ann --set due=Friday`, or `-s` for short. The loop
   after the commands adds `--dry-run` and `--config` to all four of
   them. `CLI.PARSE` then checks the command line against the
   description and answers a map with the command, its arguments and
   its options. `CLI.DONE` handles `--help` and every mistake, such as a
   missing name, by printing the usage and ending the program.

2. **One file per snippet.** `FileOf$` turns a name into a file name in
   the folder, always in small letters, so `Late` and `late` are the
   same snippet. `VALID_NAME` allows letters, digits, `_` and `-`
   only. That keeps a name from reaching outside the folder: `..\boot`
   is no snippet name. `WRITE` refuses to overwrite a snippet unless
   you pass `--force`, so a typo in `add` does not cost you a text you
   spent care on.

3. **Finding the placeholders.** `PLACEHOLDERS` asks `REGEX.FINDALL`
   for every `{{name}}` in the text. The pattern allows spaces inside
   the braces, so `{{ name }}` counts as well. Each name is kept once,
   in the order it first appears, which is the order `list` shows.

4. **Filling them in.** `FILL` walks through the text from one `{{` to
   the next `}}` and builds the result piece by piece. A placeholder
   with a value is replaced by it. One without a value stays in the
   text exactly as it was written, and its name is added to `missing`,
   so the program can tell you `Still open: due`. A text with a stray
   `{{` and no closing braces is left alone.

5. **Where the values come from.** `MERGE` puts three sources together,
   each one able to override the one before: the standing values from
   `work.conf` (your name, your phone number), then `today`, then
   whatever you gave with `--set`. `VALUES` turns the `--set` pairs into
   a map; everything after the first `=` belongs to the value, so
   `--set link=https://example.com/?a=1` works.

The clipboard is touched in one place only, the line with
`CLIPBOARD.SET` in the program. The module never sees it, and neither
does the test.

### Run it

Jonas keeps the note he sends when a meeting runs over in the file
`late.txt` in the snippets folder:

```
Hello {{name}},
I will be {{minutes}} minutes late.
{{sign}}
```

He adds a short one from the command line, looks at what is there,
and fills the first one in:

```
jdbasic snippet_tool.jdb add thanks --text "Thank you, {{name}}."
Saved thanks

jdbasic snippet_tool.jdb list
late  name, minutes, sign
thanks  name

jdbasic snippet_tool.jdb show late -s name=Anna
Hello Anna,
I will be {{minutes}} minutes late.
Jonas
Still open: minutes

jdbasic snippet_tool.jdb copy late -s name=Anna -s minutes=10
On the clipboard: late
```

In `--text`, `\n` starts a new line. `sign` came from the standing
values in `work.conf` (see below). With `--dry-run`, `copy` prints the
text and leaves the clipboard alone, and `add` shows what it would
save.

### Schedule it

There is nothing to schedule: the tool runs when you call it. What
helps is a shorter way to call it. Save these lines as `snip.cmd` in a
folder on your PATH, such as the jdBasic folder:

```
@echo off
setlocal
set HERE=%USERPROFILE%\Documents\AutomateWork\recipes
jdbasic "%HERE%\M13_snippet_tool\snippet_tool.jdb" %*
```

From then on `snip copy late -s name=Anna` does the same from any
command prompt, and the Windows *Run* box (Windows key and R) accepts
it as well.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[snippet_tool]`. The standing values go into a table of their own
below it:

```toml
[snippet_tool]
folder = "~/Documents/AutomateWork/snippets"

[snippet_tool.values]
sign = "Jonas"
phone = "+49 421 555 0100"
team = "Purchasing"
```

Point `folder` at a shared drive, and the whole team uses the same
texts. Everyone keeps their own standing values, so `{{sign}}` signs
with the name of whoever copies.

Three changes in the code are worth knowing:

- **A different date.** `{{today}}` is written as `02 October 2026`.
  For `02.10.2026`, change the format in the line that sets `today$`:

  ```basic
  DIM today$ = FORMAT_DATE(NOW(), "%d.%m.%Y")
  ```

- **A placeholder for a deadline.** Many answers promise something by
  a date. Add a value right after the line with `SNIPPETS.MERGE`, and
  `{{in_a_week}}` is filled with the day a week from now:

  ```basic
  DIM week = DATEADD("D", 7, NOW())
  values{"in_a_week"} = FORMAT_DATE(week, "%d %B %Y")
  ```

- **Refuse half-filled texts.** If a snippet with an open placeholder
  should never reach the clipboard, end the program before the
  clipboard line. Put this in front of `IF cmd$ = "copy" ANDALSO`:

  ```basic
  IF LEN(filled{"missing"}) > 0 ANDALSO cmd$ = "copy" THEN
      PRINT "Give a value for: "; JOIN(filled{"missing"}, ", ")
      END 1
  ENDIF
  ```

### When it goes wrong

- **"No snippet called late"**: the file is not in the folder the
  settings name, or it does not end in `.txt`. `list` shows what the
  tool sees.
- **"Not a snippet name"**: the name holds a space, a dot or a slash.
  Use `late_reply` or `late-reply` instead.
- **"A snippet called late exists; use --force"**: `add` keeps the old
  text. Add `--force` if you really want to replace it.
- **"Use name=value"**: a `--set` without `=`. Quote values that hold
  spaces: `-s "name=Anna Berg"`.
- **A placeholder stays open although you gave it**: the name differs.
  `list` shows the names the snippet expects; `{{due_date}}` needs
  `-s due_date=...`, not `-s due=...`.

> **Balance dividend**
> About 20 minutes a week of searching the sent folder, and no more
> mails that greet Anna as Peter.
