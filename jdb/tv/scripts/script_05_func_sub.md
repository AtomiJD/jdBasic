---
title: Train jdBasic - Lesson 05 - Functions and SUBs
hook: FUNC, SUB, and recursion in 5 minutes
tags: functions, sub, recursion, fibonacci, scope, beginner, lesson-5
---

Lesson 05 of Train jdBasic - naming chunks of code so you can call them
whenever. Two flavours: FUNC for code that returns a value, SUB for
code that just acts.

## What you'll learn

- `FUNC name(params) ... RETURN x ... ENDFUNC`
- `SUB name(params) ... ENDSUB` (no return value)
- Multiple comma-separated parameters
- Local scope: variables inside a FUNC/SUB vanish when it returns
- Recursion - a function calling itself, demonstrated with classic Fibonacci

## Code from the lesson

```basic
FUNC Fib(n)
    IF n = 0 OR n = 1 THEN
        RETURN n
    ENDIF
    RETURN Fib(n - 1) + Fib(n - 2)
ENDFUNC

FOR i = 0 TO 10
    PRINT Fib(i); " ";
NEXT i
PRINT
```

Three lines of recursion, eleven Fibonacci numbers on screen.

## Next up

Lesson 06 - maps, jdBasic's key-value data type (dictionaries in other
languages).

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
