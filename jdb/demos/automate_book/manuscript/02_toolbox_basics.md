## jdBasic for Office People

This part of the chapter teaches you enough jdBasic to read every program
in Chapter 3 and to change it. It does not try to teach everything. It
teaches the pieces the recipes use, in the order you meet them, and each
piece comes with a small example you can type in and run.

You do not need to work through it in one sitting. Read the first few
sections, set up a recipe or two, and come back when a listing shows you
something you have not seen yet. The recipes point back here.

### What a program is

A program is a text file with instructions, one per line, that the
computer carries out from the top to the bottom. jdBasic programs end in
`.jdb`. You write them in any text editor: Notepad works, Visual Studio
Code is more comfortable because it colours the code.

To run a program, open a command window in the folder where the file is
and type `jdbasic` followed by the file name:

```
jdbasic hello.jdb
```

Here is the shortest useful program. It prints one line:

```basic
PRINT "Good morning, Mia."
```

```text
Good morning, Mia.
```

Every example in this chapter looks like this: first the program, then,
in a lighter box, exactly what it prints. Every one of them was run when
this book was built, and the build would have stopped if a single
character of the output had been different from what you see here.

> **Try this**
> Save the line above as `hello.jdb`, change the name to yours, and run
> it. If the window says it does not know `jdbasic`, the installation in
> the first part of this chapter is not finished yet.

### PRINT

`PRINT` writes text to the screen. Text goes between double quotes. A
semicolon joins several things on one line with nothing in between, a
comma puts a space between them, and `PRINT` alone writes an empty line.

```basic
PRINT "Invoices: "; 12
PRINT "Paid", "Open", "Late"
PRINT
PRINT "done"
```

```text
Invoices: 12
Paid Open Late

done
```

A semicolon at the very end of a line keeps the next `PRINT` on the same
line. You will see that in loops further down.

### Variables and DIM

A variable is a name for a value that the program keeps while it runs.
You create one with `DIM` and give it a value with `=`. After that you
can use the name wherever you would use the value, and you can give it a
new value.

```basic
DIM invoices = 12
DIM paid = 9
PRINT "Open: "; invoices - paid
invoices = invoices + 3
PRINT "After the post came: "; invoices
```

```text
Open: 3
After the post came: 15
```

A name ending in `$` holds text. This is an old BASIC habit, and the
recipes keep it because it makes a listing easier to read: when you see
`folder$`, you know it is text without looking further.

```basic
DIM person$ = "Mia"
DIM greeting$ = "Hello, " + person$
PRINT greeting$
```

```text
Hello, Mia
```

Names may contain letters, digits and the underscore, and capital and
small letters count as the same: `Total` and `total` are one variable.
A few names belong to jdBasic itself. Words of the language such as
`TEXT`, `LINE` or `STEP` cannot be names at all, and the name of a
builtin such as `COUNT` or `SORT` cannot be the name of a function of
your own; a program that tries stops with an error. Appendix A lists
them. The recipes use names such as `n_files` or `line_text$` instead.

### Typing mistakes and OPTION "EXPLICIT"

A variable that was never created has no value, which jdBasic calls
`NONE`. In a calculation, `NONE` counts as 0. That is convenient while
you try things out and treacherous in a program you rely on: a typing
mistake in a name gives no error, only a wrong result.

```basic
DIM total = 10
PRINT totl + 1
PRINT TYPEOF(totl)
```

```text
1
NONE
```

The first line of output is 1, not 11, because `totl` is not `total`.
The line `OPTION "EXPLICIT"` at the top of a program asks jdBasic to
refuse every name that was not created with `DIM`. The program then
stops before it runs and names the line:

```basic
OPTION "EXPLICIT"
DIM total = 10
PRINT totl + 1
```

```text
Error #20: Undeclared variable 'TOTL' (OPTION "EXPLICIT") at line 3
```

The recipes are written so that they would pass this check, and when
you start a program of your own, it is worth writing `OPTION "EXPLICIT"`
as its first line. You will thank yourself the first time it catches a
mistake that would otherwise have moved files to the wrong folder.

### Numbers

jdBasic calculates with `+`, `-`, `*` and `/`. Two more operators help
with whole numbers: `\` divides and drops the remainder, `MOD` gives the
remainder. `^` raises to a power.

```basic
PRINT 7 / 2, 7 \ 2, 7 MOD 2, 2 ^ 10
PRINT 90 / 60, 90 \ 60, 90 MOD 60
```

```text
3.5 3 1 1024
1.5 1 30
```

The second line is the start of every time calculation in this book:
90 minutes are one hour and thirty minutes. The time tracker in E07
works this way.

Numbers with a fractional part sometimes do not come out exactly as you
would write them, because the computer stores them in binary. jdBasic
rounds the printed value, so in practice you rarely notice:

```basic
PRINT 0.1 + 0.2
PRINT ROUND(2 / 3, 2)
```

```text
0.3
0.67
```

### Text and numbers together

`+` between two numbers adds them. Between text and anything else it
joins them into one longer text:

```basic
DIM files = 3
PRINT "You have " + files + " new files."
PRINT "1" + "1", 1 + 1
```

```text
You have 3 new files.
11 2
```

When a program reads a number from a file or from the keyboard, it
arrives as text. `VAL` and `CINT` turn text into a number. `CINT` gives
a whole number and stops with an error when the text is not a number at
all, which is usually what you want; `VAL` answers 0 instead. `STR$` goes
the other way.

```basic
PRINT VAL("3.5") + 1, CINT("12") + 1
PRINT STR$(42) + "!"
PRINT VAL("twelve")
```

```text
4.5 13
42!
0
```

### Text with values inside

Joining text with `+` gets long when a sentence holds several values. A
text that starts with `$` before the quote may hold expressions between
double curly braces, and jdBasic puts in their values:

```basic
DIM n = 3
DIM folder$ = "Downloads"
PRINT $"{{n}} files are waiting in {{folder$}}."
PRINT $"That is {{n * 2}} minutes of sorting."
```

```text
3 files are waiting in Downloads.
That is 6 minutes of sorting.
```

For numbers with a fixed number of decimals, `FORMAT$` takes a pattern
with `{}` where a value goes. `{:.2f}` means two digits after the point,
`{:>8}` means right aligned in eight places:

```basic
PRINT FORMAT$("Total: {:.2f} EUR", 209.8)
PRINT FORMAT$("[{:>8}]", "right")
PRINT FORMAT$("{} of {} done", 4, 15)
```

```text
Total: 209.80 EUR
[   right]
4 of 15 done
```

### Working with text

Most of what the recipes do is looking at names of files and lines of
files, so text functions appear on almost every page of Chapter 3.

`LEN` gives the length of a text. `LEFT$` and `RIGHT$` take characters
from the start and the end. `MID$` takes characters from the middle.

```basic
DIM file$ = "2026-10-01 Offer.pdf"
PRINT LEN(file$)
PRINT LEFT$(file$, 10)
PRINT RIGHT$(file$, 4)
PRINT MID$(file$, 11, 5)
```

```text
20
2026-10-01
.pdf
Offer
```

> **Watch out**
> `MID$` counts from 0, not from 1: the first character is at position
> 0. `MID$(file$, 11, 5)` starts at the twelfth character. Many other
> BASICs count from 1, so code copied from elsewhere may be off by one.

`INSTR` finds one text inside another and answers the position where it
starts, again counted from 0. When the text is not there, it answers -1.

```basic
DIM file$ = "2026-10-01 Offer.pdf"
PRINT INSTR(file$, "Offer")
PRINT INSTR(file$, "Invoice")
IF INSTR(file$, "Offer") >= 0 THEN PRINT "an offer"
```

```text
11
-1
an offer
```

Often you only want to know whether a text appears at all. `IN` answers
that directly, and `STARTSWITH` and `ENDSWITH` look at the two ends:

```basic
DIM file$ = "2026-10-01 Offer.pdf"
PRINT "Offer" IN file$
PRINT STARTSWITH(file$, "2026"), ENDSWITH(file$, ".pdf")
```

```text
TRUE
TRUE TRUE
```

`UCASE$` and `LCASE$` change capital and small letters; they help when
you compare text that people typed, since `"PDF"` and `"pdf"` are
different texts. `TRIM$` removes spaces at both ends. `REPLACE$` changes
every occurrence of one text into another.

```basic
PRINT UCASE$("pdf"), LCASE$("Report.PDF")
PRINT "[" + TRIM$("   Mia  ") + "]"
PRINT REPLACE$("2026-10-01", "-", ".")
PRINT LCASE$("Offer.PDF") = "offer.pdf"
```

```text
PDF report.pdf
[Mia]
2026.10.01
TRUE
```

`SPLIT` cuts a text into a list of pieces at a separator, and `JOIN` puts
a list back together. Both appear wherever a recipe reads a line with
several values in it:

```basic
DIM line_text$ = "Mia;mia@example.com;Projects"
DIM parts = SPLIT(line_text$, ";")
PRINT parts
PRINT parts[1]
PRINT JOIN(parts, " / ")
```

```text
[Mia, mia@example.com, Projects]
mia@example.com
Mia / mia@example.com / Projects
```

> **Umlauts and LEN**
> `LEN` counts bytes, and an umlaut takes two bytes in the UTF-8 text
> jdBasic uses. `LEN("Müller")` is 7. When you need the number of
> characters, write `LEN(-name$)`: the minus in front of a text turns it
> into a list of its characters. `LEFT$` and `MID$` count bytes too, so
> cut names with umlauts at a space or a known position, not in the
> middle of a word.

```basic
DIM name_text$ = "Müller"
PRINT LEN(name_text$), LEN(-name_text$)
PRINT -name_text$
```

```text
7 6
[M, ü, l, l, e, r]
```

### Lists

A list, called an *array* in programming, holds several values in one
variable, in order. You write it in square brackets. `LEN` gives the
number of entries, and a number in square brackets after the name picks
one entry. The first entry has the number 0.

```basic
DIM files = ["offer.pdf", "photo.jpg", "notes.txt"]
PRINT LEN(files)
PRINT files[0]
PRINT files[2]
PRINT files
```

```text
3
offer.pdf
notes.txt
[offer.pdf, photo.jpg, notes.txt]
```

`PUSH` adds an entry at the end. `APPEND` answers a new, longer list and
leaves the old one as it is. `SORT` answers a sorted copy, in reverse
order when you add `TRUE`:

```basic
DIM files = ["offer.pdf", "photo.jpg"]
PUSH(files, "budget.xlsx")
PRINT files
DIM longer = APPEND(files, "agenda.docx")
PRINT LEN(files), LEN(longer)
PRINT SORT(files)
PRINT SORT([3, 10, 7], TRUE)
```

```text
[offer.pdf, photo.jpg, budget.xlsx]
3 4
[budget.xlsx, offer.pdf, photo.jpg]
[10, 7, 3]
```

`IN` works on lists as well, and answers whether a value is one of the
entries. The Downloads Butler in E01 uses exactly this to decide what
kind of file it has in front of it:

```basic
DIM images = ["jpg", "jpeg", "png", "heic"]
PRINT "png" IN images
PRINT "pdf" IN images
```

```text
TRUE
FALSE
```

### Maps

A map stores values under names, the way a phone book stores numbers
under people. You write it in curly braces with `"name": value` pairs,
and you read a value with the name in curly braces after the map. The
names are called *keys*.

```basic
DIM kinds = {"pdf": "Documents", "jpg": "Images"}
PRINT kinds{"pdf"}
kinds{"zip"} = "Archives"
PRINT MAP.KEYS(kinds)
PRINT MAP.EXISTS(kinds, "zip"), MAP.EXISTS(kinds, "exe")
```

```text
Documents
[pdf, jpg, zip]
TRUE FALSE
```

Reading a key that is not in the map gives `NONE`, jdBasic's word for
"no value". The operator `??` picks a default for that case, which keeps
many programs short:

```basic
DIM kinds = {"pdf": "Documents", "jpg": "Images"}
PRINT kinds{"exe"} ?? "Other"
PRINT kinds{"pdf"} ?? "Other"
```

```text
Other
Documents
```

The settings file of the book, `work.conf`, arrives in every recipe as a
map of maps: the part `[downloads_butler]` becomes one map inside it,
with the settings as its keys. That is why the recipes read their
settings with names such as `"downloads_butler.source"`.

### Decisions: IF

`IF` runs a piece of the program only when a condition holds. The
condition compares values: `=` equal, `<>` not equal, `<`, `>`, `<=` and
`>=`. A short decision fits on one line with `THEN`; a longer one has
`ELSEIF` and `ELSE` branches and ends with `ENDIF`.

```basic
DIM size_mb = 1500
IF size_mb > 1000 THEN
    PRINT "a large file"
ELSEIF size_mb > 100 THEN
    PRINT "a medium file"
ELSE
    PRINT "a small file"
ENDIF
IF size_mb > 1000 THEN PRINT "consider moving it to the archive"
```

```text
a large file
consider moving it to the archive
```

Comparisons answer `TRUE` or `FALSE`, and you can print those values or
keep them in a variable. `AND` and `OR` combine two conditions, `NOT`
turns one around:

```basic
DIM hour = 18
DIM weekday = TRUE
PRINT hour >= 17 AND weekday
PRINT hour < 8 OR NOT weekday
PRINT "Mia" = "mia"
```

```text
TRUE
FALSE
FALSE
```

The last line shows that comparing text is exact: capital and small
letters differ. Compare `LCASE$` of both sides when that should not
matter.

`ANDALSO` and `ORELSE` work like `AND` and `OR` but stop as soon as the
answer is clear. That matters when the second condition only makes
sense if the first one holds:

```basic
DIM kinds = {"pdf": "Documents"}
DIM ext$ = "zip"
IF MAP.EXISTS(kinds, ext$) ANDALSO kinds{ext$} = "Documents" THEN
    PRINT "a document"
ELSE
    PRINT "something else"
ENDIF
```

```text
something else
```

### Repeating: loops

`FOR ... NEXT` counts. The variable takes every value from the first
number to the second, and the lines in between run once for each:

```basic
DIM k = 0
FOR k = 1 TO 3
    PRINT "Reminder number "; k
NEXT k
FOR k = 10 TO 0 STEP -5
    PRINT k; " ";
NEXT k
PRINT
```

```text
Reminder number 1
Reminder number 2
Reminder number 3
10 5 0
```

`FOR EACH` walks through a list, one entry after the other, and is the
loop you will see most often in Chapter 3. With two names it gives the
position as well:

```basic
FOR EACH file$ IN ["offer.pdf", "photo.jpg"]
    PRINT "found "; file$
NEXT
FOR EACH i, file$ IN ["offer.pdf", "photo.jpg"]
    PRINT i; ": "; file$
NEXT
```

```text
found offer.pdf
found photo.jpg
0: offer.pdf
1: photo.jpg
```

On a map, `FOR EACH` with two names gives each key with its value, in
the order the keys were added:

```basic
DIM hours = {"Project A": 12, "Project B": 7.5}
FOR EACH project$, h IN hours
    PRINT project$; ": "; h; " hours"
NEXT
```

```text
Project A: 12 hours
Project B: 7.5 hours
```

`DO WHILE ... LOOP` repeats as long as a condition holds, and is the loop
for "until there is nothing left to do":

```basic
DIM remaining = 3
DO WHILE remaining > 0
    PRINT "files left: "; remaining
    remaining = remaining - 1
LOOP
PRINT "all sorted"
```

```text
files left: 3
files left: 2
files left: 1
all sorted
```

> **Watch out**
> A `DO WHILE` loop whose condition never becomes false runs for ever.
> If a program of yours seems to hang, press Ctrl+C in its window to stop
> it, and check that something inside the loop changes the condition.

### Dates and times

Half of the recipes in Chapter 3 ask what day it is: the break reminder
wants to know whether the working day has started, the shutdown ritual
whether it is Friday, the time tracker how many hours lie between start
and stop, and the birthday reminder how many days remain. jdBasic keeps
a point in time as a date value. `NOW()` gives the present one, and
`CDATE` makes one from a text in the form year, month, day and
optionally hours, minutes and seconds.

`FORMAT_DATE` turns a date into text with a pattern. `%Y` stands for the
year, `%m` for the month, `%d` for the day, `%H` and `%M` for hours and
minutes, `%A` and `%B` for the names of the weekday and the month:

```basic
DIM start = CDATE("2026-10-01 08:30:00")
PRINT FORMAT_DATE(start, "%Y-%m-%d %H:%M")
PRINT FORMAT_DATE(start, "%A, %d %B %Y")
PRINT YEAR(start), MONTH(start), DAY(start)
```

```text
2026-10-01 08:30
Thursday, 01 October 2026
2026 10 1
```

`DATEADD` moves a date by a number of units: `"D"` for days, `"H"` for
hours, `"N"` for minutes (the M is taken by months). `DATEDIFF` answers
how many units lie between two dates. Note the order of the values:
the unit comes first, then the number or the earlier date.

```basic
DIM start = CDATE("2026-10-01 08:30:00")
PRINT FORMAT_DATE(DATEADD("D", 3, start), "%Y-%m-%d")
PRINT FORMAT_DATE(DATEADD("H", 9, start), "%H:%M")
DIM finish = CDATE("2026-10-01 17:00:00")
PRINT DATEDIFF("H", start, finish)
PRINT DATEDIFF("N", start, CDATE("2026-10-01 09:15:00"))
PRINT DATEDIFF("D", CDATE("2026-10-01"), CDATE("2026-12-24"))
```

```text
2026-10-04
17:30
8.5
45
84
```

The working day in the example is 8.5 hours long, and Christmas is 84
days away. The weekday comes out of `FORMAT_DATE` as well: `%w` gives a
digit, 0 for Sunday, 1 for Monday and so on up to 6 for Saturday. The
recipes use it to tell weekdays from weekends.

```basic
DIM friday = CDATE("2026-10-02 16:00:00")
DIM wd = VAL(FORMAT_DATE(friday, "%w"))
PRINT wd
IF wd = 5 THEN PRINT "Friday: time for the week summary"
IF wd = 0 OR wd = 6 THEN PRINT "weekend"
```

```text
5
Friday: time for the week summary
```

The examples here use fixed dates so that they always print the same.
In the recipes the same lines work on `NOW()`, and the tests of each
recipe hand in a fixed date to check what the program would do on a
given day, such as a Friday or the last day of February.

### Your own functions: FUNC and SUB

When a program does the same thing in several places, you give that
thing a name. A `FUNC` takes values, called *parameters*, calculates
something and hands back the result with `RETURN`. A `SUB` does
something and hands back nothing. A parameter may have a default value,
which is used when the caller leaves it out.

```basic
FUNC Minutes(hours)
    RETURN hours * 60
ENDFUNC

FUNC Greeting$(person$, polite = TRUE)
    IF polite THEN RETURN "Dear " + person$ + ","
    RETURN "Hi " + person$ + ","
ENDFUNC

SUB Banner(title$)
    PRINT "== " + title$ + " =="
ENDSUB

Banner("Monday")
PRINT Minutes(1.5)
PRINT Greeting$("Ms Miller")
PRINT Greeting$("Jonas", FALSE)
```

```text
== Monday ==
90
Dear Ms Miller,
Hi Jonas,
```

As with variables, a function whose name ends in `$` hands back text.
Variables created inside a function belong to it and disappear when it
returns, so two functions can each have their own `k` without getting in
each other's way.

Every recipe in Chapter 3 consists mostly of functions: `KIND$` in E01
decides the kind of a file, `PLAN` decides what to move, and `APPLY`
moves it. Functions with clear names are what makes a program readable a
year later.

### Modules: IMPORT

A module is a file of functions that other programs use. The book comes
with modules for Word, Excel and PDF files, for mail, calendars,
settings and many other things, and every recipe has its own module next
to its program. `IMPORT` makes a module's functions available; you call
them with the module's name, a dot and the function's name:

```basic
IMPORT CONF
DIM q$ = CHR$(34)
DIM cfg = CONF.TOML_TEXT("[mail]" + CHR$(10) + "to = " + q$ + _
    "mia@example.com" + q$)
PRINT CONF.GET(cfg, "mail.to", "nobody")
PRINT CONF.GET(cfg, "mail.cc", "nobody")
```

```text
mia@example.com
nobody
```

The `_` at the end of a line continues the instruction on the next one.
The listings in this book use it to stay narrow enough for the page.

A file becomes a module by starting with `EXPORT MODULE` and its name,
and by marking the functions others may call with `EXPORT`:

```basic
EXPORT MODULE BUTLER

EXPORT FUNC KIND$(file_name$, rules)
    ' ...
ENDFUNC
```

jdBasic looks for a module first in the folder of the program that
imports it, then in a few standard places, among them the folder
`.jdbasic\lib` in your home folder. The setup wizard puts the shared
module of the recipes, `WORKCONF`, there, so every recipe finds it.

### When something fails: TRY and CATCH

Some things go wrong in normal use: a file is missing, a setting holds
text where a number belongs, a web page is not reachable. Without
precautions the program stops with an error message. `TRY` marks the
lines that may fail, and the lines after `CATCH` run instead when they
do. `ERRMSG$` holds the message:

```basic
TRY
    DIM v = CINT("twelve")
    PRINT "a number"
CATCH
    PRINT "Problem: "; ERRMSG$
ENDTRY
TRY
    PRINT TXTREADER$("C:/no/such/folder/notes.txt")
CATCH
    PRINT "Problem: "; ERRMSG$
ENDTRY
PRINT "the program goes on"
```

```text
Problem: CINT: "twelve" is not a number
Problem: TXTREADER$: Cannot open file: C:/no/such/folder/notes.txt
the program goes on
```

Your own program can report a problem the same way with `THROW`. The
recipes do that when a setting is missing, so the message says what to
do about it:

```basic
DIM source$ = ""
TRY
    IF source$ = "" THEN THROW "Set downloads_butler.source first."
CATCH
    PRINT ERRMSG$
ENDTRY
```

```text
Set downloads_butler.source first.
```

### Comments

A line, or the rest of a line, that starts with an apostrophe is a
comment. jdBasic ignores it; it is there for the person who reads the
program. The recipes start every function with a comment that says what
it does, in one or two sentences.

```basic
' The minutes a chore costs in a year, at so many minutes a week.
FUNC YearMinutes(per_week)
    RETURN per_week * 46   ' working weeks, without holidays
ENDFUNC
PRINT YearMinutes(15)
```

```text
690
```

### Money, hours and tidy columns

Reports are the reason many chores exist, and a report is mostly
numbers in columns. Three tools cover what the recipes need. `ROUND`
rounds to a number of decimals. `FORMAT$` with a width keeps columns
straight: `{:<12}` fills to twelve places with the text on the left,
`{:>8.2f}` puts a number with two decimals at the right of eight places.
And `SUM` adds up a whole list at once.

```basic
DIM items = ["Paper", "Toner", "Delivery"]
DIM prices = [45.9, 123.8, 9]
DIM k = 0
FOR k = 0 TO LEN(items) - 1
    PRINT FORMAT$("{:<12}{:>8.2f}", items[k], prices[k])
NEXT k
PRINT FORMAT$("{:<12}{:>8.2f}", "Total", SUM(prices))
PRINT ROUND(178.7 * 0.19, 2)
```

```text
Paper          45.90
Toner         123.80
Delivery        9.00
Total         178.70
33.95
```

Times work the same way once you count in minutes. A time tracker that
stores minutes can print them as hours and minutes with `\` and `MOD`
from the section on numbers:

```basic
FUNC Clock$(minutes)
    DIM h = minutes \ 60
    DIM m = minutes MOD 60
    RETURN STR$(h) + ":" + RIGHT$("0" + STR$(m), 2)
ENDFUNC
PRINT Clock$(95)
PRINT Clock$(480)
PRINT Clock$(7)
```

```text
1:35
8:00
0:07
```

The minutes get a leading zero by putting `"0"` in front and keeping the
last two characters with `RIGHT$`, so seven minutes print as `0:07`, the
way a clock shows them. (`FORMAT$` would fill a width with spaces here,
not with zeros.)

### Asking a question: INPUT

A few recipes ask you something while they run. The shutdown ritual in
E06 asks three questions at the end of the day, the console version of
the setup wizard asks for your name and your working hours. `INPUT`
shows a question and waits until you have typed an answer and pressed
Enter; the answer lands in the variable named after it.

```
DIM answer$ = ""
INPUT "What did you finish today? "; answer$
PRINT "Noted: "; answer$
```

There is no output box here because the program waits for you, and the
build of this book cannot type. That is also the reason the recipes keep
their questions out of their modules: the module receives the answers as
values, the test hands in answers of its own, and only the small program
around it talks to you. When you write a program that asks questions,
this split lets you test everything except the questions themselves.

### A first program of your own

To close the tour, here is a small program built from nothing, the way
you would build one for a chore of your own. The chore: Jonas wants to
know what the meetings of his week cost, counting the time of everyone
in the room. He has a list of meetings with their length in minutes and
the number of people.

The first step is to put the data into the program, as a list of maps.
Later it could come from a file or from the calendar, as in E12 and
E09, but while you develop it is easiest to have it right there:

```basic
DIM meetings = [ _
    {"title": "Team round", "minutes": 60, "people": 8}, _
    {"title": "Customer call", "minutes": 30, "people": 3}, _
    {"title": "Planning", "minutes": 90, "people": 5}]
PRINT LEN(meetings); " meetings"
PRINT meetings[2]{"title"}
```

```text
3 meetings
Planning
```

The second step adds up the person minutes: the length of each meeting
times the people in it. A `FOR EACH` loop over the list and a variable
for the sum do that:

```basic
DIM meetings = [ _
    {"title": "Team round", "minutes": 60, "people": 8}, _
    {"title": "Customer call", "minutes": 30, "people": 3}, _
    {"title": "Planning", "minutes": 90, "people": 5}]
DIM total = 0
FOR EACH m IN meetings
    DIM cost = m{"minutes"} * m{"people"}
    PRINT FORMAT$("{:<15}{:>6} person minutes", m{"title"}, cost)
    total = total + cost
NEXT
PRINT FORMAT$("{:<15}{:>6} person minutes", "Week", total)
PRINT "That is "; ROUND(total / 60, 1); " working hours."
```

```text
Team round        480 person minutes
Customer call      90 person minutes
Planning          450 person minutes
Week             1020 person minutes
That is 17 working hours.
```

The third step turns the calculation into a function, so it can be used
with another week's list, and gives the result a sentence that a person
understands without doing arithmetic. With an hourly rate the hours
become money:

```basic
FUNC PersonHours(meetings)
    DIM total = 0
    FOR EACH m IN meetings
        total = total + m{"minutes"} * m{"people"}
    NEXT
    RETURN total / 60
ENDFUNC
DIM week = [{"minutes": 60, "people": 8}, _
    {"minutes": 30, "people": 3}, {"minutes": 90, "people": 5}]
DIM hours = PersonHours(week)
DIM rate = 55
PRINT FORMAT$("{:.1f} hours, about {:.0f} EUR at {} EUR an hour", _
    hours, hours * rate, rate)
```

```text
17.0 hours, about 935 EUR at 55 EUR an hour
```

Three steps, each one tried before the next: data, calculation,
function. That is how every recipe in this book was written, and the
order is worth keeping for your own programs. A program that you build
in small steps never stops working for long, and when it does, the
mistake is in the few lines you just added. The meeting cost meter M12
in Chapter 4 grows from exactly this start into a program that reads
your real calendar.

### Switches on the command line

Most recipes can do a trial run with `--dry-run`, which shows what they
would do without doing it. Such words after the program's name are
called switches. `OS.ARGS()` answers everything on the command line as a
list: the program's own file first, then every word you typed after it.
`IN` tells whether a switch is among them:

```basic
DIM args = OS.ARGS()
DIM dry_run = "--dry-run" IN args
IF dry_run THEN
    PRINT "only showing what would happen"
ELSE
    PRINT "doing it"
ENDIF
```

```text
doing it
```

Run as an example in this book, the program gets no switches, so it
prints the second line. Typed as `jdbasic program.jdb --dry-run`, it
prints the first. The shared module `WORKCONF` wraps this in
`WORKCONF.FLAG("dry-run")`, and for switches that carry a value, such as
`--config path\to\work.conf`, in `WORKCONF.ARG$("config")`.

### Settings from work.conf

Every recipe takes its settings from one file, `work.conf`, which the
setup wizard writes and which you may change in any text editor. The
file is in a format called TOML: names in square brackets start a part,
and every line below holds a name, an equals sign and a value. Text
stands in double quotes, numbers and `true` or `false` stand alone, and
lists stand in square brackets.

```
[downloads_butler]
source = "~/Downloads"
min_age_hours = 24
skip = ["desktop.ini", "thumbs.db"]
```

A recipe reads this with three lines that look the same in all of them:

```
DIM cfg = WORKCONF.SETTINGS(WORKCONF.CONFIG_PATH$())
DIM key$ = "downloads_butler."
DIM source$ = WORKCONF.FOLDER$(cfg, key$ + "source", "~")
DIM min_age = WORKCONF.VALUE(cfg, key$ + "min_age_hours", 24)
```

The first line finds `work.conf` and reads it. The last two take one
setting each by its part and name, joined with a point, and name a
value to use when the setting is missing. `FOLDER$` also turns a
leading `~` into your home folder. Because every setting has such a
default, a short `work.conf` is enough, and a setting you delete by
mistake does not stop the recipe.

> **Watch out**
> In TOML a backslash inside double quotes starts a special character,
> so `"C:\new"` does not mean what it seems. Write paths with forward
> slashes, `"C:/Users/Mia/new"`, or with `~`; Windows understands both.

### Reading an error message

Sooner or later a program stops with a message instead of doing its
work. The message says what went wrong and where. `Error #13` is the
kind of error, the text after it the explanation, and `at line 2` the
line of the program:

```basic
DIM files = ["offer.pdf"]
PRINT files[3]
```

```text
Error #13: Array index out of bounds: 3 at line 2
```

The list has one entry, at position 0, and the program asked for
position 3. The fix is in line 2. Most messages you will meet in the
recipes are of this kind and say plainly what they miss: a file that is
not there, a setting with text where a number belongs, a folder without
permission to write. The recipes catch the common ones with `TRY` and
turn them into a sentence that tells you what to do, such as "No
settings found at ... Run the setup wizard first".

When the problem is in the shape of the program itself, for example an
`IF` without its `ENDIF`, jdBasic notices before it starts and reports a
parse error. The line it names is where it stopped understanding the
program, which may be some lines below the actual mistake: look upward
from there for the block that was never closed.

### Messages you are likely to see

These are the messages you are most likely to meet while you set up
and change the recipes, with what each one means and what to do.

- **No settings found at ...**: the recipe did not find `work.conf`.
  Either the setup wizard has not run yet, or the recipe was started
  from another folder with a `--config` that points nowhere. Run the
  wizard, or give the path to the file after `--config`.
- **Cannot open file**: a file named in a setting does not exist. Check
  the spelling in `work.conf`, and check that the path uses forward
  slashes or `~`.
- **target exists**: `FILE.MOVE` or `FILE.COPY` refused to overwrite a
  file. The recipes pick a free name before they move, so this appears
  only when another program created the same name in the meantime. Run
  the recipe again.
- **is not a number**: a setting that should hold a number holds text,
  often because it was written in quotes. `min_age_hours = 24` is a
  number, `min_age_hours = "24"` is text.
- **Undeclared variable**: you have added `OPTION "EXPLICIT"` and used a
  name that was never created with `DIM`. Usually it is a typing mistake
  in the name.
- **Array index out of bounds**: a list was asked for a position it does
  not have. Remember that the first entry is at 0 and the last one at
  `LEN(list) - 1`.
- **Parse error**: the program itself is not complete, for example an
  `IF`, `FOR` or `FUNC` without its closing line. Look upward from the
  line in the message.

If a message is not in this list and the recipe's own section "When it
goes wrong" does not mention it either, run the recipe with `--dry-run`
first. The trial run shows how far the recipe gets before it fails, and
that is usually enough to see which file or setting it was looking at.

### How the recipes use all this

The table lists, for each idea of this chapter, a recipe where you can
see it at work. When a listing in Chapter 3 puzzles you, the section
named here is the place to look it up.

| Idea | Where it appears |
|---|---|
| Lists and `IN` | E01 decides the kind of a file from its extension |
| Maps | E07 adds up hours per project |
| `SPLIT` and `JOIN` | E12 reads the contact list line by line |
| `FOR EACH` | every recipe that walks through a folder |
| Dates | E05, E06 and E07 work out the time of day and the weekday |
| `TRY` and `CATCH` | E14 survives a web page that does not answer |
| `FUNC` | every recipe, which is a module of small functions |
| `--dry-run` | every recipe that changes files |
| Settings | every recipe, through `WORKCONF` |

That is the whole language as far as Chapter 3 needs it. The listings
there contain nothing you have not met here, apart from functions from
the modules, and each recipe explains those where it uses them.

## Files and Folders

Most chores in an office are chores with files: they arrive, they pile
up, they need a new name, a new place or a copy. This section shows the
instructions the recipes use for that. The examples create a folder of
their own with `MKTEMP$`, which answers a free name in the folder for
temporary files, and remove it at the end, so you can run them without
worrying about your own files.

### Paths

A path names a file or a folder: `C:\Users\Mia\Downloads\offer.pdf`.
Windows separates the parts with `\`, other systems with `/`, and
jdBasic accepts both. `PATH.JOIN$` puts parts together with the right
separator, which is safer than joining text with `+`:

```basic
PRINT PATH.JOIN$("Reports", "2026", "march.pdf")
DIM file$ = "C:/Users/Mia/Downloads/Offer 2026.pdf"
PRINT PATH.BASENAME$(file$)
PRINT PATH.EXT$(file$)
PRINT PATH.DIRNAME$(file$)
```

```text
Reports\2026\march.pdf
Offer 2026.pdf
.pdf
C:/Users/Mia/Downloads
```

`PATH.EXT$` answers the extension with its point, `".pdf"`. E01 removes
the point and makes the rest small before it looks the extension up in
its lists, so `Offer.PDF` and `offer.pdf` end up in the same folder.

In the settings file a path may start with `~`, which stands for your
home folder: `~/Downloads` is the Downloads folder of whoever runs the
recipe. The shared module `WORKCONF` turns that into the real path.

### Reading and writing text files

`TXTWRITER` writes a text into a file, and `TXTREADER$` reads a whole
file back as one text. With `TRUE` as a third value, `TXTWRITER` adds to
the end of the file instead of replacing it; the shutdown ritual in E06
writes its journal that way.

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
DIM journal$ = PATH.JOIN$(folder$, "journal.txt")
TXTWRITER journal$, "Monday: offer sent" + CHR$(10)
TXTWRITER journal$, "Tuesday: invoices paid" + CHR$(10), TRUE
PRINT TXTREADER$(journal$)
KILL journal$
RMDIR folder$
```

```text
Monday: offer sent
Tuesday: invoices paid
```

`CHR$(10)` is the character that ends a line. To work through a file
line by line, read it and split it there:

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
DIM todo$ = PATH.JOIN$(folder$, "todo.txt")
TXTWRITER todo$, "call Jonas" + CHR$(10) + "! send offer" + CHR$(10)
DIM lines_read = SPLIT(TRIM$(TXTREADER$(todo$)), CHR$(10))
FOR EACH task$ IN lines_read
    IF STARTSWITH(task$, "!") THEN PRINT "important: "; task$
NEXT
KILL todo$
RMDIR folder$
```

```text
important: ! send offer
```

The printable week in E13 reads its to-do file exactly like this.

### Asking about a file

`FILE.EXISTS` answers whether a file or folder is there, `FILE.SIZE`
gives the size of a file in bytes, and `FILE.STAT` answers several
things at once in a map: whether it exists, its size, whether it is a
folder, whether it is hidden, and `mtime`, the date and time of its last
change in your local time.

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
DIM note$ = PATH.JOIN$(folder$, "note.txt")
PRINT FILE.EXISTS(note$)
TXTWRITER note$, "hello"
PRINT FILE.EXISTS(note$), FILE.SIZE(note$)
DIM info = FILE.STAT(note$)
PRINT info{"size"}, info{"is_dir"}, info{"hidden"}
PRINT LEN(info{"mtime"}); " characters, such as 2026-10-01 18:30:00"
KILL note$
RMDIR folder$
```

```text
FALSE
TRUE 5
5 FALSE FALSE
19 characters, such as 2026-10-01 18:30:00
```

The first ten characters of `mtime` are the date, the first seven the
month. E01 sorts files into month folders with `LEFT$(mtime, 7)`.

### Listing a folder

`DIR$` answers the names in a folder as a list. A pattern picks some of
them: `*` stands for any characters, so `*.pdf` finds every PDF. The
names come without the folder in front; join it back with `PATH.JOIN$`
before you do anything with a file.

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
TXTWRITER PATH.JOIN$(folder$, "b.pdf"), "x"
TXTWRITER PATH.JOIN$(folder$, "a.pdf"), "x"
TXTWRITER PATH.JOIN$(folder$, "c.txt"), "x"
PRINT SORT(DIR$(PATH.JOIN$(folder$, "*")))
PRINT SORT(DIR$(PATH.JOIN$(folder$, "*.pdf")))
FOR EACH f$ IN SORT(DIR$(PATH.JOIN$(folder$, "*")))
    KILL PATH.JOIN$(folder$, f$)
NEXT
RMDIR folder$
```

```text
[a.pdf, b.pdf, c.txt]
[a.pdf, b.pdf]
```

The order in which `DIR$` answers depends on the system, so the
examples sort the list before they print it.

### Folders: MKDIR and RMDIR

`MKDIR` creates a folder, together with every folder on the way that
does not exist yet, and does nothing when the folder is already there.
That makes it safe to call before every move. `RMDIR` removes an empty
folder.

```basic
DIM folder$ = MKTEMP$("book")
DIM deep$ = PATH.JOIN$(folder$, "Sorted", "Documents", "2026-10")
MKDIR deep$
MKDIR deep$
PRINT FILE.ISDIR(deep$)
RMDIR deep$
RMDIR PATH.JOIN$(folder$, "Sorted", "Documents")
RMDIR PATH.JOIN$(folder$, "Sorted")
RMDIR folder$
PRINT FILE.EXISTS(folder$)
```

```text
TRUE
FALSE
```

### Copying, moving and deleting

`FILE.COPY` copies a file and `FILE.MOVE` moves or renames it. When the
target is a folder that exists, the file keeps its name inside it. Both
refuse to overwrite a file that is already there, unless you add `TRUE`
as a third value, and both stop with an error that says what went
wrong. `KILL` deletes a file.

```basic
DIM folder$ = MKTEMP$("book")
DIM archive$ = PATH.JOIN$(folder$, "Archive")
MKDIR archive$
DIM offer$ = PATH.JOIN$(folder$, "offer.pdf")
TXTWRITER offer$, "pdf"
FILE.COPY(offer$, PATH.JOIN$(folder$, "offer copy.pdf"))
FILE.MOVE(offer$, archive$)
PRINT SORT(DIR$(PATH.JOIN$(folder$, "*")))
PRINT DIR$(PATH.JOIN$(archive$, "*"))
TRY
    FILE.COPY(PATH.JOIN$(folder$, "offer copy.pdf"), _
        PATH.JOIN$(archive$, "offer.pdf"))
CATCH
    PRINT "refused: the archive already has offer.pdf"
ENDTRY
KILL PATH.JOIN$(archive$, "offer.pdf")
KILL PATH.JOIN$(folder$, "offer copy.pdf")
RMDIR archive$
RMDIR folder$
```

```text
[Archive, offer copy.pdf]
[offer.pdf]
refused: the archive already has offer.pdf
```

> **Watch out**
> `KILL` does not put the file into the recycle bin; it is gone. None of
> the easy recipes deletes anything you created. Where a recipe removes
> files, such as old backups in E04, it says so in its text and only
> touches files it made itself.

Renaming is moving within the same folder. The batch renamer in E03
builds a new name for every file, checks that the name is free, and then
calls `FILE.MOVE`:

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
DIM scan$ = PATH.JOIN$(folder$, "scan0001.pdf")
TXTWRITER scan$, "pdf"
FILE.MOVE(scan$, PATH.JOIN$(folder$, "2026-10-01 Invoice.pdf"))
PRINT DIR$(PATH.JOIN$(folder$, "*"))
KILL PATH.JOIN$(folder$, "2026-10-01 Invoice.pdf")
RMDIR folder$
```

```text
[2026-10-01 Invoice.pdf]
```

### Names with umlauts and other characters

File names may contain any character Windows allows: umlauts, accents,
spaces, the euro sign. jdBasic keeps text as UTF-8 and passes names to
Windows in that form, so a file called `Rechnung Müller.pdf` in your
Downloads folder is found and moved like any other.

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
DIM file$ = PATH.JOIN$(folder$, "Rechnung Müller €.txt")
TXTWRITER file$, "Grüße"
PRINT DIR$(PATH.JOIN$(folder$, "*"))
PRINT TXTREADER$(file$)
KILL file$
RMDIR folder$
```

```text
[Rechnung Müller €.txt]
Grüße
```

### Folders inside folders

`DIR$` looks into one folder. To find every file below a folder,
including the files in its subfolders and in their subfolders, a
function looks into the folder, and for every subfolder it finds, calls
itself on that subfolder. A function that calls itself is called
*recursive*; it stops because a folder has only so many levels. The
duplicate finder in E10 and the disk space report in E11 walk your
folders this way.

```basic
FUNC AllFiles(folder$)
    DIM out = []
    FOR EACH entry$ IN SORT(DIR$(PATH.JOIN$(folder$, "*")))
        DIM full$ = PATH.JOIN$(folder$, entry$)
        IF FILE.ISDIR(full$) THEN
            out = APPEND(out, AllFiles(full$))
        ELSE
            PUSH(out, full$)
        ENDIF
    NEXT
    RETURN out
ENDFUNC

DIM root$ = MKTEMP$("book")
MKDIR PATH.JOIN$(root$, "2026", "October")
TXTWRITER PATH.JOIN$(root$, "readme.txt"), "x"
TXTWRITER PATH.JOIN$(root$, "2026", "plan.xlsx"), "x"
TXTWRITER PATH.JOIN$(root$, "2026", "October", "offer.pdf"), "x"
DIM found = AllFiles(root$)
PRINT LEN(found); " files"
FOR EACH f$ IN found
    PRINT PATH.BASENAME$(f$)
NEXT
FOR EACH f$ IN found
    KILL f$
NEXT
RMDIR PATH.JOIN$(root$, "2026", "October")
RMDIR PATH.JOIN$(root$, "2026")
RMDIR root$
```

```text
3 files
offer.pdf
plan.xlsx
readme.txt
```

A walk like this can take a while in a large folder: a whole drive has
hundreds of thousands of files. The recipes that walk folders start in
the folder you name in `work.conf`, not at the top of the drive, and
they print how far they got, so you can see that they are working.

### Plan first, then act

A program that moves, renames or deletes files can do a lot of harm in
a second. Every recipe in this book that changes files works in two
steps, and you will see the pattern in all of them. The first step
makes a *plan*: a list of what it would do, without touching anything.
The second step carries the plan out. With `--dry-run`, the program
stops after the first step and prints the plan.

```basic
FUNC Plan(names)
    DIM moves = []
    FOR EACH n$ IN names
        IF ENDSWITH(n$, ".pdf") THEN
            PUSH(moves, {"from": n$, "to": "Documents/" + n$})
        ENDIF
    NEXT
    RETURN moves
ENDFUNC

SUB Show(moves)
    FOR EACH m IN moves
        PRINT "would move "; m{"from"}; " -> "; m{"to"}
    NEXT
ENDSUB

DIM moves = Plan(["offer.pdf", "photo.jpg", "invoice.pdf"])
Show(moves)
PRINT LEN(moves); " files would move"
```

```text
would move offer.pdf -> Documents/offer.pdf
would move invoice.pdf -> Documents/invoice.pdf
2 files would move
```

The split has three advantages. You can look at the plan before you
trust the program with your files. The tests of a recipe can check the
plan against what they expect, without moving a single real file. And
when something does go wrong while the plan is carried out, the plan
tells you exactly which files were meant to go where.

### Putting it together

The last example of this chapter combines most of what you have seen: a
folder with a few files, a map from extensions to kinds, a loop, a
decision and a move. It is the Downloads Butler of E01 in twenty lines,
without its safety checks. Read it line by line; if every line makes
sense, you are ready for Chapter 3.

```basic
DIM folder$ = MKTEMP$("book")
MKDIR folder$
FOR EACH f$ IN ["offer.pdf", "holiday.jpg", "notes.txt"]
    TXTWRITER PATH.JOIN$(folder$, f$), "x"
NEXT
DIM kinds = {"pdf": "Documents", "txt": "Documents", "jpg": "Images"}
FOR EACH f$ IN SORT(DIR$(PATH.JOIN$(folder$, "*")))
    DIM ext$ = LCASE$(MID$(PATH.EXT$(f$), 1, 10))
    DIM kind$ = kinds{ext$} ?? "Other"
    DIM target$ = PATH.JOIN$(folder$, "Sorted", kind$)
    MKDIR target$
    FILE.MOVE(PATH.JOIN$(folder$, f$), target$)
    PRINT f$; " -> "; kind$
NEXT
FOR EACH k$, kind$ IN kinds
    DIM moved = DIR$(PATH.JOIN$(folder$, "Sorted", kind$, "*"))
    FOR EACH f$ IN moved
        KILL PATH.JOIN$(folder$, "Sorted", kind$, f$)
    NEXT
NEXT
RMDIR PATH.JOIN$(folder$, "Sorted", "Documents")
RMDIR PATH.JOIN$(folder$, "Sorted", "Images")
RMDIR PATH.JOIN$(folder$, "Sorted")
RMDIR folder$
PRINT "sorted and cleaned up"
```

```text
holiday.jpg -> Images
notes.txt -> Documents
offer.pdf -> Documents
sorted and cleaned up
```

> **Balance dividend**
> This chapter gives back no minutes by itself. It is what lets you
> change a recipe in five minutes instead of living with a version that
> almost fits.






