<!-- pagebreak -->

## X08 Parallel Crunching

### The chore

At the start of every month the travel booking tool hands Lena the
expenses of the month before: one CSV file per cost centre and day,
about two thousand files. Finance wants one table out of them, the
amount per category for hotels, meals, taxis and trains. Her first
program for it read the files one after the other and took several
minutes, during which her laptop used one of its eight cores and left
the other seven alone. Worse, when one file in the folder was not an
export at all, the program stopped halfway and she started over.

### What you get

A program that hands the files to as many workers as the computer has
cores, adds their answers into one report, and writes it as a CSV
file next to the earlier ones:

```text
category;amount
hotel;450700.00
meals;450610.00
taxi;450700.00
train;450880.00
total;1802890.00
```

A file that is no export does not stop the run. It is named in the
summary and in the log, and the other files are added up. Lines in an
export that cannot be read, such as an amount written as a word, are
counted and skipped.

### Before you start

Put the exports of one month into a folder of their own, one folder
per month, such as `exports/2026-09`. Each file needs a header line
with the columns `date`, `category` and `amount`, separated by the
separator of the settings. The recipe reads the files and never
changes them.

For the speed this page describes, compile the program first, as X07
shows. The interpreter runs the same program with the same result,
but there the workers gain little: the run takes about as long as on
one core.

### The program

The program reads the settings, lets the module do the work, and
writes the report and a log line:

<!-- include recipes/expert/X08_parallel_crunching/parallel_crunching.jdb -->

The module reads one export, runs all of them on one core or on many,
and adds the answers up:

<!-- include recipes/expert/X08_parallel_crunching/crunch.jdb -->

### How it works

1. **A line of text per file.** `SUMMARY$` reads an export and
   answers a short line such as `20;63500;0;hotel=31750,taxi=31750`:
   the number of rows, the total in cents, the lines it skipped, and
   the cents per category. Everything a worker hands back is in that
   line, so the workers share nothing while they work.

2. **Cents, not euros.** Amounts are added as whole cents. Adding
   decimal numbers in a different order can change the last digit,
   and the order in which workers finish is never the same twice.
   With cents the parallel report is always identical to the serial
   one, and the test checks exactly that.

3. **The worker.** `Worker` is an `ASYNC FUNC`: each call runs on a
   thread of its own. It takes the next job number from the pool with
   `POOL.TAKE_JOB`, reads that file, and reports the line with
   `POOL.DONE_TEXT`. When a file throws an error, `POOL.FAILED` hands
   the message back instead, and the worker goes on with the next job.
   When no job is left, `TAKE_JOB` answers -1 and the worker ends.

4. **The pool.** `PARALLEL` creates a pool with one job per file and
   starts as many workers as asked for. `POOL.GATHER` waits until
   every job has ended, and a job that takes longer than the timeout
   counts as failed. The answers come back in the order of the files,
   no matter which worker finished first.

5. **One core for comparison.** `SERIAL` does the same work in a
   plain loop and answers the same map. Run the program with
   `--serial` when you want to see what the workers gain, or when you
   look for a problem and want one thing to happen at a time.

6. **Adding up.** `MERGE` splits the lines again and adds rows, cents
   and skipped lines, and the cents of each category. `REPORT_CSV$`
   turns the totals into the report, and `MONEY$` writes cents with
   two decimals.

7. **No report is overwritten.** When `expenses 2026-09.csv` exists
   already, the next run writes `expenses 2026-09 2.csv`, so a second
   run after a correction keeps the first result for comparison.

### Run it

A dry run counts the files and says where the report would go:

```
parallel_crunching.exe --dry-run
would crunch 2001 files in C:/Exports/2026-09
on 8 workers, report into C:/Reports
```

The real run prints the summary that also goes into the log. Here one
file of the folder, `readme.csv`, is a note and no export:

```
parallel_crunching.exe
2026-10-05 08:12 C:/Exports/2026-09
  2000 files, 40000 rows, 5 lines skipped, 8 workers, 338 ms
  total 1802890.00, report C:/Reports\expenses 2026-09.csv
  not read: readme.csv: not an expense export
```

A run in which a file was not read ends with an error, so the job
server of X01 and the cockpit of X06 show it as failed, although the
report was written.

How much the workers gain depends on the work per file. For two
thousand exports of 300 lines each, on a notebook with eight threads,
the compiled program needed 4.2 seconds on one core and 1.5 seconds
with eight workers. The interpreter needed about six seconds either
way.

### Schedule it

The exports arrive once a month, so the wizard leaves the recipe to
be run by hand. With the job server of X01 it runs on the second
working day of the month, after the export has arrived:

```toml
[[job]]
name = "expenses"
when = "0 9 2 * *"
program = "X08_parallel_crunching/parallel_crunching.jdb"
args = ["--folder", "C:/Exports/latest"]
```

### Make it yours

The settings are in the part of `work.conf` that starts with
`[parallel_crunching]`:

```toml
[parallel_crunching]
folder = "C:/Exports/2026-09"
reports = "~/Documents/AutomateWork/reports"
separator = ";"
workers = 0
timeout_seconds = 30
```

`workers = 0` means one worker per core. On a computer that has other
work to do while the recipe runs, give it fewer.

- **Other columns.** If your exports have the amount in the fifth
  column and the category in the third, change the two indexes in
  `SUMMARY$` and the check `LEN(parts) = 3` to the number of columns.
- **Totals per cost centre.** Put the file name in front of the
  category in `SUMMARY$`, so a key reads `cc0412.csv/hotel`:

  ```basic
  DIM cat$ = PATH.BASENAME$(path$) + "/" + TRIM$(parts[1])
  ```

  `REPORT_CSV$` sorts the keys, so the report groups by cost centre.
- **Other work per file.** Anything that turns one file into one line
  of text fits the same pool: the page count of PDF files, the words
  of a set of Word documents, the checksum of every file of a backup.
  Write a new `SUMMARY$` and a `MERGE` that adds its lines.

### When it goes wrong

- **"No CSV files in"**: the folder is wrong, or the exports end in
  `.txt`. Change the pattern in `FILES`.
- **"not an expense export"** for every file: the header uses another
  separator or other names. Look at the first line of one file and set
  `separator`, or change the header check in `SUMMARY$`.
- **Many skipped lines**: the amounts use a decimal comma, such as
  `12,50`. Replace the comma with `REPLACE$(parts[2], ",", ".")`
  before the check.
- **A file timed out**: it is very large or lies on a slow network
  drive. Raise `timeout_seconds`, or copy the folder to the local disk
  first.

> **Balance dividend**
> About 40 minutes a month, ten a week, of waiting and starting over,
> and a report that names every file it left out instead of a total
> nobody can check.
