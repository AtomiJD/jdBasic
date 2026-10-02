<!-- pagebreak -->

## M10 Hot Folder

### The chore

Three times a day a colleague from sales drops a file into Jonas's
shared folder: an order list as CSV, a scanned delivery note, a price
list. Each one needs the same treatment. The CSV file has to become a
workbook, since that is what the team opens. Every file gets a name
with the date in front, so the folder sorts by day. Then it moves to
the archive. None of this is hard, but it waits until someone looks at
the folder, and on a busy day nobody does.

### What you get

A folder that works by itself. Every five minutes the Hot Folder looks
into it. Each file that has finished arriving is handled by the first
rule that fits it:

- a CSV file becomes a workbook with a frozen header row, and the
  original CSV file is kept in a subfolder `originals`;
- every other file is moved on;
- both get a new name such as `2026-10-05 orders.xlsx`.

Each step is written to a log, so a question like *where did the price
list of Tuesday go?* has an answer. Your own rules can send PDF files
to one folder and price lists to another.

### Before you start

Pick or create the folder people drop files into, and a folder for the
handled files. The done folder may be inside the watched folder; the
recipe leaves folders alone. Make sure the account that runs the
scheduled task may write to both.

### The program

The program reads the settings and takes the lock. Then it lets the
module plan and carry out the work:

<!-- include recipes/medium/M10_hot_folder/hot_folder.jdb -->

The module holds the rules, the lock and the work on each file:

<!-- include recipes/medium/M10_hot_folder/hotfolder.jdb -->

### How it works

1. **One look, many times.** The recipe does not run all day and watch
   the folder. It looks once, does what there is to do, and ends. The
   task scheduler starts it again five minutes later. A program that
   ends after each look cannot hang for days unnoticed, holds no
   memory, and survives a restart of the computer without anyone
   remembering to start it again.

2. **The lock file.** A run may take longer than five minutes, for a
   large CSV file on a slow network drive. The next run must not start
   on the same files then. `LOCK` writes the file `.hot_folder.lock`
   with the current time into the watched folder and answers `TRUE`. A
   second run finds the file and answers `FALSE`, and the program
   prints *Another run is working on the folder* and ends. `RELEASE`
   removes the file when the work is done. If a run is stopped halfway,
   by a crash or a shutdown, its lock stays behind. That is why the
   lock holds the time: a lock older than 30 minutes is taken over.

3. **Files that are still arriving.** A large file is copied into the
   folder over several seconds. A run that picks it up halfway would
   convert half a file. `PLAN` reads the time each file was last
   changed with `FILE.STAT` and leaves every file alone that changed
   less than `settle_seconds` ago. The next run handles it.

4. **Rules with patterns.** A rule is a map with four keys: `match`,
   a file pattern such as `*.csv`; `convert`, either `xlsx` or empty;
   `rename`, a name pattern; and `target`, the folder the file goes to.
   `MATCHES` turns the file pattern into a regular expression:
   `*` becomes `.*`, `?` becomes `.`, and every other character that
   is no letter or digit is put in brackets, so a dot in `*.csv`
   stands for a dot and not for any character. `RULE_FOR` answers the
   first rule that fits, which is why the catch-all rule `*` comes
   last.

5. **The CSV conversion.** `CSV_FIELDS` reads a line character by
   character, so a quoted field like `"Weber; Sohn & Co"` keeps its
   semicolon, and two quotes in a row stand for one. A field made of
   digits with an optional decimal point becomes a number, which lets
   Excel add up the column. `CSV_TO_XLSX` writes the rows as a sheet.

6. **Never lose a file.** `FreePath$` adds ` (2)` to a name that is
   already taken in the target folder, so a second `orders.csv` on the
   same day does not overwrite the first. `APPLY` wraps the work on
   each file in `TRY` and `CATCH`: a file that cannot be moved, maybe
   because it is open in Excel, gets a *failed* line in the log and
   stays where it is, and the other files are handled.

### Run it

With the example CSV file and a scanned PDF in the folder:

```
jdbasic hot_folder.jdb --dry-run
would handle orders.csv -> C:/Orders/done/2026-10-05 orders.xlsx
would handle scan.pdf -> C:/Orders/done/2026-10-05 scan.pdf
```

Without `--dry-run`, each line of the log is printed too:

```
jdbasic hot_folder.jdb
2026-10-05 09:20  orders.csv  made 2026-10-05 orders.xlsx (3 rows)
2026-10-05 09:20  scan.pdf  moved to 2026-10-05 scan.pdf
```

A run that finds nothing prints *Nothing to do*. That is the common
case, and it costs a fraction of a second.

### Schedule it

The wizard plans the Hot Folder every 5 minutes. Shorter intervals
make little sense for files people drop by hand. For a folder that
another program fills at night, once an hour is enough.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[hot_folder]`:

```toml
[hot_folder]
folder = "C:/Orders"
done_folder = "C:/Orders/done"
rename = "{date} {name}"
separator = ";"
settle_seconds = 60
```

`rename` may use `{date}` and `{time}` of the last change and `{name}`,
the old name without its extension. An empty `rename` keeps the names.

- **Your own rules.** Rules in `work.conf` replace the two built-in
  ones. Each rule needs all four keys, and the first rule that fits a
  file wins:

  ```toml
  [[hot_folder.rule]]
  match = "*.pdf"
  convert = ""
  rename = "{name}"
  target = "C:/Scans"

  [[hot_folder.rule]]
  match = "*.csv"
  convert = "xlsx"
  rename = "{date} {name}"
  target = "C:/Orders/done"
  ```

  Files that no rule fits stay in the folder.

- **A month in the name.** Add one line to `NEW_NAME$`, next to the
  other placeholders, and use `{month}` in `rename`:

  ```basic
  s$ = REPLACE$(s$, "{month}", LEFT$(changed$, 7))
  ```

- **A folder per month.** To sort the handled files into subfolders
  such as `2026-10`, change the target path in `PLAN`:

  ```basic
  DIM month$ = LEFT$(changed$, 7)
  DIM there$ = PATH.JOIN$(r{"target"}, month$, new$)
  ```

  and use `there$` as the `"to"` of the job. `APPLY` creates missing
  folders on the way.

### When it goes wrong

- **"Another run is working on the folder", every time**: a lock is
  left over and the stale time has not passed yet. Wait 30 minutes or
  delete `.hot_folder.lock` in the watched folder.
- **A file stays in the folder**: no rule fits it, or it is still
  changing. Run with `--dry-run`, which lists every file the recipe
  would handle.
- **A *failed* line in the log**: the file was open in another
  program, or the target folder is not writable. The file stays and is
  tried again on the next run.
- **Every row is one long field in the workbook**: the CSV file uses a
  different separator. Set `separator` in `work.conf`.

> **Balance dividend**
> About 20 minutes a week of renaming, converting and moving, and files
> that are ready minutes after they arrive instead of whenever someone
> looks.
