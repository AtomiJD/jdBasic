<!-- pagebreak -->

# Appendix C: Troubleshooting

These are the 25 messages readers of this book are most likely to
meet, grouped by what you were doing when they appeared. Each one is
shown as jdBasic or the recipe prints it, followed by what it means
and what to do. A message longer than the page is broken over two
lines here; on the screen it is one line. jdBasic prints the word
*Error* and its number in color, and the rest as shown.

When a message is not in this list, read it from the end: the part
after `at line` names the line where it happened. When `of` and a file
name such as `CONF.jdb` follow, that line is in a shared library, and
the words before it tell you more than the number. Those words usually
name the file, the setting or the value that was wrong.

## Installing and starting

### 1. Windows does not know jdbasic

```text
'jdbasic' is not recognized as an internal or external command,
operable program or batch file.
```

The command prompt cannot find `jdbasic.exe`. Either the folder of
jdBasic is not on the PATH, or the prompt was opened before you added
it. Close every command prompt and open a new one. If the message
stays, repeat the four steps of *Installing jdBasic* in Chapter 2 and
check that the folder you added is the one that holds `jdbasic.exe`.
On a German Windows the same message starts with *Der Befehl
"jdbasic" ist entweder falsch geschrieben*.

### 2. A library cannot be found

```text
Error #99: Parse error at line 8: cannot load module 'WORKCONF'
```

The program imports a module that is in none of the places jdBasic
looks: the program's own folder, a `modules` folder next to it, the
folders in `JDBASIC_PATH`, `.jdbasic\lib` in your home folder, and
the `lib` folder next to `jdbasic.exe`. The shared modules of the
recipes, such as WORKCONF and OUTBOX, are copied into `.jdbasic\lib`
by the setup wizard. Run the wizard once, or start the recipe from
the copy in your work folder, not from the book's download. A module
of the jdBasic libraries, such as CONF or XLSX, belongs in the `lib`
folder of your jdBasic installation; unpack the release again if it
is missing.

### 3. The forms release cannot compile

```text
Native compilation not available (build with NATIVEC flag).
```

`jdbasic -c` needs the release of jdBasic with the native compiler.
The forms release this book starts with runs every recipe but does
not contain the compiler. `jdbasic --version` lists *NativeC* among
the features when it is there. Use the professional release for X07
and for handing programs to colleagues, as Chapter 2 explains under
*From Script to Program*.

## The settings file

### 4. No settings file

```text
Error #99: No settings found at C:/Users/mia/Documents/AutomateWork/
config/work.conf. Run the setup wizard first, or name the file with
--config. at line 58 of WORKCONF.jdb
```

A recipe looks for `work.conf` after `--config` on the command line,
then in the environment variable `AUTOMATEWORK_CONF`, then in the
`config` folder of the work folder. Run the setup wizard, or give the
file with `--config`. The `line 58 of WORKCONF.jdb` belongs to the
shared module that reads the settings and says nothing about your
program.

### 5. A folder with backslashes

```text
Error #99: CONF.TOML C:/Users/mia/Documents/AutomateWork/config/
work.conf line 5: "C:\Users\mia\Reports" is a Windows path in double
quotes, where a backslash starts an escape; write the path with
forward slashes, "C:/Users/mia", or in single quotes, 'C:\Users\mia'
at line 48 of CONF.jdb
```

Inside double quotes a TOML file reads a backslash as the start of a
special character, so `"C:\Users\mia\Reports"` cannot stay as it is
written. The message names the line in `work.conf`, here line 5.
Write the folder with forward slashes, `"C:/Users/mia/Reports"`, which
Windows understands as well, or in single quotes,
`'C:\Users\mia\Reports'`, which keep every backslash.

### 6. A quote that is not closed

```text
Error #99: CONF.TOML C:/Users/mia/Documents/AutomateWork/config/
work.conf line 2: the text has no closing quote at line 48 of CONF.jdb
```

A value in `work.conf` starts with a double quote and has no second
one on the same line. Open `work.conf` at the line the message names,
here line 2, and add the quote at the end of the value; a folder that
ends without its quote is the usual case.

### 7. A heading without its bracket

```text
Error #99: CONF.TOML C:/Users/mia/Documents/AutomateWork/config/
work.conf line 3: a section heading needs its closing bracket:
[downloads_butler at line 48 of CONF.jdb
```

A heading in `work.conf` is missing its `]`: `[downloads_butler`
instead of `[downloads_butler]`. Text without quotes gives a message
of the same kind, *text needs quotes*: write `name = "Mia Example"`,
not `name = Mia Example`. One mistake gives no message at all. A key
with a different spelling, `min_age_hour` instead of `min_age_hours`,
is read by nobody, and the recipe runs with its default instead of
your value.
Compare the part with Appendix D, which lists every key a recipe
reads, and run the recipe with `--dry-run` to see the values it uses.

## The wizard and the Task Scheduler

### 8. A schedule the wizard cannot read

```text
WIZARD: cannot schedule "daily 8:30" for E01
```

The wizard understands `daily HH:MM`, `weekdays HH:MM`,
`weekly DAY HH:MM`, `every N minutes`, `every N hours`, `hourly`,
`at logon` and `manual`. The time needs two digits for the hour, so
write `daily 08:30`. The day of `weekly` is a weekday name such as
`fri` or `friday`.

### 9. The Task Scheduler refuses a task

```text
could not schedule Downloads Butler: FEHLER: Ungültiger
Startzeitwert.
```

The wizard handed the schedule to the Task Scheduler, and Windows
refused it. The text after the colon is Windows' own and comes in the
language of your Windows; this one is from a German system and says
that the start time is not valid. The usual cause is a time that does not exist, such
as `25:00`. Correct the schedule on the third page of the wizard and
set up again. If the message speaks of missing rights, your company
does not let you add tasks; ask IT, or start the recipes under the
job server of X01, which needs only one task at logon.

### 10. The doctor finds a problem

```text
  no settings file at C:/Users/mia\Documents\AutomateWork\config\
work.conf
  jdbasic not found; install it or name it in [paths] jdbasic
  the shared module is missing: C:/Users/mia\.jdbasic\lib\workconf.jdb
  E01: no scheduled task AutomateWork\E01 Downloads Butler
```

`setup_console.jdb --doctor` lists one line for each part of the
setup that is not where the wizard put it. A missing settings file or
shared module means the wizard has not run on this account, or the
work folder moved. *jdbasic not found* means the path in `[paths]`
no longer points to `jdbasic.exe`, usually after an update into a new
folder. A missing task was removed in the Task Scheduler. Run the
wizard again in each case; it keeps your answers.

## Mail and the outbox

### 11. No mail server

```text
Set [mail] server in work.conf first.
```

You ran a recipe with `--send`, and `work.conf` has no mail server.
Add the `[mail]` part as Chapter 4 shows, or run the wizard again and
fill in *Mail server* on its second page.

### 12. A message stays in the outbox

```text
0 sent
  not sent: status.eml: curl: (7) Failed to connect to
mail.example.com:465 after 2022 ms: Could not connect to server
```

The recipe could not reach the mail server, so the message stays in
the outbox for the next `--send`. The number after `curl:` tells the
cause. `(7)` means no connection: a wrong server name or port, or a
network that blocks it. `(67)` means the server refused your user
name or password. `(60)` means the server's certificate could not be
checked, which a company proxy often causes. Check the server and
port with your IT department; many offices use port 465 with
`smtps://` or 587 with `smtp://` and `starttls = true`.

### 13. No signing key for the audit trail

```text
No signing key yet. Run with --init-audit first.
```

X14 checks the audit trail with a key kept in the Windows Credential
Manager, and there is none for your account yet. Run
`secrets_audit.jdb --init-audit` once. The key belongs to your
Windows account; on another account or another computer the trail
cannot be checked with it.

## Pages in the browser

### 14. A port that is taken

```text
Error #99: Port 8766 is taken. Set port in [find_anything]. at line 56
```

A recipe that shows a page on `localhost` needs a port of its own,
and another program already listens on it. Often that program is the
same recipe, started twice, for example once by its task at logon
and once by hand. Close the other one, or set another number above
1024 for `port` in the recipe's part of `work.conf`.

## Writing and changing code

### 15. A name jdBasic keeps for itself

```text
Error #99: Parse error at line 1: expected variable name, got 'STEP'
```

`STEP` belongs to the FOR loop, and jdBasic does not accept it as the
name of a variable. The same happens with other words of the
language, such as `STOP`, `LINE`, `ON` or `CLS`, and with the names of
builtin functions. Choose another name: `step_size`, `stop_time`.

### 16. A misspelt function

```text
Error #22: Undefined function: PRNT at line 1
```

jdBasic knows no statement or function of that name. Check the
spelling; `jdbasic --lint program.jdb` finds these before you run the
program. A module function needs its module name in front, such as
`WORKCONF.VALUE`, and its IMPORT line at the top.

### 17. A variable that was never declared

```text
Error #20: Undeclared variable 'TOTAL' (OPTION "EXPLICIT") at line 2
```

With `OPTION "EXPLICIT"` at the top, every variable needs a `DIM`
before its first use. Add `DIM total = 0` before the line, or check
the name for a typing mistake: `totl` is a new variable, not a
misspelt `total`.

### 18. An index past the end

```text
Error #13: Array index out of bounds: 3 at line 2
```

The positions of a list start at 0, so a list of three items has the
positions 0, 1 and 2. A loop over a list runs
`FOR k = 0 TO LEN(items) - 1`. An empty list has no position at all;
check `LEN(items) > 0` before reading `items[0]`.

### 19. A block without its end

```text
Error #99: Parse error at line 4: expected 'END', got ''
```

An `IF` has no `ENDIF`, a `FUNC` no `ENDFUNC`, or a loop no `NEXT` or
`LOOP`; for a FOR loop the message says `expected 'NEXT'`. The line
number is the end of the file, not the start of the block, which
makes this the least helpful message of the list. Indent each block
as the recipes do, and the block whose end is missing shows itself.

### 20. A program named like a library

```text
Error #22: Undefined function: MAIL.MESSAGE at line 7
```

The function exists, but jdBasic loaded another file as the module.
A program saved as `mail.jdb` next to your script is found before the
library MAIL, because the program's own folder comes first. Give your
own files names that no library uses, such as `my_mail.jdb`.

### 21. A wrong answer without a message

```text
epo
0
```

These lines came from `PRINT MID$("Report", 1, 3)` and
`PRINT INSTR("Report", "R")`. Positions in text start at 0 in
jdBasic, as they do in lists, so `MID$(t$, 1, 3)` starts at the second
letter, and INSTR answers 0 for a match at the very start. It answers
-1 when there is no match, so test `INSTR(t$, x$) >= 0`, never
`> 0`. Programs from other BASICs need this change more often than
any other.

### 22. A setting that prints as NONE

```text
value: NONE
```

A map was asked for a key it does not have. The answer is no value,
and text made from it reads `NONE`. Check with
`MAP.EXISTS(m, "key")` first, or read settings through
`WORKCONF.VALUE` with a default, which is what the recipes do.

### 23. Arguments in the wrong order

```text
Error #24: DATEADD: unknown unit ""; use Y, M, W, D, H, N or S at
line 1
```

`DATEADD` takes the unit first, then the number, then the date:
`DATEADD("D", 1, day)`. With the date first, jdBasic reads the date as
the unit. When a builtin answers with a message about a strange
value, compare the order of its arguments with the reference.

### 24. Dates and numbers in other forms

```text
Error #99: CDATE: "10/05/2026" is not a date; write YYYY-MM-DD or
DD.MM.YYYY, with HH:MM:SS after a space if needed at line 1
```

`CDATE` reads `2026-10-05` and the German form `05.10.2026`, each with
a time if needed. A date with slashes can mean the 10th of May or the
5th of October, so `CDATE` stops with this message instead of
guessing. Bring such a date into one of the two forms first. Numbers
need a decimal point: `VAL("12,5")` answers 12 without a message, so
convert the form first with `REPLACE$(t$, ",", ".")`.

## Compiling

### 25. Rules of the compiler

```text
error at 2: STRICT: cannot assign DOUBLE to INTEGER 'MOOD'; wrap with
CINT() to assign explicitly
error at 1: undeclared variable 'TOTAL'
```

`jdbasic -c` is stricter than the interpreter. Every variable needs a
`DIM`, and a variable keeps the kind of its first value: one that
starts as `0` holds whole numbers, so a value with decimals needs
`CINT(...)` or a start of `0.0`. The number after `error at` is the
line. A program that runs in the interpreter and fails here usually
needs one of these two changes; the recipes of this book all pass
both.
