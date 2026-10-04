<!-- pagebreak -->

## M02 Report Merger

### The chore

Every month the eight people of Jonas's team send a small Excel report:
the projects they worked on, the hours, the costs, a note. Jonas opens
eight files, copies the rows into one workbook, sorts them by project,
adds up the hours with a formula that he fixes each time because the
row count changed, and then notices that one report is missing and
another one has a column more than the rest.

### What you get

You put the reports of a month into one folder. The Report Merger
reads all of them and writes one workbook: a sheet *Total* with the
rows of all reports added up by project, and one sheet per person with
the report as it came. Before it writes, it tells you who has not sent
a report and whose columns differ from the others.

### Before you start

The setup wizard has run. Make a folder for the reports with one
subfolder per month, named like `2026-10`. Each person's file goes in
there under the person's name, such as `Anna.xlsx`. All reports have
the same columns, and the first column names the row:

| Project | Hours | Costs | Note |
|---|---|---|---|
| Alpha | 10 | 250.50 | on track |
| Beta | 4 | 80.00 | |

The first column can be anything that the rows of different people
have in common: a project, a cost centre, a customer.

### The program

The program finds the month, reads the folder, reports what is
missing, and writes the workbook:

<!-- include recipes/medium/M02_report_merger/report_merger.jdb -->

The module `MERGER` reads, compares, adds up and writes:

<!-- include recipes/medium/M02_report_merger/merger.jdb -->

### How it works

1. **The month.** Without `--month` the program takes last month,
   which is what you want on the first working days of a month.
   `LastMonth$` handles January by going back to December of the year
   before.
2. **Reading.** `REPORTS` lists the `.xlsx` files of the month's folder
   in name order and reads each one with `XLSX.READ`. The person is
   the file name without `.xlsx`. Files that start with `~$` are the
   lock files Excel leaves while a workbook is open; they are skipped.
3. **Who is missing.** `MISSING` turns the reports into a list of
   their names in small letters with `SELECT`, and keeps with `FILTER`
   the people of the `team` list from the settings who are not in it.
   An empty list means that nobody is checked.
4. **Odd columns.** `ODD_HEADERS` joins each header row into one text
   and keeps with `FILTER` the reports whose text differs from the
   first report's; a `SELECT` answers their names. A report with a
   column more, a column less or the columns in another order is
   named, and its numbers still count by position.
5. **Adding up.** `TOTALS` keeps a map from the first column to the
   position of its row in the totals. The first time a project appears
   it gets a row of empty cells; after that every number in a report
   row is added to the cell in the same column. A text cell, such as
   the note, keeps the first text that is not empty.
6. **Writing.** `WRITE` puts the totals on the sheet *Total*, freezes
   its header row, and adds one sheet per report. `SHEET_NAME$`
   replaces the characters Excel does not allow in sheet names and
   cuts the name to 31 characters.

### Run it

```
jdbasic report_merger.jdb --month 2026-10 --dry-run
3 reports for 2026-10
Missing: Dora
would write reports\2026-10 team.xlsx with 3 rows
```

Dora's report is missing. Once it is in the folder, run it again
without `--dry-run`:

```
jdbasic report_merger.jdb --month 2026-10
4 reports for 2026-10
wrote reports\2026-10 team.xlsx (5 sheets)
```

The workbook lands next to the month's folder, so the folder itself
keeps only the reports.

### Schedule it

The merge is worth running when the reports are in, and that is a
date nobody can plan. The wizard sets up no task for this recipe. Run
it with `--dry-run` on the first working day to see who is missing,
and without it when they have all sent theirs.

### Make it yours

The settings:

```toml
[report_merger]
folder = "~/Documents/AutomateWork/reports"
tab = ""
team = ["Anna", "Ben", "Cleo", "Dora"]
```

`tab` names the sheet to read in each report. Leave it empty to read
the first sheet, whatever its name.

Changes in the code:

- **Add up by two columns,** such as project and task: in `TOTALS`,
  build the key from both cells,
  `DIM key$ = TRIM$("" + row[0]) + " / " + TRIM$("" + row[1])`.
  Start a new row as `[row[0], row[1]]` instead of `[key$]`, and let
  both `FOR c = 1` loops start at 2.
- **Leave out a sum row** that people add at the bottom of their
  report: in `TOTALS`, change `IF key$ <> "" THEN` to
  `IF key$ <> "" ANDALSO LCASE$(key$) <> "sum" THEN`.
- **Stop on odd columns** instead of only naming them: in the program,
  after the line that prints them, add `THROW "Fix the columns first."`
  inside the `IF`.
- **The workbook in another folder,** such as a shared drive: change
  `out$` in the program to `PATH.JOIN$("S:/Team", month$ + ".xlsx")`.
- **Mail the workbook** to the head of the department: build a message
  with `MAIL.MESSAGE` and `MAIL.ATTACH(m, out$)` and put it into the
  outbox with `OUTBOX.PUT$`, following the outbox rule of this
  chapter; M01 shows the lines.

### When it goes wrong

- **"No folder ...\2026-09"**: the month's folder does not exist yet,
  or it is named another way. Give the month with `--month`, or name
  the folder `YYYY-MM`.
- **"Anna.xlsx has no sheet October"**: `tab` names a sheet that this
  report does not have. Leave `tab` empty, or ask for the template's
  sheet name to be kept.
- **A person is missing although the file is there**: the file name
  and the name in `team` differ, such as `anna_mueller.xlsx` and
  `Anna`. Use the same name in both places.
- **A project appears twice in the totals**: it was written two ways,
  such as `Alpha` and `Alpha `. Spaces at the ends are removed;
  any other difference makes a new row.
- **Writing fails**: the workbook of the last run is still open in
  Excel, which locks the file. Close it and run again.

> **Balance dividend**
> About an hour a month of copying and checking, fifteen minutes a
> week, and the missing report is named before anyone asks.
