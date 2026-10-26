# jdBasic for language models

[Docs home](README.md) · [Tour](tour.md) · [Language reference](languages.md) · [Module library](../lib/README.md)

This page is written for a language model that has to read, write or explain
jdBasic code, and for the person who hands it to one. It states what jdBasic
is, the syntax that differs from other BASICs and from Python, and the
mistakes a model makes when it guesses. Every example with an output block is
run by CI (`tools/check_doc_examples.jdb`), and the output shown is what it
prints.

## What jdBasic is

- **jdBasic** is a BASIC dialect with a bytecode VM, APL-style arrays and a
  native compiler. It was created by Atomi JD (GitHub `AtomiJD`) and is MIT
  licensed. The current release line is 1.1.
- **One language, two backends.** The interpreter (`jdbasic prog.jdb`, or the
  REPL) is forgiving. `jdbasic -c prog.jdb` compiles the same source through
  LLVM to a native `.exe` that loads `jdbrt.dll`; the compiler always applies
  `STRICT` and `EXPLICIT`.
- **Built in:** graphics (SDL3), Dear ImGui, native Win32 forms, a terminal
  UI, sound and a music sequencer, an HTTP client and server, TCP/UDP,
  SQLite, JSON, regular expressions, ZIP, COM automation on Windows, local
  LLMs (llama.cpp) and ONNX models, and a foreign function interface.
- **An MCP server.** `jdbasic --mcp` is a Model Context Protocol server: an AI
  agent runs code in a persistent VM, pauses a running program, edits a
  `SUB` and resumes it without a restart. See [The MCP server](#the-mcp-server).
- **Runs on:** Windows (primary), Linux, macOS, bare-metal x86-64 (jdBasic
  OS), RP2350 (PicoCalc) and ESP32-S3 microcontrollers, and in the browser
  at [jdbasic.org/live](https://jdbasic.org/live/index.html).
- **Lineage:** the spirit of VB6 and QBasic (edit and continue, `SUB`,
  `TYPE`, `MSGBOX`, forms) with APL's array operations (`IOTA`, `REDUCE`,
  `SCAN`, `OUTER`, `GRADE`).
- **Home:** [jdbasic.org](https://www.jdbasic.org), source at
  [github.com/AtomiJD/jdBasic](https://github.com/AtomiJD/jdBasic),
  releases on its [releases page](https://github.com/AtomiJD/jdBasic/releases/latest).

## Rules a model gets wrong

Read these before writing any jdBasic. Each one is a real, recurring mistake.

| Wrong guess | jdBasic |
|---|---|
| strings and arrays count from 1 | **0-based**: `arr[0]`, `MID$(s$, 0, 1)` is the first character |
| `INSTR` answers 0 when absent | `INSTR` answers a 0-based position, **-1** when absent: test `>= 0` |
| `IOTA(n)` starts at 0 | `IOTA(5)` is `[1, 2, 3, 4, 5]`; `IOTA(5, 0)` is `[0, 1, 2, 3, 4]` |
| `"a\nb"`, `"say \"hi\""` | **no escapes** in strings: `CHR$(10)`, `CHR$(34)`, `VBNEWLINE` (CR LF), or `""` for one quote |
| `APPEND arr, v` | `APPEND` is a function: `arr = APPEND(arr, v)` |
| `MAP(fn, arr)` | the higher-order map is `SELECT(fn@, arr)`; `MAP` is the dictionary type |
| `5 AND 3` is bitwise | `AND`, `OR`, `NOT`, `XOR` are logical; bitwise is `BAND`, `BOR`, `BXOR`, `BNOT`, `SHL`, `SHR` |
| `AND` stops early | `AND` and `OR` evaluate both sides; `ANDALSO` and `ORELSE` stop early |
| `MAX(a, b)` | `MAX`, `MIN`, `SUM`, `MEAN` reduce **one array**: `MAX([a, b])` |
| `IF x = NONE` tests absence | `x = NONE` is also true for `0` and `""`; use `TYPEOF(x) = "NONE"` or `??` |
| `TYPEOF(m) = "MAP"` | a map is `"OBJECT"`; numbers are `"INT64"` and `"FLOAT64"`, test with `ISNUM` |
| `PI`, `E`, `TAU` | the constants are `MATH.PI` and `MATH.E`; there is no bare `PI` |
| `TIMER` for frame time | `TICK()` answers milliseconds since start |
| `DATEADD(d, 30, "D")` | `DATEADD(unit$, count, date)`: unit first, date last |
| `ENDIF` / `END IF`, `NEXT i` required | blocks close with `ENDIF`, `ENDFUNC`, `ENDSUB`, `ENDTYPE`, `ENDSWITCH`, `ENDTRY`; `NEXT` alone is enough |
| `EXIT FOR`, `END FUNCTION` | single tokens: `EXITFOR`, `EXITDO`, `EXITFUNC`, `EXITSUB`, `EXITSWITCH`, `CONTINUEFOR`, `CONTINUEDO` |
| a `SUB` sets a new global | an assignment in a body writes a global **only if it already exists**; otherwise it makes a local |
| naming a variable `count`, `len`, `line`, `step`, `cls`, `tick`, `val` | every builtin and keyword name is reserved, case-insensitively |
| `b = a` copies an array | arrays and maps are shared; copy numbers with `a + 0`, strings with `TAKE(LEN(a), a)` |
| `x++`, `x += 1` | write `x = x + 1` |

When unsure whether a builtin exists or which order its arguments take, look
it up: `HELP "NAME"` in the REPL, the [language reference](languages.md), or
the MCP tool `jdb_doc`. If a name is in none of them, it does not exist.

## Values and operators

```basic
' A comment starts with an apostrophe
PRINT "Hello, jdBasic"
DIM n = 42
DIM price = 9.95
DIM name$ = "Ada"
DIM ok = TRUE
PRINT TYPEOF(n), TYPEOF(price), TYPEOF(name$), TYPEOF(ok)
PRINT 7 / 2, 7 \ 2, 7 MOD 2, 2 ^ 10
```

```text
Hello, jdBasic
INT64 FLOAT64 STRING BOOLEAN
3.5 3 1 1024
```

- `DIM` declares a variable and takes its type from the value; `DIM x AS
  INTEGER`, `AS DOUBLE`, `AS STRING`, `AS ARRAY`, `AS MAP`, `AS BOOLEAN` state
  it. A `$` at the end of a name is the classic mark for a string.
- Names are case-insensitive: `total` and `TOTAL` are one variable.
- `/` always divides as floating point, `\` is integer division, `^` is power.
- In `PRINT`, a `,` between values prints a space and a trailing `;` keeps the
  line open.
- A long statement continues on the next line after ` _`.

## Strings

```basic
DIM s$ = "jdBasic"
PRINT LEFT$(s$, 2), RIGHT$(s$, 5), MID$(s$, 0, 2), MID$(s$, 2, 5)
PRINT INSTR(s$, "B"), INSTR(s$, "z")
PRINT LEN(s$), UCASE$(s$), LCASE$(s$)
PRINT REPLACE$("a-b-c", "-", "+"), TRIM$("  x  ")
PRINT SPLIT("a,b,c", ","), JOIN(["x", "y"], "/")
PRINT -"abc"
PRINT "say " + CHR$(34) + "hi" + CHR$(34)
DIM who$ = "Ada"
PRINT $"Hello, {{who$}}! 2 + 3 = {{2 + 3}}"
PRINT FORMAT$("{:>8.2f}|{:04d}", 3.14159, 42)
PRINT STR$(12) + "px", VAL("3.5") * 2
```

```text
jd Basic jd Basic
2 -1
7 JDBASIC jdbasic
a+b+c x
[a, b, c] x/y
[a, b, c]
say "hi"
Hello, Ada! 2 + 3 = 5
    3.14|0042
12px 7
```

- `MID$(s$, start, length)` counts `start` from 0.
- Unary minus splits a string into its characters, UTF-8 aware.
- `$"..."` puts expressions between double braces into a string.
- `FORMAT$` uses C++20 format specifiers (`{}`, `{:.2f}`, `{:04d}`, `{:>8}`).

## Arrays

Arrays are where jdBasic is at home: operators and most functions work on a
whole array at once, as in APL. Reach for these before writing a `FOR` loop.

```basic
DIM a = [10, 20, 30]
PRINT a[0], a[2], LEN(a)
a = APPEND(a, 40)
a[1] = 99
PRINT a
PRINT IOTA(5), IOTA(5, 0)
PRINT [1, 2, 3] * 10, [1, 2, 3] + [10, 20, 30]
PRINT SUM(IOTA(100)), MEAN([2, 4, 6]), MAX([3, 9, 4]), MIN([3, 9, 4])
PRINT SORT([3, 1, 2]), REVERSE([1, 2, 3]), GRADE([30, 10, 20])
PRINT TAKE(2, [5, 6, 7]), DROP(1, [5, 6, 7]), UNIQUE([1, 1, 2])
PRINT SLICE(RESHAPE(IOTA(6), [2, 3]), 1, 2)
PRINT RESHAPE(IOTA(6), [2, 3])
PRINT [1, 5, 3] > 2, SUM([1, 5, 3] > 2)
```

```text
10 30 3
[10, 99, 30, 40]
[1, 2, 3, 4, 5] [0, 1, 2, 3, 4]
[10, 20, 30] [11, 22, 33]
5050 4 9 3
[1, 2, 3] [3, 2, 1] [1, 2, 0]
[5, 6] [6, 7] [1, 2]
[3, 6]
[[1, 2, 3], [4, 5, 6]]
[FALSE, TRUE, TRUE] 2
```

- `GRADE` answers the indexes that sort an array; `SLICE(matrix, 1, col)` is
  a column, `SLICE(matrix, 0, row)` a row.
- A comparison of an array answers an array of truth values, and `SUM`
  counts them.
- Other array tools: `SCAN`, `OUTER`, `CUMSUM`, `ROTATE`, `TRANSPOSE`,
  `MATMUL`, `FFT`, `LINSPACE`, `RANGE`, `ZEROS`, `ONES`. See
  [From loops to array pipelines](APL_pipeline.md) and the
  [vector and matrix cookbook](howto-vector-matrix-data.md).

## Functions as values

```basic
PRINT SELECT(LAMBDA x -> x * x, [1, 2, 3])
PRINT FILTER(LAMBDA x -> x MOD 2 = 0, IOTA(10))
PRINT REDUCE(LAMBDA acc, x -> acc + x, [1, 2, 3], 0)
PRINT IOTA(5) |> SUM(?)
FUNC Twice(x)
    RETURN x * 2
ENDFUNC
PRINT SELECT(Twice@, [1, 2, 3])
DIM f = Twice@
PRINT f(21)
PRINT TALLY(["b", "a", "b"])
PRINT AGG(["x", "y", "x"], [1, 2, 3], LAMBDA g -> SUM(g))
```

```text
[1, 4, 9]
[2, 4, 6, 8, 10]
6
15
[2, 4, 6]
42
[[b, 2], [a, 1]]
[[x, 4], [y, 2]]
```

- `LAMBDA params -> expression` writes a function in place;
  `LAMBDA USE(v) x -> x + v` captures a local.
- `Name@` is a reference to a named `FUNC` or `SUB`.
- `|>` pipes a value into the `?` of the next call.
- `TALLY` counts distinct values; `AGG(keys, values, fn)` groups and reduces
  in one pass.

## Maps and JSON

```basic
DIM user = {"name": "Ada", "age": 36}
user{"city"} = "London"
PRINT user{"name"}, user{"age"}, MAP.KEYS(user)
PRINT MAP.EXISTS(user, "email"), TYPEOF(user{"email"})
PRINT user{"email"} ?? "no email"
PRINT TYPEOF(user)
DIM rows = JSON.PARSE$("[{""id"": 1, ""tag"": ""a""}, {""id"": 2, ""tag"": ""b""}]")
PRINT rows[1]{"tag"}, LEN(rows)
PRINT JSON.STRINGIFY$({"ok": TRUE, "n": [1, 2]})
FOR EACH k IN MAP.KEYS(user)
    PRINT k; "="; user{k}; " ";
NEXT
PRINT
```

```text
Ada 36 [name, age, city]
FALSE NONE
no email
OBJECT
b 2
{"ok":true,"n":[1,2]}
name=Ada age=36 city=London
```

- A map is indexed with braces, `m{"key"}`; an array with brackets, `a[0]`.
  They nest: `rows[1]{"tag"}`.
- A missing key reads as `NONE`. `??` gives the left side unless it is
  absent; `m?{"k"}` and `a?[i]` read into something that may not be there.
- `""` inside a string literal is one `"` character.

## The traps, run

```basic
PRINT 0 = NONE, "" = NONE
DIM m = {"a": 0}
PRINT TYPEOF(m{"a"}) = "NONE", TYPEOF(m{"b"}) = "NONE"
PRINT 374 AND 255, 374 BAND 255
PRINT MAX([1, 5]), MIN([4, 2])
PRINT TYPEOF(MATH.PI), ROUND(MATH.PI, 4)
```

```text
TRUE TRUE
FALSE TRUE
TRUE 118
5 2
FLOAT64 3.1416
```

`MAX(1, 5)` is an error: the second argument of `MAX` is the axis of a
matrix, not a second value.

## Control flow

```basic
DIM score = 73
IF score >= 90 THEN
    PRINT "A"
ELSEIF score >= 70 THEN
    PRINT "B"
ELSE
    PRINT "C"
ENDIF
IF score > 50 THEN PRINT "passed"
FOR i = 1 TO 10 STEP 3
    PRINT i; " ";
NEXT
PRINT
FOR EACH w$ IN ["red", "green"]
    PRINT w$; " ";
NEXT
PRINT
DIM k = 1
DO WHILE k < 100
    k = k * 3
LOOP
PRINT k
DIM j = 0
DO
    j = j + 1
    IF j = 2 THEN CONTINUEDO
    IF j > 4 THEN EXITDO
    PRINT j; " ";
LOOP
PRINT
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
passed
1 4 7 10
red green
243
1 3 4
good
```

`WHILE cond ... WEND` and `DO ... LOOP UNTIL cond` exist too. A one-line `IF`
has no `ENDIF`.

## FUNC, SUB and what a body can see

```basic
DIM total = 0

SUB Add(n)
    total = total + n
    carry = n
ENDSUB

FUNC Area(w, h)
    RETURN w * h
ENDFUNC

FUNC Greet$(name$, greeting$ = "Hello")
    RETURN greeting$ + ", " + name$
ENDFUNC

Add(5)
Add 2
PRINT total, TYPEOF(carry)
PRINT Area(3, 4), Greet$("Ada"), Greet$("Ada", "Hi")
```

```text
7 NONE
12 Hello, Ada Hi, Ada
```

- A `FUNC` answers a value with `RETURN`; a `SUB` does not. Both may be
  called before their definition and may be recursive.
- `total` was declared above `Add`, so `Add` writes the global. `carry` was
  not, so it is a local that is gone after `ENDSUB`. This is the most common
  surprise in jdBasic: `DIM` every global a body writes.
- Parameters may have defaults and types: `FUNC Area(w AS DOUBLE, h AS
  DOUBLE) AS DOUBLE`. Arrays and maps arrive as the caller's own; scalars
  are copies.
- A `FUNC` or `SUB` may not take the name of a builtin.

## TYPE, CONST, destructuring

```basic
TYPE Point
    X AS DOUBLE
    Y AS DOUBLE
    FUNC Dist()
        RETURN SQR(THIS.X ^ 2 + THIS.Y ^ 2)
    ENDFUNC
ENDTYPE
DIM p AS Point
p.X = 3
p.Y = 4
PRINT p.Dist()
CONST LIMIT = 10
PRINT LIMIT * 2
DIM a, b
[a, b] = [1, 2]
[a, b] = [b, a]
PRINT a, b
```

```text
5
20
2 1
```

A `TYPE` may have `SUB INIT(...)` as constructor (`DIM p AS Point(1, 2)`) and
`SUB DISPOSE()` as destructor. `ENUM Name ... ENDENUM` declares named
integer constants, read as `Name.Member`.

## Errors

```basic
TRY
    DIM small = [1, 2, 3]
    PRINT small[10]
CATCH
    PRINT "caught: "; ERRMSG$
FINALLY
    PRINT "cleanup"
ENDTRY
TRY
    THROW "custom failure"
CATCH
    PRINT ERRMSG$
ENDTRY
```

```text
caught: Array index out of bounds: 10
cleanup
custom failure
```

Inside `CATCH`, `ERRMSG$` is the message, `ERR` the code and `ERL` the line.

## Files and dates

```basic
DIM path$ = "demo_notes.txt"
TXTWRITER path$, "line one" + CHR$(10) + "line two"
DIM text$ = TXTREADER$(path$)
PRINT SPLIT(text$, CHR$(10))
PRINT FILE.EXISTS(path$)
KILL path$
PRINT FILE.EXISTS(path$)
DIM d = CVDATE("2026-10-01")
PRINT FORMAT_DATE(DATEADD("D", 30, d), "%Y-%m-%d"), YEAR(d), MONTH(d), DAY(d)
PRINT DATEDIFF("D", CVDATE("2026-01-01"), CVDATE("2026-03-01"))
```

```text
[line one, line two]
TRUE
FALSE
2026-10-31 2026 10 1
59
```

`CSVREADER(file$)` reads a table into rows, `CSVWRITER` writes one;
`FILE.OPEN_LINES`, `FILE.READLINE$` and `FILE.AT_EOF` stream a large file
line by line.

## Modules

```basic
IMPORT TESTKIT
TESTKIT.SUITE("arithmetic")
TESTKIT.EQ(2 + 2, 4, "two plus two")
TESTKIT.REPORT()
```

`IMPORT NAME` loads `name.jdb` from next to the program, the working
directory, `JDBASIC_PATH`, `~/.jdbasic/lib` or the `lib/` folder beside the
executable. The [module library](../lib/README.md) has 45 modules: tests,
web apps (`JDWEB`, `TMPL`), data frames, SQLite, Excel and Word files, PDF,
QR codes, YAML and more. A module exports with `EXPORT MODULE`.

## A web server

```basic
FUNC HandleHome(request)
    RETURN "<h1>Hello from jdBasic</h1>"
ENDFUNC

FUNC HandleApi(request)
    RETURN {"status": "ok", "path": request{"PATH"}}
ENDFUNC

HTTP.SERVER.ON_GET "/", "HandleHome"
HTTP.SERVER.ON_GET "/api", "HandleApi"
IF HTTP.SERVER.START(8080) THEN
    PRINT "http://localhost:8080"
    HTTP.SERVER.WAIT()
ENDIF
```

A handler answers a string (sent as HTML), a map (sent as JSON), or a map
with `__http_status`, `__http_body` and `__http_headers` for full control.
Handlers run one at a time, never beside the main program. The client side
is `HTTP.GET$(url$)` and `HTTP.POST$(url$, body$)`. See
[Web development](WebDev.md).

## A graphics window

```basic
SCREEN 640, 480, "Game loop"
DIM running AS INTEGER = 1
SUB OnQuit(info)
    running = 0
ENDSUB
ON "QUIT" CALL OnQuit
DIM x AS DOUBLE = 320
DO WHILE running = 1
    IF GFX.KEYSTATE("Left") THEN x = x - 4
    IF GFX.KEYSTATE("Right") THEN x = x + 4
    CLS 10, 10, 30
    CIRCLE x, 240, 12, TRUE, 255, 220, 0
    TEXT 10, 10, "Arrows to move", 200, 200, 200
    SCREENFLIP
    SLEEP 16
LOOP
GFX.CLOSE
```

Draw into the back buffer, then `SCREENFLIP` shows it and collects input
events. See [Graphics and games](guides/graphics-games.md); for desktop tools,
[Desktop applications](guides/gui.md) covers native forms (`FORM.*`), ImGui
and the terminal UI (`TUI.*`).

## Compiling to a native .exe

The interpreter accepts this; `jdbasic -c` refuses it, because `-c` always
runs with `STRICT` and `EXPLICIT`:

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

The compiler answers `error at 1: undeclared variable 'TOTAL'`. Declared,
it compiles:

```basic
DIM total AS INTEGER = 0
FOR i = 1 TO 3
    total = total + i * 2
NEXT
PRINT total
```

```text
12
```

```sh
jdbasic -c totals.jdb      # writes totals.exe and copies jdbrt.dll beside it
```

Give a parameter that receives a string a `$` name or `AS STRING`, and
declare every variable. A few things stay interpreter-only (`EXECUTE`,
`HELP`, `FOR EACH` over a channel); the compiler says so. See
[Compiling to a native .exe](guides/native.md).

## The MCP server

jdBasic ships its own [Model Context Protocol](https://modelcontextprotocol.io/)
server, so an AI agent (Claude Code, Claude Desktop, Cursor, Cline, Continue,
Zed, Windsurf, ChatGPT Desktop) works inside a live jdBasic VM instead of
guessing:

```json
{
  "mcpServers": {
    "jdbasic": { "command": "C:/path/to/jdBasic.exe", "args": ["--mcp"] }
  }
}
```

All tools share **one persistent VM**: variables, `FUNC`s and loaded data
stay between calls, so a dataset is loaded once and worked on for a whole
session.

| Tool | What it does |
|---|---|
| `jdb_eval` | run jdBasic statements in the persistent VM and return the output |
| `jdb_check` | lint a file or snippet without running it |
| `jdb_doc` | look a name up in the language reference: the answer to "does jdBasic have X" |
| `jdb_load` | load a `.jdb` file so its functions can be called |
| `jdb_stop` / `jdb_status` / `jdb_resume` | pause a running program, inspect it, continue |
| `jdb_recompile` | re-read the edited source and swap the changed `FUNC`/`SUB` into the paused program |
| `jdb_vars` / `jdb_funcs` | list live variables with previews, and the defined functions |
| `jdb_savews` / `jdb_loadws` | save and restore a workspace (globals plus the code that defined them) |
| `jdb_reset` | clear the VM |
| `jdb_run_native` | run a shell command, for example `jdbasic -c prog.jdb` and the produced `.exe` |
| `jdb_nodes` / `jdb_peek` | see programs on the Node Bus and read their published values |

The typical loop for an agent: `jdb_doc` before using an unfamiliar builtin,
`jdb_eval` to probe a one-liner, `jdb_check` before handing code over. For a
running program (a game, a server, a GUI), `jdb_stop` pauses it,
`jdb_eval` reads or changes its variables, the agent edits the source,
`jdb_recompile` swaps the changed `SUB` in, and `jdb_resume` continues with
all state intact. `--tools <dir>` adds your own tools written as jdBasic
`FUNC`s; `--tools-only` offers nothing else. Full reference:
[The MCP server](MCP.md); a walkthrough:
[Programming with an AI agent](guides/ai-mcp.md).

## Where to look things up

| Need | Read |
|---|---|
| a 15-minute introduction | [The tour](tour.md) |
| the exact rules of a statement or builtin | [Language reference](languages.md), or `HELP "NAME"` |
| coming from Python | [Idioms from Python](idioms-from-python.md) |
| arrays, statistics, data | [Array pipelines](APL_pipeline.md), [Vectors, matrices and data](howto-vector-matrix-data.md), [Working with data](guides/data.md) |
| a native .exe | [Compiling to a native .exe](guides/native.md) |
| games and graphics | [Graphics and games](guides/graphics-games.md) |
| desktop applications | [Desktop applications](guides/gui.md) |
| web apps and APIs | [Web development](WebDev.md) |
| working with an AI agent | [Programming with an AI agent](guides/ai-mcp.md), [MCP server](MCP.md) |
| ready-made modules | [Module library](../lib/README.md) |
| complete programs | [Sample gallery](../jdb/README.md) |
| microcontrollers | [jdBasic on a microcontroller](guides/boards.md) |
| building jdBasic | [Building from source](BUILD.md) |
