<!-- pagebreak -->

## M06 Friday Status Mail

### The chore

Every Friday afternoon Jonas writes the status mail for his manager
and the team. He scrolls through the chat for what got done, asks
three people what they are stuck on, copies it all into a mail and
tidies the formatting. It takes 45 minutes on a good Friday, and on a
bad one the mail goes out on Monday.

### What you get

Each person in the team keeps a small notes file during the week, one
line per item with a check box. On Friday afternoon the status mail
collects all files and puts the finished mail into the outbox: a
summary line, then for every person what is blocked, what is open and
what is done. The mail has a plain text part and an HTML part with a
table, so it reads well in any mail program.

Jonas opens the mail in the outbox, adds a sentence of his own if he
wants, and sends it.

### Before you start

The team needs a shared folder with one text file per person, named
after them: `Ann.txt`, `Ben.txt`, and so on. In the file, a line that
starts with a check box is an item; every other line is ignored, so a
heading or a comment does no harm:

```
# Ann, week 43
- [x] Offer for Miller sent
- [ ] Call the printer about the toner
- [!] Contract waits for legal
```

`[x]` means done, `[ ]` still to do, `[!]` blocked. The `x` may be a
capital letter too. Markdown editors and many note apps show these
lines as real check boxes.

The mail server goes into the `[mail]` part of `work.conf` once, as the
introduction of this chapter describes; the status mail follows the
outbox rule of this chapter.

### The program

The program reads the settings, collects the notes, builds the mail
and puts it into the outbox. With `--send` it sends what the outbox
holds instead:

<!-- include recipes/medium/M06_friday_status/friday_status.jdb -->

The module reads the notes and writes both parts of the mail:

<!-- include recipes/medium/M06_friday_status/status.jdb -->

The HTML part comes from a template file next to the program, in
`templates/status.html`. It is plain HTML with a few marks of the TMPL
library: `{{ week }}` is replaced by the week, and
`{% for p in people %}` repeats a table row for every person.

### How it works

1. `STATUS.NOTES` lists the `.txt` files of the folder with `DIR$`,
   sorted by name, reads each with `TXTREADER$`, and names the person
   after the file without its extension.
2. `READ_NOTES` looks at the first five characters of every line. If
   they are one of the three check boxes, the rest of the line goes
   into the list for done, to do or blocked. `LCASE$` makes `[X]`
   count as `[x]`.
3. `WEEK$` names the week the way calendars do, with `DT.ISOWEEK`: the
   ISO week starts on Monday, and the first of January can belong to
   the last week of the year before.
4. `PLAIN$` builds the text part line by line into a list and joins it
   at the end. Blocked items come first, because they are the ones
   someone has to act on.
5. `HTML$` hands a map with the week, the people and the totals to
   `TMPL.RENDER$`, which fills the template. TMPL writes every value
   escaped for HTML, so an item such as *Miller & Co* cannot break
   the table.
6. `MAIL.MESSAGE` creates the message with the sender from the `[mail]`
   part, and `OUTBOX.PUT$` writes it as `status-2026-W43.eml` into the
   outbox. The same week always gives the same file name, so running
   the program twice replaces the first mail instead of adding a
   second one.
7. With `clear_done = true`, the program then rewrites every notes file
   without its done lines, ready for the next week. `WITHOUT_DONE$`
   keeps every other line as it was.

### Run it

A dry run prints the mail and changes nothing:

```
jdbasic friday_status.jdb --dry-run
To: team@example.com
Subject: Status 2026-W43

Status 2026-W43

2 done, 2 open, 1 blocked.

Ann
  Blocked:
    - Contract waits for legal
  Open:
    - Call the printer about the toner
  Done:
    - Offer for Miller sent

Ben
  Open:
    - Plan the team day
  Done:
    - Budget 2027 draft

Nothing written. Run without --dry-run for the outbox.
```

Without the switch, the mail goes into the outbox, and
`jdbasic friday_status.jdb --send` sends it after asking for the
password.

### Schedule it

The wizard plans the status mail for Fridays at 15:00, which leaves an
hour to look at it before the weekend. The `--send` step stays a step
you take yourself.

### Make it yours

The settings are the `[friday_status]` part of `work.conf`:

```toml
[friday_status]
notes_folder = "S:/Team/status"
to = ["team@example.com", "boss@example.com"]
subject = "Sales team, status {week}"
clear_done = true
```

Changes in the code:

- **Only the blocked and open items.** In `PLAIN$`, delete the line
  `AddList(out, "Done:", p{"done"})`. In the template, delete the cell
  with `p.done` and its column heading.
- **A fourth kind of note**, such as `[?]` for questions to the
  manager. In `READ_NOTES`, add the kind to the map at the top,
  `{"done": [], "todo": [], "blocked": [], "ask": []}`, and one line
  for the box:

  ```
  IF box$ = "- [?]" THEN kind$ = "ask"
  ```

  Then add `AddList(out, "Questions:", p{"ask"})` to `PLAIN$`.
- **People who sent nothing this week.** In `PLAIN$`, after the line
  `PUSH(out, p{"person"})`, add a line when all three lists are empty:

  ```
  DIM n = LEN(p{"done"}) + LEN(p{"todo"}) + LEN(p{"blocked"})
  IF n = 0 THEN PUSH(out, "  (no notes this week)")
  ```

### When it goes wrong

- **"Set to in [friday_status]"**: the list of recipients is empty.
  Add at least one address.
- **A person is missing**: the file does not end in `.txt`, or it lies
  in a subfolder. The program reads the files of the folder itself.
- **An item is missing**: its line does not start with one of the
  three boxes. A space inside the brackets, `[ x ]`, does not count.
- **"No mail template at"**: the `templates` folder was not copied
  with the program. Run the wizard again, or copy the folder by hand.

> **Balance dividend**
> About 45 minutes every Friday, and a status that goes out on time
> because it no longer depends on your memory of the week.
