<!-- pagebreak -->

## Maintenance and the Monthly Check

Programs do not wear out, but the world around them moves. The recipes
of this book were written to fail in a visible way: they print what is
wrong, they stop before they touch a file they cannot handle, and they
leave your data where it was. That is a good start. It only helps if
somebody looks.

### What breaks

Almost every failure in the first year comes from one of five places.

**Paths.** A folder is renamed, a network drive gets a new letter, a
new laptop comes with a different user name. The recipe looks for its
folder, does not find it and stops. The fix is one line in
`work.conf`, or a run of the wizard, which finds the settings again
and lets you point the recipe at the new place.

**Passwords.** Your mail password expires every ninety days, and the
recipes of Chapter 4 do not know. A `--send` then ends with a message
from the mail server, and the messages stay in the outbox, so nothing
is lost. If you keep the password in the Windows Credential Manager,
update the entry there; recipe X14 shows how.

**Web pages.** The Page Watcher of E14 and the Web Harvester of X11
read pages that other people build. When a shop redesigns its product
page, the value the recipe looked for is no longer where it was. The
recipe reports that it found nothing, which is the right answer, and
you change the markers of E14 or the selectors of X11 to match the new
page.

**Files from other programs.** An export from the accounting system
gets a new column, an Excel template gains a sheet, a calendar program
writes its dates in another form. Recipes that read such files check
what they expect and say which column or field is missing.

**Settings you typed.** A quote missing in `work.conf` makes the whole
file unreadable, and every recipe stops with a message from the
settings reader, such as `CONF.TOML: unterminated string`. The line
number in that message belongs to the reader, not to your file, so
look at the lines you changed last; a quote that opens and never
closes is the usual cause.

> **Watch out**
> Most easy and medium recipes print what they do, and the Task
> Scheduler does not keep that output. When a scheduled recipe fails,
> the only trace on its own is the task's last result. Run the recipe
> by hand to see its message, or let the job server of X01 start it,
> which writes every run into a log.

### The monthly check

Pick a fixed day, such as the first Monday of the month, and give the
check fifteen minutes. Mia does it with her first coffee; Lena has
turned most of it into a job.

1. **Ask the doctor.** It checks the settings file, the path to
   `jdbasic`, the copied recipes and their tasks:

   ```
   jdbasic wizard\setup_console.jdb --doctor
   ```

`Everything is in order.` is the answer you want. Each problem it
finds is one line, and running the wizard again repairs most of them.

2. **Look at the last results.** The Task Scheduler remembers when each
   task last ran and how it ended. In the *AutomateWork* folder the
   columns *Last Run Time* and *Last Run Result* show it; a result of
   `0x0` means the program ended without an error. From a command
   prompt:

   ```
   schtasks /Query /FO LIST /V /TN "AutomateWork\E01 Downloads Butler"
   ```

A task that has not run for weeks usually means the computer was off
or asleep at its time. A result other than `0x0` means the program
stopped with an error; run it by hand to read the message.

3. **Run each recipe dry once.** `--dry-run` shows what a recipe would
   do today without doing it. Read the list: are these the files you
   expect, the mails, the rows? A few recipes that only read and write
   their own report, such as the Duplicate Finder (E10) or the Disk
   Space Report (E11), have no dry run because they change nothing.

4. **Empty the outbox.** Mail that waits in `outbox` for weeks is mail
   nobody sent. Send it with `--send`, or delete what is no longer
   needed.

5. **Write down what you found.** The file `problems.txt` from Chapter
   2 gets a line for every failure and its fix. After a few months you
   will see that the same two or three causes come back, and you can
   fix them for good.

Here are Mia's first entries, from October to December:

```text
2026-10-12  E01 did not run on Friday. Laptop asleep at 18:30.
            Nothing to fix; Monday's run sorted both days.
2026-10-29  E08 template for the travel request lost its umlauts.
            Saved the template as UTF-8 again in Notepad.
2026-11-03  Doctor: "E03: program missing at ...". I had renamed
            the recipes folder. Ran the wizard again.
2026-11-17  E14 found no price. The shop changed the page; new
            start marker id="offer-price">.
2026-12-01  E07 sheet empty for Monday. I forgot to start the
            clock. Not a bug; I start it first thing now.
```

None of the five entries was a bug in a recipe. Two needed a setting,
one a file saved again, the others a habit or nothing at all. That is
typical, and it is the reason to write them down: after a quarter you
know which failures need code and which need a habit.

### The expert version

Lena runs her jobs under the job server of X01, so most of the check
happens on its own:

- The health checks of X13 run every fifteen minutes. Lena has
  switched `send_alerts` on, so they send her a mail when a job failed
  or is late, when the server has gone quiet, or when mail has waited
  in the outbox for too long. On the first Monday she reads the numbers
  they keep, not each job's log.
- The test runner of X12 runs every recipe's test before she copies a
  changed recipe into the work folder, and once a month as a job of
  its own, so a library update that breaks something shows up on a
  Monday and not at the end of the quarter.
- `jdbasic secrets_audit.jdb --verify` of X14 checks that the audit
  trail of the job server is whole.

Her monthly check is the doctor, the health report and the outbox. It
takes five minutes.

### Updates

New versions of jdBasic come out a few times a year. Before you
install one, run the tests of the recipes you use with the new version
in a folder of its own, or with the test runner of X12. Keep the old
version until the new one has run your recipes for a week. The same
goes for Windows updates on a work computer: you do not choose when
they arrive, but the monthly check after one is worth doing a little
more carefully.