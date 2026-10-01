# Compiling to a native .exe

[Docs home](../README.md) · [Tour](../tour.md) · [Language reference](../languages.md) · [Module library](../../lib/README.md)

This guide is for you if you have a jdBasic program that runs in the
interpreter and you want a standalone Windows executable, or if you write new
code that is meant to be compiled. The compiler is built into `jdbasic` itself
(LLVM backend, `NATIVEC` build flag); there is no separate tool to install.

## 1. The smallest compile

Save this as `hello.jdb`:

```basic
PRINT "Hello from a compiled program"
```

```text
Hello from a compiled program
```

Compile and run it:

```sh
jdbasic -c hello.jdb
hello.exe
```

The compiler prints its steps on stderr and ends with `Compiled: hello.exe`.

### The command line options that matter

| Option | What it does |
|---|---|
| `-c`, `--compile` | compile the script to a native `.exe` instead of running it |
| `-o`, `--output <file>` | name the `.exe`; the default is the script name with `.exe` |
| `--lint` | parse and check only, nothing runs; lists undeclared names |
| `--emit-ir` | print the LLVM IR to stdout instead of writing an `.exe` |
| `-t`, `--time` | print execution timing after an interpreted run |

The options come before the script name, because everything after the script
name is passed to the program:

```sh
jdbasic -c -o dist/app.exe app.jdb
```

The folder in `-o` must already exist. If it does not, the compile stops with
`Failed to emit object file: no such file or directory`.

### What comes out, and what to ship

`-c` writes the `.exe` and copies the runtime next to it, so the program runs
from any folder:

| File | Purpose |
|---|---|
| `hello.exe` | your program |
| `jdbrt.dll` | the jdBasic runtime every compiled program loads |
| `SDL3.dll`, `SDL3_image.dll`, `SDL3_mixer.dll`, `SDL3_ttf.dll` | graphics, images, sound, fonts |
| `libssl-3-x64.dll`, `libcrypto-3-x64.dll` | HTTPS and crypto |
| `jdbasic_default.ttf` | the default font a `SCREEN` window loads |

The compiler copies every DLL that `jdbrt.dll` needs and that sits beside
`jdbasic.exe`, so the exact list follows the features your jdBasic was built
with. To run the program on another PC, copy the whole folder: the `.exe` plus
everything listed above. The OpenSSL DLLs use the Microsoft Visual C++ runtime;
if the other PC reports a missing `VCRUNTIME140.dll`, install the Visual C++
Redistributable (x64) there.

Modules you `IMPORT` are resolved at compile time and built into the `.exe`.
You do not ship `.jdb` files, and changing `JDBASIC_PATH` later has no effect
on a compiled program.

Command line arguments reach the compiled program through `OS.ARGS()` as they
do in the interpreter. Element 0 is the script or the `.exe`, the first argument is
element 1:

```basic
DIM args = OS.ARGS()
IF LEN(args) < 2 THEN
    PRINT "usage: greet name"
ELSE
    PRINT "Hello, "; args[1]
ENDIF
```

`jdbasic greet.jdb Ann` and `greet.exe Ann` both print `Hello, Ann`.

## 2. STRICT and EXPLICIT

The interpreter creates a variable the first time you assign it and lets it
hold any type. The compiler does not: `-c` always compiles with `STRICT` and
`EXPLICIT` on, and there is no switch to turn that off for the main file.

* **EXPLICIT**: every variable is declared with `DIM` before it is used.
  `FOR` and `FOR EACH` declare their own loop variables.
* **STRICT**: every value fits the type of the variable it goes into. A
  `DIM` without `AS` takes its type from the initial value, so `DIM total = 0`
  is an integer and `DIM total = 0.0` is a double.

The compiler needs this to produce machine code with fixed types. A loose
program that the interpreter runs and `-c` rejects is expected behaviour.

### A loose program

This runs in the interpreter:

```basic
prices = [0.5, 0.75, 1.25]
qty = [4, 3, 1]
total = 0
FOR i = 0 TO LEN(qty) - 1
    total = total + prices[i] * qty[i]
NEXT
PRINT "total: "; total
cents = total * 100
PRINT "cents: "; cents
```

```text
total: 5.5
cents: 550
```

`jdbasic -c` refuses it and lists every problem:

```text
error at 1: undeclared variable 'PRICES'
error at 2: undeclared variable 'QTY'
error at 3: undeclared variable 'TOTAL'
error at 8: undeclared variable 'CENTS'
error at 5: STRICT: cannot assign DOUBLE to INTEGER 'TOTAL'; wrap with CINT() to assign explicitly
5 error(s). Compilation aborted.
```

The undeclared names need a `DIM`. The STRICT error on line 5 says that
`total` starts as the integer `0`, but `prices[i] * qty[i]` is a double.

### The compilable version

```basic
DIM prices = [0.5, 0.75, 1.25]
DIM qty = [4, 3, 1]
DIM total AS DOUBLE = 0
FOR i = 0 TO LEN(qty) - 1
    total = total + prices[i] * qty[i]
NEXT
PRINT "total: "; total
DIM cents AS INTEGER = CINT(total * 100)
PRINT "cents: "; cents
PRINT "same total: "; SUM(prices * qty)
```

```text
total: 5.5
cents: 550
same total: 5.5
```

It prints the same in both backends. The last line shows the array form:
`prices * qty` multiplies element by element and `SUM` adds the result, with
no loop and no accumulator to type.

`jdbasic --lint file.jdb` lists every undeclared name without compiling, which
is a quick first pass when you convert a large program. The type checks only
run under `-c`.

### Fixing the common errors

| Message | Fix |
|---|---|
| `undeclared variable 'X'` | add `DIM x = ...` or `DIM x AS type` before the first use |
| `cannot assign DOUBLE to INTEGER 'X'; wrap with CINT()` | declare `x` `AS DOUBLE` (or start it at `0.0`), or convert with `CINT(...)` if you want the integer part |
| `cannot assign STRING to INTEGER 'X'; wrap with VAL() to parse the string` | parse with `VAL(s$)` |
| `Type Mismatch in DIM 'X': expected INTEGER, got STRING` | the same: `VAL(s$)`, or declare the variable as a string |

The conversion functions:

* `CINT(x)` turns a number into a 32-bit integer, truncating toward zero.
  `CLNG(x)` does the same for 64-bit.
* `CDBL(x)` turns a number into a double.
* `VAL(s$)` parses a string into a number. Use it for text; `CINT` and `CDBL`
  convert numbers.
* `STR$(x)` turns a number into a string.

The type names for `DIM` and for parameters are `INTEGER`, `DOUBLE`,
`STRING`, `BOOLEAN`, `ARRAY` and `MAP` (plus the sized types listed under
[Data Types](../languages.md#data-types)). A name ending in `$` is a string.

```basic
DIM row$ = "apple,4,0.5"
DIM parts$ = SPLIT(row$, ",")
DIM n AS INTEGER = VAL(parts$[1])
DIM price AS DOUBLE = VAL(parts$[2])
DIM cost AS DOUBLE = n * price
PRINT parts$[0]; ": "; n; " x "; price; " = "; cost
DIM whole AS INTEGER = CINT(cost * 1.5)
PRINT "rounded down: "; whole
DIM ratio AS DOUBLE = CDBL(n) / 3
PRINT "ratio: "; ratio
DIM msg$ = "count " + STR$(n)
PRINT msg$
```

```text
apple: 4 x 0.5 = 2
rounded down: 3
ratio: 1.333333
count 4
```

## 3. Functions, arrays, maps and strings

Inside a `FUNC` or `SUB` the same rules apply: locals need `DIM`. Parameters
and return values need a type where the compiler cannot work it out on its
own. Annotate them when they carry anything other than a plain number.

* A parameter that receives an array: `AS ARRAY`.
* A parameter that receives a string: `AS STRING`, or a name ending in `$`.
* A parameter that receives a map: `AS MAP`.
* A `FUNC` that returns an array: `AS ARRAY` after the parameter list.
* `AS INTEGER`, `AS DOUBLE`, `AS BOOLEAN` and `AS STRING` on a `FUNC` make it
  return that type, in the interpreter and in the compiled program alike.

```basic
FUNC Area(w AS DOUBLE, h AS DOUBLE) AS DOUBLE
    RETURN w * h
ENDFUNC

FUNC IsEven(n AS INTEGER) AS BOOLEAN
    RETURN n MOD 2 = 0
ENDFUNC

FUNC Squares(xs AS ARRAY) AS ARRAY
    RETURN xs * xs
ENDFUNC

FUNC Tag$(s AS STRING) AS STRING
    RETURN "<" + s + ">"
ENDFUNC

FUNC IsShort(s AS STRING) AS BOOLEAN
    RETURN LEN(s) <= 3
ENDFUNC

PRINT Area(2.5, 4)
PRINT IsEven(10); " "; IsEven(7); " "; TYPEOF(IsEven(10))
PRINT Squares([1, 2, 3, 4])
DIM names$ = ["ann", "robert", "cy"]
PRINT SELECT(Tag$@, names$)
PRINT FILTER(IsShort@, names$)
```

```text
10
TRUE FALSE BOOLEAN
[1, 4, 9, 16]
[<ann>, <robert>, <cy>]
[ann, cy]
```

### What happens without the annotations

Missing annotations in a `FUNC` do not always stop the compile. Under `-c` an
untyped parameter or return value is treated as a number, and the program
computes with the wrong value:

```basic
FUNC Squares(xs)
    RETURN xs * xs
ENDFUNC

FUNC Tag(s)
    RETURN "<" + s + ">"
ENDFUNC

PRINT Squares([1, 2, 3, 4])
DIM names$ = ["ann", "cy"]
PRINT SELECT(Tag@, names$)
```

The interpreter prints `[1, 4, 9, 16]` and `[<ann>, <cy>]`. The compiled
program prints `0` and `[<0>, <0>]`. Compare the two outputs whenever you
compile a program for the first time.

The rule matters most for the functions you hand to `SELECT`, `FILTER` and
`AGG`: a mapper or predicate that receives a string needs `AS STRING` (or a
`$` name), and an `AGG` reducer receives each group as an array, so its
parameter is `AS ARRAY`. The details are in the notes under
[Array & Matrix Functions](../languages.md#array--matrix-functions).

### Maps

Declare a map with `DIM name AS MAP` and pass it to a `SUB` or `FUNC` as
`AS MAP`. Changes the `SUB` makes are visible to the caller:

```basic
DIM stock AS MAP = {"apple": 12, "pear": 0}

SUB Restock(m AS MAP, item$, n AS INTEGER)
    m{item$} = m{item$} + n
ENDSUB

FUNC InStock(m AS MAP) AS ARRAY
    DIM found$ = []
    FOR EACH k$ IN MAP.KEYS(m)
        IF m{k$} > 0 THEN found$ = APPEND(found$, k$)
    NEXT
    RETURN found$
ENDFUNC

Restock(stock, "pear", 5)
PRINT stock{"pear"}
PRINT InStock(stock)
FOR EACH item$ IN MAP.KEYS(stock)
    PRINT item$; " = "; stock{item$}
NEXT
```

```text
5
[apple, pear]
apple = 12
pear = 5
```

### Modules

An `IMPORT`ed module is compiled into the `.exe` with your program. It stays
loose unless it opts in with its own `OPTION` lines, with one exception: a
`FUNC` or `SUB` in a module that assigns a name it never declared is refused
under `-c`, with the name and line. Add a `DIM` for that name in the module.

## 4. What stays interpreter-only

The compiler covers the language, not the whole REPL. These are rejected at
compile time with a message that names the reason (from
[What `-c` will not compile](../languages.md#what--c-will-not-compile)):

| Construct | Use instead |
|---|---|
| `PYTHON$`, `PY.*` | run it in the interpreter |
| `SQL.QUERY` | `SQL.TABLE` and `SQL.COLUMNS` |
| `GROUPBY(fn@, ...)`, `INTEGRATE(fn@, ...)` | run it in the interpreter; for grouping, `AGG` compiles |
| `HELP`, `HELP$` | run it in the interpreter |
| `JSON.STRINGIFY$(<UDT>)` | pass a MAP or ARRAY, or build the JSON from the fields |
| `name@` that resolves to nothing | check the name and the number of parameters |

A rejected construct looks like this:

```text
error at 7: JSON.STRINGIFY$ does not support a UDT instance (type 'PT') under -c: pass a MAP/ARRAY, or build the JSON from its fields
1 error(s). Compilation aborted.
```

Two more differences:

* `FOR EACH` over a channel is interpreter only.
* A compiled program has no garbage collector. It releases strings where it
  can prove that is safe; the rules are in
  [Memory in a compiled program](../languages.md#memory-in-a-compiled-program).

## 5. When compiling pays off

Compiling pays off for tight scalar loops: arithmetic on numbers, nested
`FOR` loops, early exits. The measurements in
[jdb/bench/Results.md](../../jdb/bench/Results.md) give these wall-clock
times:

| Benchmark | Interpreter | `--compile` | C++ (MSVC `/O2`) |
|---|---:|---:|---:|
| Pi via Leibniz series, 100 000 000 iterations | 20 789 ms | 1 293 ms | 946 ms |
| Mandelbrot 800 x 600, max_iter 1000 | 28 531 ms | 706 ms | 1 032 ms |

The same page also shows the other side: whole-array operations already run
inside the runtime, so code written with `SUM`, element-wise operators and
the other array builtins gains less from compiling. On its Game of Life bench
the array form takes 27 ms per step on a 256 x 256 field against 170 ms for
the loop. Try the array form first, and compile when a loop remains the
bottleneck.

To compare, time the interpreted run with `jdbasic -t file.jdb`.

## 6. Windows forms applications

Programs built with `FORM.*` compile with `-c` like any other program; the
[GUI guide](./gui.md) walks through them.

## 7. Where next

| I want to ... | Read |
|---|---|
| see a large program that compiles under STRICT | [tests/gate/native_test.strict.jdb](../../tests/gate/native_test.strict.jdb) |
| see the full list of what `-c` rejects | [What `-c` will not compile](../languages.md#what--c-will-not-compile) |
| set the version, company name and icon of the `.exe` | [Setting EXE file properties](../languages.md#setting-exe-file-properties-jdbprops-sidecar) |
| understand memory in compiled programs | [Memory in a compiled program](../languages.md#memory-in-a-compiled-program) |
| look up types and conversions | [Data Types](../languages.md#data-types), [Conversion](../languages.md#conversion) |
| write array code instead of loops | [Array pipelines](../APL_pipeline.md) |
| build a window application | [GUI guide](./gui.md) |
| build jdBasic with the `NATIVEC` flag | [Building from source](../BUILD.md) |
