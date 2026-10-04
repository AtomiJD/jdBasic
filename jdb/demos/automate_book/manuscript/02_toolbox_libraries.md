## The Libraries the Recipes Use

Most office chores come down to a handful of file types and a few
recurring questions. A spreadsheet has to be read or written. A Word
document has to be filled. A date has to be turned into "next Monday".
A mail has to be put together with an attachment. You will meet the
same small set of building blocks in almost every recipe of this book,
so it pays to know them before you read the recipes.

jdBasic keeps these building blocks in *libraries*: ordinary jdBasic
files that someone wrote once so that nobody has to write them again.
A program asks for a library with `IMPORT`. The line `IMPORT XLSX`
loads the file `xlsx.jdb`, and from then on the program can call
everything that file offers under the name `XLSX`, such as
`XLSX.READ`. jdBasic looks for the file in a fixed order: in the folder
of the program itself, in a `modules` folder next to it, in the folders
listed in the environment variable `JDBASIC_PATH`, in the folder
`.jdbasic\lib` inside your home folder, and finally in the `lib` folder
next to `jdbasic.exe`. The libraries of this chapter come with jdBasic
and live in that last folder. The setup wizard puts the one module that
belongs to the book, `WORKCONF`, into `.jdbasic\lib`, so every recipe
finds it wherever you keep the recipe.

> **Do not name a program after a library**
> Never call your own program after a library. A program saved as
> `conf.jdb` that says `IMPORT CONF` finds itself first, in its own
> folder, and the import fails with "Undefined function". Give your
> programs names such as `my_settings.jdb`.

Each section below covers one library: the problem it solves, the few
calls the recipes use, one example, and the recipes in Chapter 3 that
rely on it. The examples are complete programs. You can paste each one
into a file, run it with `jdbasic`, and see exactly the output printed
under it. They work in a temporary folder made with `MKTEMP$` and clean
up after themselves, so they leave nothing behind on your computer.

You do not have to learn these calls by heart. Read the sections once,
then come back here when a recipe uses a library and you want to know
what a line does. The full description of every library is in the
`lib` folder of jdBasic, in a file called `<name>_lib_readme.md`.

### CONF: settings you can edit by hand

Every automation needs settings: which folder to tidy, how many
backups to keep, when the working day ends. They belong in a file that
a person can read and change without touching the program. CONF reads
three common formats: dotenv files (`KEY=value` lines), INI files with
`[sections]`, and TOML, which the book uses for `work.conf`.

TOML looks like INI but knows types. Text stands in quotes, numbers and
`true` or `false` stand bare, and lists stand in square brackets:

```toml
[backup]
folders = ["~/Documents", "~/Pictures"]
keep = 10
zip = true
```

The calls the recipes use:

- `CONF.TOML(path$)` reads a TOML file into a map. `CONF.TOML_TEXT(text$)`
  does the same for text you already have.
- `CONF.GET(cfg, "backup.keep", 5)` walks into the map along the dotted
  path and answers the value there, or the fallback `5` when the key is
  missing. This is the call that lets every recipe work with a short
  `work.conf`: whatever you leave out keeps its default.
- `CONF.ENV(path$)` and `CONF.INI(path$)` read the two other formats,
  for files that other programs give you.

```basic
IMPORT CONF

DIM L$ = CHR$(10)
DIM Q$ = CHR$(34)
DIM src$ = "[backup]" + L$ + "folders = [" + Q$ + "~/Documents" + Q$
src$ = src$ + ", " + Q$ + "~/Pictures" + Q$ + "]" + L$
src$ = src$ + "keep = 10" + L$ + "zip = true" + L$
DIM cfg = CONF.TOML_TEXT(src$)
PRINT CONF.GET(cfg, "backup.keep", 5)
PRINT JOIN(CONF.GET(cfg, "backup.folders", []), " and ")
PRINT CONF.GET(cfg, "backup.zip", FALSE)
PRINT CONF.GET(cfg, "backup.target", "~/Backups")
```

```text
10
~/Documents and ~/Pictures
TRUE
~/Backups
```

The last line shows the fallback at work: the text has no `target`, so
`CONF.GET` answers the default the program passed in.

**Where the recipes use it.** Every recipe reads its part of
`work.conf` through the book's own module `WORKCONF`, which calls
`CONF.TOML` and `CONF.GET` and adds three conveniences: it finds
`work.conf` (after `--config`, in the variable `AUTOMATEWORK_CONF`, or
in your work folder), it turns a leading `~` into your home folder,
and it reads switches such as `--dry-run`. When you see
`WORKCONF.VALUE(cfg, key$ + "keep", 10)` in a recipe, that is
`CONF.GET` underneath.

> **Backslashes in TOML**
> Inside double quotes TOML reads a backslash as the start of an escape,
> so `"C:\Users\mia"` does not mean what it says. `CONF.TOML` stops
> at such a path with an error that names the file and the line. Write
> paths with forward slashes, `"C:/Users/mia/Documents"`, or start them
> with `~`. Windows accepts forward slashes everywhere a program opens a
> file.

### WORKCONF: the book's own settings module

WORKCONF is the one module that belongs to this book rather than to
jdBasic. It is short, about seventy lines, and you will find it in the
folder `recipes/lib` next to the recipes. It answers the questions every
recipe asks before it does anything: where is `work.conf`, what does it
say, and did the person start the program with `--dry-run`?

- `WORKCONF.CONFIG_PATH$()` finds the settings file. It looks after
  `--config` on the command line first, then in the environment
  variable `AUTOMATEWORK_CONF`, then in `Documents/AutomateWork/config`
  inside your home folder, where the setup wizard writes it.
- `WORKCONF.SETTINGS(path$)` reads the file. When it is missing, the
  error says so and tells you to run the wizard, instead of a recipe
  failing later with a puzzling message.
- `WORKCONF.VALUE(cfg, key$, fallback)` reads one value, like
  `CONF.GET`.
- `WORKCONF.FOLDER$(cfg, key$, fallback$)` reads a folder and turns a
  leading `~` into your home folder, so `~/Downloads` works on every
  computer.
- `WORKCONF.FLAG("dry-run")` is `TRUE` when the program was started
  with `--dry-run`, and `WORKCONF.ARG$("config")` answers the word after
  `--config`.

```basic
IMPORT WORKCONF

DIM folder$ = MKTEMP$("wcdemo")
MKDIR folder$
DIM file$ = folder$ + "/work.conf"
DIM L$ = CHR$(10)
DIM Q$ = CHR$(34)
DIM src$ = "[downloads_butler]" + L$ + "min_age_hours = 48" + L$
src$ = src$ + "source = " + Q$ + "~/Downloads" + Q$ + L$
TXTWRITER file$, src$
DIM cfg = WORKCONF.SETTINGS(file$)
DIM key$ = "downloads_butler."
PRINT WORKCONF.VALUE(cfg, key$ + "min_age_hours", 24)
PRINT WORKCONF.VALUE(cfg, key$ + "target", "(not set)")
DIM src_dir$ = WORKCONF.FOLDER$(cfg, key$ + "source", "")
PRINT src_dir$ = PATH.JOIN$(WORKCONF.HOME$(), "Downloads")
PRINT WORKCONF.FLAG("dry-run")
KILL file$
RMDIR folder$
```

```text
48
(not set)
TRUE
FALSE
```

The third line compares instead of printing the folder, because your
home folder has a different name from the one on the computer this
book was built on. The last line is `FALSE` because the example was
started without `--dry-run`.

Every recipe begins with the same three lines: import WORKCONF and the
recipe's own module, find and read the settings, and read the values
it needs with a default for each. When you write your own automations
later in the book, start them the same way, and they will read the
same `work.conf` as everything the wizard set up.

### LOGGER: a diary for programs that run alone

A program that runs at half past six in the evening while you are
already at home cannot tell you what it did. It has to write it down.
LOGGER writes one line per event, with the time, a level and the
message, to the console, to a text file, or to a file of JSON lines
that other tools can read. It is called LOGGER because `LOG` already
means the logarithm in jdBasic.

Levels sort the events by weight: `DEBUG` for details you only want
while you look for a problem, `INFO` for the normal course of things,
`WARN` for something that deserves a look, `ERROR` for a failure. A
logger has a threshold, and records below it are dropped.

- `LOGGER.NEW(name$)` makes a logger; the name appears in every line.
- `LOGGER.LEVEL(lg, "INFO")` sets the threshold.
- `LOGGER.TO_FILE(lg, path$, [max_bytes], [keep])` sends the lines to a
  file. With `max_bytes` the file is rotated when it grows past that
  size, and `keep` old files are kept.
- `LOGGER.INFO(lg, message$, [fields])`, and `WARN`, `ERROR`, `DEBUG`
  in the same form, write a record. The optional map adds fields such
  as `files=214` to the line.

```basic
IMPORT LOGGER

DIM folder$ = MKTEMP$("logdemo")
MKDIR folder$
DIM file$ = folder$ + "/jobs.log"
DIM lg = LOGGER.NEW("backup")
LOGGER.LEVEL(lg, "INFO")
LOGGER.TO_FILE(lg, file$)
LOGGER.INFO(lg, "archive written", {"files": 214})
LOGGER.DEBUG(lg, "this one is below INFO")
LOGGER.WARN(lg, "disk almost full")
DIM rows = SPLIT(TRIM$(TXTREADER$(file$)), CHR$(10))
PRINT LEN(rows); " lines"
PRINT MID$(rows[0], 20, LEN(rows[0]))
PRINT MID$(rows[1], 20, LEN(rows[1]))
KILL file$
RMDIR folder$
```

```text
2 lines
INFO  [backup] archive written files=214
WARN  [backup] disk almost full
```

The file has two lines, because the `DEBUG` record lies below the
threshold. The example prints each line from its twentieth character
on: the first twenty hold the date and the time, which differ on every
run.

**Where the recipes use it.** The easy recipes print what they do, and
the Task Scheduler keeps their output for you. The systems of
Chapter 5 run for weeks, and there LOGGER is everywhere: the personal
job server of X01 writes a log line for each run, retry and
failure, and the health checks of X13 read those lines back.

### DT: dates as numbers you can count with

Office work is full of dates. Invoices are due in fourteen days.
Reports are for last month. A file that arrived more than a day ago
should be sorted away. Dates written as text are hard to compute with,
so DT turns them into plain numbers: the seconds since the first of
January 1970. Two dates subtract like any two numbers, and DT does the
calendar arithmetic that plain numbers cannot, such as "one month
later" or "the start of the week".

- `DT.PARSE(text$)` reads the date formats that turn up in files: ISO
  (`2026-10-30`, `2026-10-30T16:45:00Z`), the form mails use
  (`Fri, 30 Oct 2026 16:45:00 +0100`) and the German `30.10.2026 16:45`.
- `DT.FMT$(t, "%d.%m.%Y")` writes a date in the form you want; `%A` is
  the weekday, `%B` the month, `%H:%M` the time.
- `DT.ADD(t, "+1 month -2 days")` moves a date by a spoken amount.
- `DT.DIFF(a, b, "days")` measures from one date to another in a unit.
- `DT.STARTOF(t, "week")` and `DT.ENDOF` give the first and the last
  moment of a day, a week (starting on Monday), a month or a year.
- `DT.IS_LEAP(year)` and `DT.DAYS_IN(year, month)` answer the calendar
  questions behind birthdays and month ends.

```basic
IMPORT DT

DIM t = DT.PARSE("2026-10-30T16:45:00Z")
PRINT DT.FMT$(t, "%A %d %B %Y, %H:%M")
PRINT DT.FMT$(DT.ADD(t, "+1 month"), "%d.%m.%Y")
DIM friday = DT.STARTOF(t, "day")
DIM first = DT.PARSE("2026-10-01")
PRINT DT.DIFF(first, friday, "days"); " days since the 1st"
PRINT DT.WEEKDAY$(t, "de")
PRINT DT.IS_LEAP(2028), DT.DAYS_IN(2026, 2)
```

```text
Friday 30 October 2026, 16:45
30.11.2026
29 days since the 1st
Freitag
TRUE 28
```

DT has no time zone database. Every call takes an optional last number,
the offset from UTC in hours (`1` for Berlin in winter, `2` in summer),
and `DT.LOCAL_OFFSET()` tells you the offset your computer uses right
now. Without the number DT reads and writes UTC. That is why the
examples in this book write their dates with a `Z` at the end: they
then print the same result on every computer.

**Where the recipes use it.** E09 Meeting Notes Starter finds the next
meeting with `DT.PARSE` and writes its date into the notes with
`DT.FMT$`. E12 Birthday Reminder counts the days to each birthday with
`DT.DIFF`, and asks `DT.IS_LEAP` what to do with a birthday on the 29th
of February. The core language has date functions too (`NOW`,
`DATEADD`, `DATEDIFF`, `FORMAT_DATE`), which E01 uses for the age of a
download; DT is the choice when a date comes from a file.

### DF: tables, and the CSV builtins

Many chores are about tables: a list of hours, an export from the
accounting program, the contacts of a team. A *data frame* is a table
kept as named columns. DF loads one from a CSV file, adds computed
columns, filters and sorts rows, groups them and sums them up, the
everyday half of what a spreadsheet does, but in a program that runs
the same way every week.

- `DF.READCSV(path$)` loads a CSV file with a header row; numbers come
  back as numbers.
- `DF.NROWS(df)` and `DF.NAMES(df)` answer the size and the column
  names; `DF.COL(df, "hours")` is a whole column as an array, and
  `DF.ROW(df, i)` one row as a map.
- `DF.WHERE(df, mask)` and `DF.SORT(df, "total", TRUE)` pick and order
  rows; `TRUE` sorts from the largest down.
- `DF.GROUPBY(df, "project", {"total": ["sum", "hours"]})` makes one row
  per project with the sum of its hours. Instead of `sum` you can ask
  for `mean`, `min`, `max`, `median` or `count`.
- `DF.WRITECSV(df, path$)` writes the table back.

```basic
IMPORT DF

DIM folder$ = MKTEMP$("dfdemo")
MKDIR folder$
DIM file$ = folder$ + "/hours.csv"
DIM rows = [["day", "project", "hours"], ["Mon", "Miller", 3.5], _
    ["Mon", "Office", 2], ["Tue", "Miller", 5], _
    ["Wed", "Office", 1.5]]
CSVWRITER file$, rows
DIM hours = DF.READCSV(file$)
PRINT DF.NROWS(hours); " rows, columns "; JOIN(DF.NAMES(hours), ", ")
DIM aggs = {"total": ["sum", "hours"]}
DIM per = DF.SORT(DF.GROUPBY(hours, "project", aggs), "total", TRUE)
DIM k = 0
FOR k = 0 TO DF.NROWS(per) - 1
    DIM r = DF.ROW(per, k)
    PRINT r{"project"}; ": "; r{"total"}; " hours"
NEXT k
KILL file$
RMDIR folder$
```

```text
4 rows, columns day, project, hours
Miller: 8.5 hours
Office: 3.5 hours
```

For a small file you do not always need a whole data frame. jdBasic has
three builtins for CSV that need no `IMPORT` at all. `CSVWRITER` writes
a two-dimensional array, with an optional header row; `CSVHEADER` reads
the names in the first line; `CSVREADER` reads the rows back. The last
argument of `CSVREADER` names the type of each column, so that a date
such as `1975-11-02` stays text instead of being read as a calculation:

```basic
DIM folder$ = MKTEMP$("csvdemo")
MKDIR folder$
DIM file$ = folder$ + "/contacts.csv"
DIM people = [["Ann Miller", "1980-02-29"], _
    ["Ben Ortiz", "1975-11-02"]]
CSVWRITER file$, people, ",", ["name", "birthday"]
PRINT CSVHEADER(file$)
DIM rows = CSVREADER(file$, ",", TRUE, ["STRING", "STRING"])
PRINT LEN(rows); " contacts"
PRINT rows[1][0]; " was born on "; rows[1][1]
KILL file$
RMDIR folder$
```

```text
[name, birthday]
2 contacts
Ben Ortiz was born on 1975-11-02
```

> **Commas inside a field**
> `CSVWRITER` puts a field that holds a comma in quotes, as the CSV
> format asks: `"Miller, Ann"`. The current `CSVREADER` and
> `DF.READCSV` do not understand those quotes yet and split the field
> at the comma. Until that is fixed, keep commas out of the values you
> write, as E07 does with the names of projects.

**Where the recipes use it.** E07 Time Tracker keeps its log with
`CSVWRITER` and reads it back with `CSVREADER`. E12 Birthday Reminder
reads the contact list the same way. E09 Meeting Notes Starter gets its
meetings from ICAL as a data frame and reads it with `DF.ROW`. The
report pipeline of X09 in Chapter 5 is built on DF from end to end.

### XLSX: Excel files without Excel

Sooner or later every result has to go to someone who wants a
spreadsheet. XLSX writes real `.xlsx` files that Excel, LibreOffice and
Google Sheets open: several sheets, a bold header row, column widths,
number formats, cell colours, formulas and frozen rows. It also reads
any `.xlsx` back as tables of values. No Excel has to be installed,
which matters for a program that runs at night on a computer where
nobody is logged in.

- `XLSX.NEW()` starts a workbook.
- `XLSX.SHEET(wb, name$, rows)` adds a sheet from a two-dimensional
  array; the first row becomes the bold header. A text that starts with
  `=` is a formula.
- `XLSX.COLUMN(sheet, "B", {"width": 12, "format": "0.0"})` sets the
  width in characters and the Excel number format of a column.
- `XLSX.FREEZE(sheet, 1)` keeps the header row in view while you
  scroll.
- `XLSX.WRITE(path$, wb)` writes the file. `XLSX.READ(path$)` answers
  every sheet as a table, keyed by the sheet's name, and
  `XLSX.SHEETS(path$)` lists the names.

```basic
IMPORT XLSX

DIM folder$ = MKTEMP$("xlsxdemo")
MKDIR folder$
DIM file$ = folder$ + "/week.xlsx"
DIM wb = XLSX.NEW()
DIM rows = [["Project", "Hours"], ["Miller", 8.5], ["Office", 3.5], _
    ["Total", "=SUM(B2:B3)"]]
DIM sh = XLSX.SHEET(wb, "Week 44", rows)
XLSX.COLUMN(sh, "A", {"width": 20})
XLSX.COLUMN(sh, "B", {"format": "0.0"})
XLSX.FREEZE(sh, 1)
PRINT XLSX.WRITE(file$, wb); " parts written"
PRINT JOIN(XLSX.SHEETS(file$), ", ")
DIM back = XLSX.READ(file$)
DIM sheet = back{"Week 44"}
PRINT sheet[1][0]; " "; sheet[1][1]
PRINT LEN(sheet); " rows"
KILL file$
RMDIR folder$
```

```text
7 parts written
Week 44
Miller 8.5
4 rows
```

An `.xlsx` file is a ZIP archive of XML parts; "7 parts written" is how
many the workbook needed. The formula `=SUM(B2:B3)` reads back as an
empty cell, because XLSX writes the formula and leaves the computing
to Excel when the file is opened.

**Where the recipes use it.** E07 Time Tracker writes the weekly sheet
of your hours. E10 Duplicate Finder writes its report of files stored
twice, one row per copy. In Chapter 4, M02 Report Merger reads ten
colleagues' sheets with `XLSX.READ` and writes the summary.

### DOCX: Word documents without Word

Meeting notes, offers, letters and timesheets live in Word. DOCX writes
`.docx` files with headings, paragraphs with **bold** and *italic*
words, bullet and numbered lists, tables, page breaks, a header and a
footer, pictures and a table of contents. It fills the `{{placeholders}}`
of a Word document you prepared, which is how mail merge works in
Chapter 4. And it reads the paragraphs and tables of any document back,
so a program can check what a document says.

- `DOCX.NEW()` starts a document; `DOCX.LANGUAGE(doc, "en-US")` sets
  the language Word checks the spelling in.
- `DOCX.HEADING(doc, text$, [level])` and `DOCX.PARAGRAPH(doc, text$)`
  add text. Two stars around a word make it bold, one star italic.
- `DOCX.BULLETS(doc, items)`, `DOCX.NUMBERED(doc, items)` and
  `DOCX.TABLE(doc, rows)` add lists and tables.
- `DOCX.WRITE(doc, path$)` writes the file.
- `DOCX.FILL(template$, target$, values)` copies a Word file and
  replaces its placeholders with the values of a map.
- `DOCX.READ(path$)` answers a map with the `paragraphs` and the
  `tables` of a document.

```basic
IMPORT DOCX

DIM folder$ = MKTEMP$("docxdemo")
MKDIR folder$
DIM file$ = folder$ + "/notes.docx"
DIM doc = DOCX.NEW()
DOCX.LANGUAGE(doc, "en-US")
DOCX.HEADING(doc, "Project meeting, 30 October")
DOCX.PARAGRAPH(doc, "Present: **Mia**, Jonas, Priya")
DOCX.BULLETS(doc, ["Budget", "Next release", "Holidays"])
DOCX.TABLE(doc, [["Task", "Who"], ["Send the offer", "Mia"]])
DOCX.WRITE(doc, file$)
DIM back = DOCX.READ(file$)
DIM paras = FILTER(LAMBDA p$ -> p$ <> "", back{"paragraphs"})
PRINT LEN(paras); " paragraphs, the first: "; paras[0]
PRINT back{"tables"}[0][1][0]; " ("; back{"tables"}[0][1][1]; ")"
KILL file$
RMDIR folder$
```

```text
5 paragraphs, the first: Project meeting, 30 October
Send the offer (Mia)
```

The heading, the paragraph and the three list items are five
paragraphs. Reading the file back shows the text without its marks: the
stars around *Mia* became bold type in Word, and `DOCX.READ` answers the
plain words. The empty paragraphs the example filters away are the ones
Word needs after a table.

**Where the recipes use it.** E09 Meeting Notes Starter writes the notes
file for your next meeting with a heading, the attendees and an empty
table for the decisions. In Chapter 4, M01 writes invoices, M03 fills
letters from a template with `DOCX.FILL`, M14 compares two versions of
a contract with `DOCX.READ`, and M15 writes the monthly timesheet. The
build of this book also writes a Word file with DOCX, for reviewing.

### PDFGEN: pages to print and to send

A PDF looks the same on every screen and every printer, which makes it
the right format for a page you print for your desk, a card you hang in
the meeting room or an invoice you send. PDFGEN writes PDF files
without any other program. You place text, lines, boxes, pictures and
tables on a page by position in millimetres, measured from the top left
corner. Paragraphs wrap and tables continue on a new page by
themselves.

- `PDFGEN.DOC({"size": "A5"})` starts a document; sizes A3, A4, A5,
  Letter and Legal, or any size with `"width"` and `"height"` in mm.
- `PDFGEN.ADDPAGE(pdf)` starts a page.
- `PDFGEN.USEFONT(pdf, "helvetica", "B", 16)` picks a font, a style
  (`B` bold, `I` italic) and a size in points. The standard fonts
  Helvetica, Times and Courier are always there; `PDFGEN.ADDFONT` adds
  a TrueType font file for any other look.
- `PDFGEN.TEXTAT(pdf, x, y, text$)` writes text at a position;
  `PDFGEN.MULTICELL` writes a paragraph that wraps;
  `PDFGEN.TABLE(pdf, rows, {"headers": [...]})` writes a table.
- `PDFGEN.IMAGE(pdf, path$, x, y, width)` places a JPEG or PNG picture.
- `PDFGEN.WRITEFILE(pdf, path$)` writes the file;
  `PDFGEN.BUILD$(pdf)` answers its bytes instead.

```basic
IMPORT PDFGEN

DIM pdf = PDFGEN.DOC({"size": "A5"})
PDFGEN.SETINFO(pdf, "Week 44", "Mia")
PDFGEN.ADDPAGE(pdf)
PDFGEN.USEFONT(pdf, "helvetica", "B", 16)
PDFGEN.TEXTAT(pdf, 15, 20, "Week 44")
PDFGEN.USEFONT(pdf, "helvetica", "", 10)
PDFGEN.SETXY(pdf, 15, 28)
DIM todo$ = "Send the offer to Miller. Book the room for Thursday."
PDFGEN.MULTICELL(pdf, 0, 5, todo$)
DIM opts = {"headers": ["Day", "Task"]}
PDFGEN.TABLE(pdf, [["Mon", "Offer"], ["Thu", "Room"]], opts)
DIM bytes$ = PDFGEN.BUILD$(pdf)
PRINT LEFT$(bytes$, 8)
PRINT PDFGEN.PAGECOUNT(pdf); " page, ";
PRINT PDFGEN.PAGEWIDTH(pdf); " mm wide"
PRINT INSTR(bytes$, "(Week 44) Tj") >= 0
```

```text
%PDF-1.4
1 page, 148 mm wide
TRUE
```

The example builds the PDF in memory and looks into it: every PDF file
starts with `%PDF`, A5 is 148 millimetres wide, and the heading stands
in the file as the text command `(Week 44) Tj`. To keep the page,
replace `PDFGEN.BUILD$` with `PDFGEN.WRITEFILE(pdf, "week.pdf")`.

**Where the recipes use it.** E13 Printable Week turns your to-do file
into a page for the desk. E15 Guest Wi-Fi Card draws the cards with
the QR code. The print and the screen edition of this book are set with
PDFGEN too, with embedded TrueType fonts, bookmarks and links.

### MAIL: messages built, saved and read back

Status mails, invoices, reminders: much of what an office sends is
written to a pattern. MAIL builds a message the way a mail program
does, with a plain text and an HTML version, pictures inside the HTML,
attachments, and names and subjects with umlauts. It saves the message
as an `.eml` file that every mail program opens, or sends it through
your mail server. It also reads a saved message back into its parts,
which is how a program can sort the attachments of the mails you save.

- `MAIL.MESSAGE(from$, to, subject$)` starts a message; an address may
  carry a name, such as `Mia <mia@example.com>`.
- `MAIL.PLAIN(msg, text$)` and `MAIL.HTMLBODY(msg, html$)` set the text
  and the HTML version; `MAIL.ATTACH(msg, path$)` adds a file.
- `MAIL.WRITEEML(msg, path$)` saves the message as a file.
- `MAIL.SEND(msg, server)` sends it; `server` is a map with the
  `url` of the mail server, the `user` and the `password`.
- `MAIL.PARSEFILE(path$)` reads an `.eml` file back into a map with
  `subject`, `from`, `to`, `text`, `html` and the attachments.

```basic
IMPORT MAIL

DIM folder$ = MKTEMP$("maildemo")
MKDIR folder$
DIM file$ = folder$ + "/status.eml"
DIM msg = MAIL.MESSAGE("Mia <mia@example.com>", _
    "team@example.com", "Status, week 44")
DIM body$ = "Done: the Miller offer. Open: the room for Thursday."
MAIL.PLAIN(msg, body$)
MAIL.SETDATE(msg, "Fri, 30 Oct 2026 16:00:00 +0100")
MAIL.WRITEEML(msg, file$)
DIM back = MAIL.PARSEFILE(file$)
PRINT back{"subject"}
PRINT back{"to"}[0]
PRINT TRIM$(back{"text"})
KILL file$
RMDIR folder$
```

```text
Status, week 44
team@example.com
Done: the Miller offer. Open: the room for Thursday.
```

`MAIL.SETDATE` gives the message a fixed date. Without it, a message
carries the moment it was built, which is what you want in real use.

> **Your mail password**
> A program that sends mail needs the password of your mail account.
> The recipes of this book never store it in `work.conf`. They ask for
> it when they run by hand, or they put the finished message into an
> outbox folder as an `.eml` file that you open and send yourself.

**Where the recipes use it.** No easy recipe sends mail, which is why
the setup wizard can promise that your files and mail stay on your
computer. From
Chapter 4 on, mail is everywhere: M01 sends invoices, M03 serial
letters, M06 the Friday status, M07 unpacks the attachments of saved
mails with `MAIL.PARSEFILE`, and X02 drafts replies to your inbox.

### ICAL: calendars

Your calendar knows when you are busy, and every calendar program can
save it as an `.ics` file and subscribe to one. ICAL reads and writes
those files. It understands repeating events, such as "every Monday at
nine, eight times", and expands them into the single dates they stand
for. That turns a calendar into data a program can work with: the next
meeting, the hours spent in meetings last month, the shift plan of a
team.

- `ICAL.PARSEFILE(path$)` reads a calendar; `ICAL.EVENTS(cal)` answers
  its events.
- `ICAL.PROP$(cal, event, "SUMMARY")` reads a property of an event,
  such as its title, place or description.
- `ICAL.EXPAND(cal, from, to)` answers every event between two dates
  as a DF table with `start`, `end` and `summary`, repeating events
  expanded. `ICAL.OCCURRENCES(cal, event, from, to)` does the same for
  one event.
- `ICAL.NEW()`, `ICAL.ADDEVENT(cal, title$, start, end)` and
  `ICAL.SETPROP(cal, event, name$, value$)` build a calendar;
  `ICAL.WRITEFILE(cal, path$)` saves it.

```basic
IMPORT ICAL, DT

DIM cal = ICAL.NEW()
DIM start = DT.PARSE("2026-10-05T09:00:00Z")
DIM ev = ICAL.ADDEVENT(cal, "Team meeting", start, start + 1800)
ICAL.SETPROP(cal, ev, "RRULE", "FREQ=WEEKLY;BYDAY=MO;COUNT=8")
DIM from = DT.PARSE("2026-10-19T00:00:00Z")
DIM upto = DT.PARSE("2026-11-03T00:00:00Z")
DIM times = ICAL.OCCURRENCES(cal, ev, from, upto)
DIM k = 0
FOR k = 0 TO LEN(times) - 1
    PRINT DT.FMT$(times[k], "%a %d %b %H:%M"); "  ";
    PRINT ICAL.PROP$(cal, ev, "SUMMARY")
NEXT k
PRINT LEFT$(ICAL.TEXT$(cal), 15)
```

```text
Mon 19 Oct 09:00  Team meeting
Mon 26 Oct 09:00  Team meeting
Mon 02 Nov 09:00  Team meeting
BEGIN:VCALENDAR
```

The rule `FREQ=WEEKLY;BYDAY=MO;COUNT=8` is the way calendars write
"weekly on Monday, eight times". ICAL answers the three Mondays that
fall into the two weeks asked for. The last line shows the start of the
text an `.ics` file holds.

**Where the recipes use it.** E09 Meeting Notes Starter reads your
calendar file and finds the next meeting. E12 Birthday Reminder writes
the birthdays of your contacts as a calendar of yearly events that you
can import into Outlook or Google. In Chapter 4, M05 builds the shift
and vacation plan of a team as a calendar and M12 sums up the hours you
spend in meetings.

### SCHED: when something should run

"Every weekday at half past seven", "on the last day of the month",
"every fifteen minutes": schedules are easy to say and fiddly to
compute. SCHED reads them and answers when the next run is due. It
understands the five-field cron notation that servers use
(`0 7 * * mon-fri`) and plain words such as `daily 18:30`,
`weekly fri 16:00` and `every 90 minutes`. It also has a job loop that
runs several jobs on time inside one long-running program, with
retries, locks and a log.

- `SCHED.NEXTRUN(when$, t)` answers the first run after the moment `t`.
- `SCHED.UPCOMING(when$, t, n)` answers the next `n` runs.
- `SCHED.VALID(when$)` tells whether a schedule can be read, and
  `SCHED.PROBLEM$(when$)` why not.
- `SCHED.JOB(name$, when$, fn, [opts])` adds a job to the loop, and
  `SCHED.SERVE(0)` runs the loop until the program is stopped.

```basic
IMPORT SCHED, DT

DIM t = DT.PARSE("2026-10-30T12:00:00Z")
PRINT DT.ISO$(SCHED.NEXTRUN("daily 18:30", t))
PRINT DT.ISO$(SCHED.NEXTRUN("weekly mon 08:00", t))
DIM runs = SCHED.UPCOMING("every 90 minutes", t, 3)
PRINT JOIN(SELECT(LAMBDA r -> DT.FMT$(r, "%H:%M"), runs), " ")
PRINT SCHED.VALID("0 7 * * mon-fri"), SCHED.VALID("tomorrow")
```

```text
2026-10-30T18:30:00Z
2026-11-02T08:00:00Z
13:30 15:00 16:30
TRUE FALSE
```

The 30th of October 2026 is a Friday, so the next Monday morning is the
2nd of November. An interval such as `every 90 minutes` counts from
midnight, which is why its runs fall on 13:30, 15:00 and 16:30.

**Where the recipes use it.** The easy recipes leave the timing to the
Windows Task Scheduler, which the setup wizard fills in for you. Their
`recipe.toml` files use the same words SCHED understands, so a schedule
reads the same in both places. In Chapter 5 the personal job server of
X01 replaces the scattered scheduled tasks with one program built on
`SCHED.JOB` and `SCHED.SERVE`.

### TMPL: text with holes

A standard reply, a status mail, a web page: much of what we write is
the same text with a few things changed. TMPL fills a *template*, a
text with holes in double curly braces, from a map of values. Loops
repeat a part for every item of a list, conditions show a part only
when it applies, and filters format a value on the way in.

- `TMPL.RENDERSTR$(template$, model)` fills a template held in a text.
- `TMPL.RENDER$(path$, model)` fills a template file and keeps it in a
  cache until the file changes.
- In the template, `{{ name }}` is replaced by the value of `name`,
  `{% for i in items %}` and `{% endfor %}` repeat what stands between
  them, and `{% if paid %}` and `{% endif %}` show a part only when the
  value is there.
- Filters such as `{{ me | upper }}` or `{{ phone | default:'none' }}`
  change a value before it is written.

```basic
IMPORT TMPL

DIM L$ = CHR$(10)
DIM model AS MAP
model{"name"} = "Mr Miller"
model{"items"} = ["the offer", "the price list"]
model{"me"} = "Mia"
DIM src$ = "Dear {{ name }}," + L$ + "attached are:" + L$
src$ = src$ + "{% for i in items %}* {{ i }}" + L$ + "{% endfor %}"
src$ = src$ + "Kind regards, {{ me | upper }}" + L$
src$ = src$ + "Phone: {{ phone | default:'see the signature' }}"
PRINT TMPL.RENDERSTR$(src$, model)
```

```text
Dear Mr Miller,
attached are:
* the offer
* the price list
Kind regards, MIA
Phone: see the signature
```

The model has no `phone`, so the `default` filter writes its text
instead. TMPL was made for web pages, so it escapes the characters
`& < > " '` that have a meaning in HTML. For a plain mail text that is
usually what you want; where it is not, three braces, `{{{ name }}}`,
write a value exactly as it is.

**Where the recipes use it.** E08 Mail Templates keeps your standard
replies as template files and fills them with the name of the person
and the values from `work.conf`. In Chapter 4, M06 writes the Friday
status mail from a template, and the personal dashboard of M08 renders
its web page with TMPL.

### Looking things up yourself

This chapter shows the calls the recipes need, not every call a
library has. When you want to know more, there are three places to
look, from quick to thorough.

The first is the readme of the library. Each one starts with a short
example, then lists every call in a table with what it does, then
explains the details that tend to surprise people, such as the
offsets of DT or the formula cells of XLSX. The readmes are plain text
files in the `lib` folder of jdBasic; any editor opens them.

The second is the library file itself. A jdBasic library is an
ordinary program, and every function in it that you may call starts
with `EXPORT` and carries a comment saying what it does. Searching the
file for `EXPORT FUNC` lists them all. Reading the code of a function
is the surest way to find out what it does with an unusual input.

The third is the self test. Every library has a test in the folder
`tests/jdlibs` of the jdBasic sources, named after the library, such
as `xlsx_selftest.jdb`. A test is a list of small, exact examples, each
with the answer it expects: often the quickest way to see how a call
behaves at its edges.

For the builtins, the functions that need no `IMPORT` such as
`CSVREADER`, `FILE.MOVE` or `DIR$`, the reference is the file
`help.txt` next to `jdbasic.exe`, and the command `HELP` followed by a
name shows the same text in the jdBasic console.

### Further libraries in later chapters

The medium and expert recipes reach for more of what comes with
jdBasic. Each has its readme in the `lib` folder; the recipe that first
uses one explains the calls it needs.

| Library | What it does | First used in |
|---|---|---|
| QR | QR codes as a matrix, as text or drawn into a PDF | E15 |
| TEXTDIFF | the differences between two texts, line by line | E14, M14 |
| REQ | HTTP requests with sessions, cookies and headers | X04, X11 |
| JDWEB | small web applications and local web pages | M08, X03 |
| SVG | charts and drawings as SVG pictures | M08, M09 |
| VALID, SCHEMA | checking that data has the right form | M11 |
| FUZZY | names that are almost the same | M11 |
| CLI | the arguments of a command line tool | M13 |
| RETRY | trying again after a failure, with growing pauses | X01, X11 |
| SEARCH | full-text search over many documents | X03 |
| JWT, OAUTH | signed tokens and logging in to web services | X04, X05 |
| POOL | spreading work over all the cores of a computer | X08 |
| DB | SQLite databases without hand-written SQL | X09 |
| LLMAPI | one way to talk to a language model, local or online | X02 |
| HTMLDOM, CACHE | reading web pages and keeping what was fetched | X11 |
| TESTKIT, PROPTEST | tests for your own programs | every recipe, X12 |
| METRICS | counters and timings for programs that run alone | X13 |
| SECRET | password hashes and random tokens | X14 |

TESTKIT deserves a word already here: every recipe in this book comes
with a test written with it, a second program that runs the recipe
against made-up files and checks each result. When you change a recipe
to fit your work, run its test afterwards. If the test still passes,
your change kept everything else as it was.
