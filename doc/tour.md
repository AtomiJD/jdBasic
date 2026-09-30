# A tour of jdBasic

[Docs home](README.md) · **Tour** · [Language reference](languages.md) · [Module library](../lib/README.md)

Fifteen minutes from the first `PRINT` to a compiled `.exe`. Every example
below is run by CI (`tools/check_doc_examples.jdb`), and the output shown
under it is what it actually prints.

**Run the examples** in the REPL (`jdbasic`, then type or paste), from a
file (`jdbasic hello.jdb`), or without installing anything in the
[browser playground](https://jdbasic.org/live).

## 1. Values

```basic
PRINT "Hello, jdBasic"
DIM greeting$ = "Hello"
DIM n = 42
DIM price = 9.95
DIM ok = TRUE
PRINT TYPEOF(greeting$), TYPEOF(n), TYPEOF(price), TYPEOF(ok)
```

```text
Hello, jdBasic
STRING INT64 FLOAT64 BOOLEAN
```

`DIM` declares a variable and takes its type from the value. A `$` at the
end of a name is the classic BASIC mark for a string; it is a convention,
not a requirement. Names are case-insensitive: `n` and `N` are the same
variable.

## 2. Strings

```basic
DIM who$ = "Ada"
DIM count_new = 3
PRINT $"Hello, {{who$}}! You have {{count_new}} new messages."
PRINT "Hello, " + who$

DIM word$ = "jdBasic"
PRINT LEFT$(word$, 2), MID$(word$, 2, 5), UCASE$(word$), LEN(word$)
PRINT INSTR(word$, "B"), INSTR(word$, "z")
PRINT SPLIT("a,b,c", ",")
PRINT JOIN(["x", "y", "z"], "-")
PRINT -"abc"
PRINT "one" + CHR$(10) + "two"
```

```text
Hello, Ada! You have 3 new messages.
Hello, Ada
jd Basic JDBASIC 7
2 -1
[a, b, c]
x-y-z
[a, b, c]
one
two
```

- `$"..."` puts expressions between double braces into a string.
- **Positions count from 0**: `MID$(word$, 2, 5)` starts at the third
  character, and `INSTR` answers `-1` when there is no match.
- A string has no `\n` escape; use `CHR$(10)`, or `VBNEWLINE` for `CR LF`.
- Unary minus turns a string into an array of its characters.

## 3. Arrays

Arrays are where jdBasic is at home: most functions and operators work on a
whole array at once, the way APL does.

```basic
DIM nums = [10, 20, 30]
PRINT nums[0], nums[2], LEN(nums)
nums = APPEND(nums, 40)
PRINT nums

PRINT IOTA(5)
PRINT IOTA(5) * 2
PRINT [1, 2, 3] + [10, 20, 30]
PRINT SUM(IOTA(100))
PRINT MAX([3, 9, 4]), MEAN([2, 4, 6])
PRINT REVERSE(SORT([3, 1, 2]))
PRINT RESHAPE(IOTA(6), [2, 3])
```

```text
10 30 3
[10, 20, 30, 40]
[1, 2, 3, 4, 5]
[2, 4, 6, 8, 10]
[11, 22, 33]
5050
9 4
[3, 2, 1]
[[1, 2, 3], [4, 5, 6]]
```

- **Indexes count from 0**, like string positions.
- **`IOTA(n)` counts from 1**: it is APL's index generator. `IOTA(n, 0)`
  starts at 0.
- `APPEND` returns a new array; assign the result.

Functions are values too. `LAMBDA` writes one inline, and `|>` pipes a value
into the `?` of the next call:

```basic
PRINT FILTER(LAMBDA x -> x MOD 2 = 0, IOTA(10))
PRINT SELECT(LAMBDA x -> x * x, [1, 2, 3])
PRINT SELECT(LAMBDA w -> LEN(w), SPLIT("the quick brown fox", " "))
PRINT IOTA(5) |> SUM(?)
```

```text
[2, 4, 6, 8, 10]
[1, 4, 9]
[3, 5, 5, 3]
15
```

For the whole toolbox - `SCAN`, `OUTER`, `GRADE`, `AGG`, matrices, FFT - see
[From loops to array pipelines](APL_pipeline.md) and the
[vector and matrix cookbook](howto-vector-matrix-data.md).

## 4. Maps, and values that are not there

```basic
DIM user = {"name": "Ada", "age": 36}
PRINT user{"name"}, user{"age"}
user{"city"} = "London"
PRINT MAP.KEYS(user)

PRINT TYPEOF(user{"email"})
PRINT user{"email"} ?? "no email"
```

```text
Ada 36
[name, age, city]
NONE
no email
```

A missing key reads as `NONE`. `??` gives the left side unless it is absent.

**The one trap to know:** do not test for absence with `= NONE`. The
comparison is also true for `0` and for an empty string. Ask the type
instead:

```basic
PRINT 0 = NONE, "" = NONE
PRINT TYPEOF(0) = "NONE"
```

```text
TRUE TRUE
FALSE
```

## 5. Control flow

```basic
DIM score = 73
IF score >= 90 THEN
    PRINT "A"
ELSEIF score >= 70 THEN
    PRINT "B"
ELSE
    PRINT "C"
ENDIF

FOR i = 1 TO 3
    PRINT i;
NEXT
PRINT

FOR EACH fruit$ IN ["apple", "pear"]
    PRINT fruit$
NEXT

DIM k = 1
DO WHILE k < 100
    k = k * 3
LOOP
PRINT k

SWITCH score
    CASE 90 TO 100
        PRINT "top"
    CASE 70 TO 89
        PRINT "good"
    DEFAULT
        PRINT "keep going"
ENDSWITCH
```

```text
B
123
apple
pear
243
good
```

A `;` at the end of a `PRINT` keeps the line open; a `,` between values
prints a space.

## 6. Functions and subs

```basic
FUNC Area(w, h)
    RETURN w * h
ENDFUNC

FUNC Greet$(name$, greeting$ = "Hello")
    RETURN greeting$ + ", " + name$
ENDFUNC

SUB Shout(msg$)
    PRINT UCASE$(msg$) + "!"
ENDSUB

FUNC Fact(x)
    IF x <= 1 THEN RETURN 1
    RETURN x * Fact(x - 1)
ENDFUNC

PRINT Area(3, 4)
PRINT Greet$("Ada")
PRINT Greet$("Ada", "Welcome")
Shout("hi")
Shout "there"
PRINT Fact(10)
```

```text
12
Hello, Ada
Welcome, Ada
HI!
THERE!
3628800
```

A `FUNC` returns a value, a `SUB` does not. Parameters may have defaults.
Every builtin name is reserved - a function called `Count` would collide
with the builtin `COUNT`, and jdBasic says so:

```basic
FUNC Count(x)
    RETURN x
ENDFUNC
```

```text
Error #10: FUNC COUNT collides with the builtin function COUNT - choose another name
```

## 7. Errors

```basic
TRY
    DIM small = [1, 2, 3]
    PRINT small[10]
CATCH
    PRINT "caught: "; ERRMSG$
FINALLY
    PRINT "cleanup"
ENDTRY

CONST LIMIT = 10
TRY
    LIMIT = 11
CATCH
    PRINT ERRMSG$
ENDTRY
```

```text
caught: Array index out of bounds: 10
cleanup
Cannot assign to constant 'LIMIT'
```

## 8. Dates

```basic
DIM d = CVDATE("2026-10-01")
PRINT FORMAT_DATE(DATEADD("D", 30, d), "%Y-%m-%d")
PRINT YEAR(d), MONTH(d), DAY(d)
PRINT MATH.PI
```

```text
2026-10-31
2026 10 1
3.141593
```

`DATEADD` takes the unit, then the count, then the date. The math constants
live under `MATH.`: `MATH.PI` and `MATH.E`.

## 9. Modules

A program imports a module by name; jdBasic looks next to the program, in
`JDBASIC_PATH` and in its own `lib/` folder.

```basic
IMPORT TESTKIT
TESTKIT.SUITE("arithmetic")
TESTKIT.EQ(2 + 2, 4, "two plus two")
TESTKIT.REPORT()
```

The [module library](../lib/README.md) has 45 of them, each with its own
page: testing, HTTP clients, web apps, data frames, SQLite, Excel and Word
files, PDF, QR codes, YAML, and more.

## 10. From script to `.exe`

The interpreter is forgiving; the native compiler is not. `jdbasic -c`
compiles with `STRICT` and `EXPLICIT` always on: every variable is declared,
and every value fits the type it goes into.

This runs in the interpreter, and `-c` refuses it:

```basic
total = 0
FOR i = 1 TO 3
    total = total + i * 2
NEXT
PRINT total
```

```text
12
```

```text
error at 1: undeclared variable 'TOTAL'
```

Declared, it compiles and prints the same. A function with an untyped
parameter answers a floating-point number, so the total starts as `0.0`:

```basic
DIM total = 0.0
FUNC Twice(x)
    RETURN x * 2
ENDFUNC
FOR i = 1 TO 3
    total = total + Twice(i)
NEXT
PRINT total
```

```text
12
```

```sh
jdbasic -c totals.jdb      # writes totals.exe next to it
totals.exe
```

The compiled program needs `jdbrt.dll` beside it; `-c` copies it there.

## Five things that trip people up

| | |
|---|---|
| Positions and indexes count from 0 | `MID$`, `INSTR`, `arr[0]`; `INSTR` answers `-1` when absent |
| `IOTA` counts from 1 | `IOTA(5)` is `[1, 2, 3, 4, 5]`; `IOTA(5, 0)` starts at 0 |
| `x = NONE` is true for `0` and `""` | test absence with `TYPEOF(x) = "NONE"` or use `??` |
| Builtin names are reserved | no variable or function called `COUNT`, `LINE`, `STEP`, `LEN`, ... |
| No `\n` in strings | `CHR$(10)`, or `VBNEWLINE` for `CR LF` |

## Where next

| I want to ... | Read |
|---|---|
| look up any statement or builtin | [Language reference](languages.md) - contents at the top, A-Z index at the end |
| work with arrays, data, statistics | [Array pipelines](APL_pipeline.md), [vector and matrix cookbook](howto-vector-matrix-data.md) |
| build a web app or an API | [Web development](WebDev.md) |
| make music or process audio | [Sequencer](SequencerHelp.md), [Audio FX](AudioFX.md), [FX how-to](HowTo-FX.md) |
| let an AI agent program with me | [MCP server](MCP.md) |
| come from Python | [Idioms from Python](idioms-from-python.md) |
| use a ready-made module | [Module library](../lib/README.md) |
| see complete programs | [Sample gallery](../jdb/README.md) |
| build jdBasic myself | [Building from source](BUILD.md) |
