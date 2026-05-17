---
title: Train jdBasic - Lesson 02 - If and For
hook: Decisions and Loops in 6 minutes
tags: if-then-else, for-loop, fizzbuzz, control-flow, beginner, lesson-2
---

Lesson 02 of Train jdBasic - the two control-flow primitives that every
program needs: making decisions with IF/THEN/ELSE, and repeating work
with FOR/NEXT.

## What you'll learn

- Inline `IF ... THEN ...` for one-line checks at the REPL
- Block-form `IF / ELSE / ELSEIF / ENDIF` for anything bigger
- `FOR i = a TO b NEXT i` with optional `STEP` (positive, negative, fractional)
- `MOD` for divisibility tests
- The full FizzBuzz in 10 lines

## Code from the lesson

```basic
FOR i = 1 TO 15
    IF i MOD 15 = 0 THEN
        PRINT "FizzBuzz"
    ELSEIF i MOD 3 = 0 THEN
        PRINT "Fizz"
    ELSEIF i MOD 5 = 0 THEN
        PRINT "Buzz"
    ELSE
        PRINT i
    ENDIF
NEXT i
```

## Next up

Lesson 03 - arrays, plus jdBasic's vector operations that work on whole
arrays at once with no FOR loop in sight.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
