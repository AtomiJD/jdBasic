<!-- pagebreak -->

## Sharing Without Becoming the Help Desk

Jonas's Friday Status Mail (M06) worked so well that two other team
leads asked for it. A week later one of them called because it did not
find his notes, and the other wanted the mail in a different order.
Sharing a recipe is a good thing to do. It also changes it: on your
own computer it is a tool, on five computers it is a small product,
and a product needs a few things a tool does not.

### Before you hand it out

Run through this list once, on a copy of the recipe:

- **Nothing of yours is in the code.** Folders, names, addresses and
  times belong in `work.conf`, with a default that makes sense for
  somebody else. Search the program for your user name and your mail
  address; neither should appear.
- **The test passes on a fresh folder.** Every recipe's test works in
  a folder of its own, made with `MKTEMP$`. If it only passes on your
  computer, it depends on something you forgot.
- **The dry run is the first thing they see.** Tell your colleague to
  start with `--dry-run` and read the list, exactly as you did.
- **There is a page of text.** What the recipe does, which settings it
  reads, what it changes, and what to do when it fails. The recipe
  pages of this book are written in that order; a colleague's copy
  needs a short version of the same.
- **It has a version.** A date in the first comment is enough, such as
  `version 2026-11-02`. When somebody reports a problem, the first
  question is which version they have.

Jonas's page for the Friday Status Mail fits on one screen:

```text
Friday Status Mail (M06), version 2026-11-02
Owner: Jonas Example. Questions: the "automation" channel.

What it does: every Friday at 15:00 it reads the notes in the
status folder, builds the week's status mail and puts it into
your outbox. It sends nothing on its own.

Settings, in the [friday_status] part of work.conf:
  notes_folder  where your notes are (~/Documents/Team/status)
  to            who gets the mail, a list of addresses
  subject       the subject line; {week} becomes 2026-W45
  clear_done    true removes the done lines from your notes

What it changes: a new .eml file in the outbox; with
clear_done = true also your notes files.

First run: jdbasic friday_status.jdb --dry-run
Send:      jdbasic friday_status.jdb --send

When it fails: send the version, the full message and the
output of the dry run to the channel above.
```

### Three ways to share

**The recipe folder.** Your colleague installs jdBasic, copies the
recipe folder into the book's `recipes` folder, next to the others of
its level, and runs the setup wizard, which lists it and writes their
own `work.conf`. This is the right way for people who will change the
recipe themselves.

**A compiled program.** Recipe X07 builds an `.exe` with its runtime,
a version file and checksums, and puts it on a shared drive. Its
`update` command brings a colleague's copy to the newest version and
checks every file before it changes anything. Your colleagues need no
jdBasic of their own. Building the program needs the release of
jdBasic with the native compiler, and handing it out needs the
approval of IT, which the next section is about.

**The cockpit.** The Work Cockpit of X06 is a window with a button for
every automation. For colleagues who never open a command prompt, it
turns "run this with `--dry-run`" into a click.

### Rules that keep you out of the help desk

1. **One place for questions.** A mailbox, a channel in your chat
   program or a page on the team drive. Questions in the corridor are
   answered once and forgotten; questions in one place are answered
   once and found again.
2. **A report has three parts.** Ask for the version, the full message
   and the output of the dry run. With those three you can find most
   problems without visiting anybody's desk.
3. **One version for everybody.** When a colleague wants the mail in a
   different order, find out whether it can be a setting. If it can,
   everybody gets the setting. If it cannot, it is their copy, and
   they maintain it.
4. **Say what you support.** "I fix bugs in the version on the shared
   drive, on Thursdays" is a fair promise. "I keep it running for
   everybody" is a second job.
5. **Find a second owner.** When the whole team depends on a recipe,
   somebody besides you should be able to read it, run its test and
   change a setting. Show them once, with this book open at the
   recipe's page.

### When you leave

People change teams, and automations stay behind. Before you go, write
down which recipes run where, under whose account, with which
settings, and who owns them now. If nobody wants to own a recipe,
switch it off and say so; an automation nobody owns is worse than
none, because people rely on it until the day it breaks.