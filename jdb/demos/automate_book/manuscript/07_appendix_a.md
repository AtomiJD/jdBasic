<!-- pagebreak -->

# Appendix A: jdBasic Quick Reference

This appendix lists the part of jdBasic that the programs in this book
use, and nothing else. Each entry gives the call with its arguments in
the order jdBasic expects them, what it answers, and the trap that
catches people most often. The full reference with every builtin is
`doc/languages.md` in the jdBasic folder; `jdbasic --help` and the
`HELP` command of the interactive mode show it too.

Three rules hold everywhere:

- **Positions count from 0.** The first character of a text, the first
  element of an array and the result of `INSTR` all start at 0. `IOTA`
  is the one exception: it counts from 1 unless you ask otherwise.
- **A name that ends in `$` holds text.** `name$` is always a string;
  functions whose name ends in `$` answer one.
- **Names are not case sensitive.** `Total`, `TOTAL` and `total` are
  the same variable.

## Programs and modules

| Statement | What it does |
|---|---|
| `IMPORT CONF, DT` | Loads modules; their functions are called as `CONF.GET(...)` |
| `EXPORT MODULE NAME` | First line of a module file such as `name.jdb` |
| `EXPORT FUNC NAME(a, b)` | A function other programs may call |
| `FUNC Name(a, b = 0)` ... `ENDFUNC` | A function; `b` has a default value |
| `SUB Name(a)` ... `ENDSUB` | A procedure that answers nothing |
| `RETURN value` | Leaves a function with its answer |
| `END` | Ends the program at once |
| `CONST LIMIT = 3` | A name whose value cannot change |
| `Name@` | The function itself as a value, to hand to `SELECT` or `RETRY.RUN` |
| `LAMBDA x -> x + 1` | A small function written in place |
| `ASYNC FUNC Worker(...)` | A function a worker pool runs in parallel (X08) |
| `ON handler$ CALL Name` | Connects a window event to a function (X06) |
| `DECLARE FUNC ... LIB "x.dll"` | Calls a function of a Windows DLL (VAULT in X14) |

A program finds its modules in its own folder, then in the folders of
the environment variable `JDBASIC_PATH`. The wizard copies the shared
modules of the book into such a folder for you.

## Variables and values

| Form | Meaning |
|---|---|
| `DIM n = 0` | A variable with its first value |
| `DIM name$ = ""` | A text variable |
| `DIM items = []` | An empty array |
| `DIM m AS MAP` | An empty map |
| `DIM m = {"a": 1, "b": "two"}` | A map with keys and values |
| `TRUE`, `FALSE` | Truth values |
| `NONE` | No value; what a missing map key answers |
| `TYPEOF(x)` | `"INT64"`, `"FLOAT64"`, `"STRING"`, `"ARRAY"`, `"OBJECT"`, `"DATE"`, `"BOOLEAN"` or `"NONE"` |

To find out whether a map has a key, ask `MAP.EXISTS(m, "key")` or
`TYPEOF(m{"key"}) = "NONE"`. A test such as `m{"key"} = NONE` does not
work, and `"" + m{"missing"}` gives the text `NONE`.

## Operators

| Operator | Meaning |
|---|---|
| `+ - * /` | Arithmetic; `+` also joins texts |
| `\` | Whole-number division: `7 \ 2` is 3 |
| `MOD` | Remainder: `7 MOD 3` is 1 |
| `^` | Power: `2 ^ 3` is 8 |
| `= <> < > <= >=` | Comparisons; `=` compares texts too |
| `AND OR NOT` | Logic; both sides are always worked out |
| `ANDALSO ORELSE` | Logic that stops early: the right side only runs when it matters |
| `x IN items` | Whether an array holds a value |

Use `ANDALSO` when the right side would fail without the left one, as in
`IF LEN(a) > 0 ANDALSO a[0] = "x" THEN`.

## Decisions and loops

```
IF n > 10 THEN
    PRINT "many"
ELSEIF n > 0 THEN
    PRINT "some"
ELSE
    PRINT "none"
ENDIF
IF done THEN PRINT "ok" ELSE PRINT "not yet"

FOR k = 0 TO LEN(items) - 1
    PRINT items[k]
NEXT k
FOR k = 10 TO 0 STEP -2
NEXT k
FOR EACH name$ IN names
NEXT
FOR EACH key$, value IN settings
NEXT

DO WHILE NOT finished
    IF tries > 3 THEN EXITDO
LOOP
DO
    n = n + 1
LOOP WHILE FILE.EXISTS(candidate$)
```

`EXITDO` leaves a `DO` loop, `EXITFOR` a `FOR` loop. A `DIM` inside a
loop gives the variable a new value on every pass.

## Text

| Call | Answers |
|---|---|
| `LEN(s$)` | The number of characters |
| `MID$(s$, start, n)` | `n` characters from position `start`, counted from 0; without `n` to the end |
| `LEFT$(s$, n)`, `RIGHT$(s$, n)` | The first or last `n` characters |
| `INSTR(s$, find$)` | The position of `find$`, from 0, or -1 when it is not there |
| `INSTR(start, s$, find$)` | The same, searching from `start` |
| `REPLACE$(s$, old$, new$)` | Every `old$` replaced |
| `TRIM$(s$)` | Without spaces at both ends |
| `LCASE$(s$)`, `UCASE$(s$)` | In small or capital letters |
| `SPLIT(s$, sep$)` | An array of the parts |
| `JOIN(items, sep$)` | The parts joined into one text |
| `STARTSWITH(s$, p$)`, `ENDSWITH(s$, p$)` | Whether the text begins or ends so |
| `STR$(n)`, `VAL(s$)` | A number as text, a text as number |
| `CINT(x)` | A whole number; text that is not a number is an error |
| `CHR$(n)`, `ASC(c$)` | A character from its code, a code from its character |
| `REPEAT$(s$, n)` | The text `n` times |
| `FORMAT$(spec$, v, ...)` | Values put into `{}` holes: `{:.2f}`, `{:>8}` |
| `REGEX.MATCH(pattern$, s$)` | Whether the whole text matches |
| `REGEX.FINDALL(pattern$, s$)` | Every match |
| `REGEX.REPLACE(pattern$, s$, new$)` | Every match replaced; `$1` in `new$` is the first group |
| `-s$` | The characters as an array |

Two traps: `INSTR` answers -1, not 0, when it finds nothing, so test
`INSTR(...) >= 0`; and the start position of `INSTR` comes first. In
`FORMAT$` a width pads with spaces only, so `{:05d}` does not give
leading zeros.

```basic
DIM s$ = "Invoice 0413.pdf"
PRINT MID$(s$, 0, 7)
PRINT INSTR(s$, "0413")
PRINT INSTR(s$, "x")
PRINT FORMAT$("{:.2f} EUR", 209.8)
PRINT JOIN(SPLIT("a;b;c", ";"), " + ")
```

```text
Invoice
8
-1
209.80 EUR
a + b + c
```

## Numbers

| Call | Answers |
|---|---|
| `INT(x)` | The whole part: `INT(3.7)` is 3 |
| `ROUND(x, digits)` | Rounded: `ROUND(2.345, 2)` is 2.35 |
| `FLOOR(x)`, `ABS(x)` | Rounded down, without sign |
| `SUM(items)` | The sum of an array |
| `MIN(items)`, `MAX(items)` | The smallest and largest element of an array |

`MIN` and `MAX` take one array: write `MIN([a, b])` for the smaller of
two numbers.

## Arrays

| Call | Answers |
|---|---|
| `items[0]` | The first element |
| `LEN(items)` | The number of elements |
| `PUSH(items, v)` | Adds `v` at the end of the array itself |
| `APPEND(items, v)` | A new array with `v` added; the old one stays |
| `SORT(items)` | A sorted copy |
| `GRADE(items)` | The positions that would sort the array |
| `TAKE(n, items)`, `DROP(n, items)` | The first `n`, or all but the first `n`; negative `n` counts from the end |
| `UNIQUE(items)`, `REVERSE(items)` | Without repeats, back to front |
| `SELECT(Fn@, items)` | `Fn` applied to every element |
| `FILTER(Fn@, items)` | The elements for which `Fn` is true |
| `ZEROS(n)` | `n` zeros |
| `IOTA(n)` | 1 to `n`; `IOTA(n, 0)` gives 0 to `n - 1` |

`APPEND` is a function: write `items = APPEND(items, v)`. `PUSH`
changes the array in place and needs no assignment.

```basic
DIM a = [30, 10, 20]
PUSH(a, 5)
PRINT SORT(a)
PRINT TAKE(2, a)
PRINT SELECT(LAMBDA x -> x * 2, a)
PRINT 10 IN a
```

```text
[5, 10, 20, 30]
[30, 10]
[60, 20, 40, 10]
TRUE
```

## Maps

| Call | Answers |
|---|---|
| `m{"key"}` | The value of a key, `NONE` when it is missing |
| `m{"key"} = v` | Sets a value |
| `MAP.EXISTS(m, "key")` | Whether the key is there |
| `MAP.KEYS(m)` | The keys, in the order they were added |
| `MAP.DELETE(m, "key")` | Removes a key |

## Dates and times

| Call | Answers |
|---|---|
| `NOW()` | The date and time of this moment |
| `CDATE(text$)` | A date from `"2026-10-05"` or `"2026-10-05 08:30:00"` |
| `FORMAT_DATE(d, fmt$)` | Text with `%Y %m %d %H %M %S %A %B` filled in |
| `DATEADD(unit$, n, d)` | `d` moved by `n` units: `"Y" "M" "W" "D" "H" "N" "S"` |
| `DATEDIFF(unit$, a, b)` | From `a` to `b` in `"D" "H" "N"` or `"S"` |
| `YEAR(d)`, `MONTH(d)`, `DAY(d)` | Parts of a date |
| `HOUR(d)`, `MINUTE(d)` | Parts of the time |
| `WEEKDAY(d)` | 0 for Sunday to 6 for Saturday |
| `TICK()` | Milliseconds, for measuring how long something takes |

In `DATEADD` the number comes before the date. `"N"` is minutes,
because `"M"` is months. `DATEDIFF` with `"D"` answers fractions of a
day; wrap it in `INT` for whole days. Days and weeks follow the
calendar, so adding a day across a change of the clocks keeps the time
of day. The DT library of Appendix B does the same with seconds you
can count with, and knows time zones.

```basic
DIM d = CDATE("2026-10-05 08:30:00")
PRINT FORMAT_DATE(d, "%A, %d %B %Y")
PRINT FORMAT_DATE(DATEADD("D", 3, d), "%Y-%m-%d")
PRINT DATEDIFF("H", CDATE("2026-10-05"), d)
```

```text
Monday, 05 October 2026
2026-10-08
8.5
```

## Files and folders

| Call | Answers |
|---|---|
| `PATH.JOIN$(a$, b$, ...)` | A path from its parts, with the right separator |
| `PATH.DIRNAME$(p$)`, `PATH.BASENAME$(p$)` | The folder, the file name |
| `PATH.EXT$(p$)` | The extension with its dot: `".pdf"` |
| `PATH.NORMALIZE$(p$)` | The path tidied up |
| `FILE.EXISTS(p$)`, `FILE.ISDIR(p$)` | Whether there is a file, or a folder |
| `FILE.STAT(p$)` | A map with `size`, `mtime`, `is_dir`, `hidden`, `readonly`, `exists` |
| `FILE.SIZE(p$)` | The size in bytes |
| `FILE.MOVE(from$, to$, overwrite)` | Moves or renames; into a folder when `to$` is one |
| `FILE.COPY(from$, to$, overwrite)` | Copies; `overwrite` is `FALSE` when left out |
| `DIR$(pattern$)` | The names in a folder that match, such as `"C:/Scans/*.pdf"` |
| `MKDIR path$` | Creates a folder and every folder above it that is missing |
| `RMDIR path$` | Removes an empty folder |
| `KILL path$` | Deletes a file |
| `TXTREADER$(path$)` | A text file as one string |
| `TXTWRITER path$, text$` | Writes a text file, replacing what was there |
| `BINREADER$(path$)`, `BINWRITER path$, data$` | The same for bytes |
| `CSVREADER(path$, sep$, header, types)` | A CSV file as rows of cells |
| `MKTEMP$(prefix$)` | A fresh path in the temporary folder, not yet created |
| `CD()` | The current folder |

`DIR$` answers names only; join them with the folder before you open
them. `FILE.MOVE` and `FILE.COPY` refuse to overwrite unless you pass
`TRUE`. `MKTEMP$` gives a name: create the folder with `MKDIR`.

## Other programs and the system

| Call | Answers |
|---|---|
| `OS.EXEC(program$, args)` | Runs a program; a map with `"EXIT_CODE"` and `"OUTPUT"` |
| `OS.ARGS()` | The words on the command line |
| `GETENV$(name$)` | An environment variable, `""` when it is not set |
| `SETENV name$, value$` | Sets one for this program and the programs it starts |
| `SLEEP ms` | Waits so many milliseconds |
| `OS.GETOS()` | `"WINDOWS"` on Windows |
| `OS.SCREENSHOT(path$, mode$, title$)` | A picture of a window (X06) |

`OS.EXEC` starts the program directly, without a command prompt in
between, and hands each element of `args` over as one argument. For a
command of `cmd.exe` such as `rmdir`, call
`OS.EXEC("cmd", ["/c", "rmdir", folder$])`.

## The console

| Statement | What it does |
|---|---|
| `PRINT a; b` | Prints values next to each other |
| `PRINT a, b` | Prints them with a space between |
| `PRINT a;` | No new line at the end |
| `INPUT "Name: "; name$` | Reads a line; the typed text shows on screen |
| `WAITKEY$()` | Waits for one key and answers it |
| `OUTPUT.CAPTURE_BEGIN` | Collects what `PRINT` writes from here on |
| `OUTPUT.CAPTURE_END$()` | Stops collecting and answers the text |

Never ask for a password with `INPUT`: it shows on screen. The VAULT
module of Appendix B has a prompt that prints stars.

## Data, hashes and the web

| Call | Answers |
|---|---|
| `JSON.STRINGIFY$(v)` | A map or array as JSON text |
| `JSON.PARSE$(text$)` | JSON text read back as a map or array |
| `CODEC.SHA256$(data$)` | The SHA-256 hash as 64 hex digits |
| `HTTP.REQUEST(method$, url$, body$, type$)` | A map with `status`, `body` and `headers` |
| `HTTP.SETTIMEOUT(seconds)` | How long a request may take |
| `HTTP.STATUSCODE()` | The status of the last request |

Despite its `$`, `JSON.PARSE$` answers a map or an array. The REQ and
JDWEB libraries of Appendix B are the comfortable way to the web.

## Errors

```
TRY
    DIM data = XLSX.READ(path$)
CATCH
    PRINT "Could not read "; path$; ": "; ERRMSG$
ENDTRY

IF days < 0 THEN THROW "days must not be negative"
```

`ERRMSG$` holds the message of the error inside `CATCH`. `THROW` raises
an error with your own text; a `TRY` further up catches it, and without
one the program stops and prints it.

## Names you cannot use

Some words are part of the language and cannot name a variable. These
fifteen came up while the book was written; `DIM stop = 1` stops with
"expected variable":

`CLS`, `END`, `FOR`, `INPUT`, `LINE`, `LOOP`, `NEXT`, `ON`, `PRINT`,
`STEP`, `STOP`, `TEXT`, `THEN`, `TYPE`, `USE`

The names of builtins are allowed for variables, but not for your own
functions: `FUNC Count(x)` stops with "FUNC COUNT collides with the
builtin function COUNT". Names that caught the recipes are `COUNT`,
`DAY`, `JOIN`, `LEFT`, `LEN`, `MAX`, `MIN`, `MONTH`, `NOW`, `PLACE`,
`SORT`, `SPLIT`, `SUM`, `TICK`, `TRIM` and `VAL`. A word in front, as
in `CountFiles` or `RunStep`, avoids all of them.
