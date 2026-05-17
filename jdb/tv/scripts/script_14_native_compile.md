---
title: Train jdBasic - Lesson 14 - Native Compilation
hook: One EXE, ten times faster
tags: native, compile, llvm, strict, performance, exe, intermediate, lesson-14
---

Lesson 14 of Train jdBasic - the engineering hat. `jdbasic -c` compiles
your program to a standalone Windows .exe via LLVM. Trade: the compiler
is stricter than the interpreter (you write a few AS annotations), and
in return the inner loops run on raw CPU instructions instead of
interpreter dispatch.

## What you'll learn

- `jdbasic -c file.jdb` produces `file.exe` plus the tiny `jdbrt.dll` runtime
- Strict mode (auto-on with `-c`) enforces three rules: DIM before use, AS clause on every DIM, AS clause on every FUNC param + return
- Type annotations: `n AS INTEGER`, `total AS DOUBLE`, etc
- `TICK()` for millisecond-resolution timing
- Side-by-side benchmark: same code interpreted vs native, count the speedup
- When to reach for native: hot CPU loops, shipping to people without jdBasic, type-check sanity pass

## Code from the lesson

```basic
FUNC SumSquares(n AS INTEGER) AS DOUBLE
    DIM total AS DOUBLE = 0.0
    DIM i AS INTEGER
    FOR i = 1 TO n
        total = total + i * i
    NEXT i
    RETURN total
ENDFUNC

DIM t0 AS INTEGER = TICK()
DIM s AS DOUBLE = SumSquares(1000000)
DIM t1 AS INTEGER = TICK()
PRINT "Result : "; s
PRINT "Elapsed: "; (t1 - t0); " ms"
```

A million iterations of `total + i * i`, timed. Run it under the
interpreter, then `jdbasic -c bench.jdb` and run `bench.exe` for the
native timing.

## End of Season 1

That closes the advanced arc. You now have the productivity tools,
the higher-order toolbox, the HTTP and JSON pair, and a way to ship
native binaries. Pick a problem you actually care about and build
something with it.

Thank you for spending fourteen lessons here.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
- More at jdbasic.org
