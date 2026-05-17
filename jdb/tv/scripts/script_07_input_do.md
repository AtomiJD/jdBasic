---
title: Train jdBasic - Lesson 07 - INPUT and DO Loops
hook: Make your program talk back
tags: input, do-loop, interactive, exitdo, beginner, lesson-7
---

Lesson 07 of Train jdBasic - until now every program ran the same way
every time. Now the user gets to steer: INPUT for reading the keyboard,
and the DO loop for repeating until a condition holds.

## What you'll learn

- `INPUT "prompt", var$` reads a string from the keyboard
- Same statement reads a number when the variable name has no `$`
- `DO ... LOOP UNTIL condition` runs the block then re-checks
- `EXITDO` to break out of a loop early
- Combining INPUT + DO for an interactive entry session

## Code from the lesson

```basic
DIM total = 0
DIM n = 1
DO
    INPUT "Number (0 to quit): ", n
    total = total + n
LOOP UNTIL n = 0
PRINT "Sum: "; total
```

Adds whatever numbers the user types until they enter 0, then reports the
running total.

## Next up

Lesson 08 - file I/O: saving data between runs by reading and writing
text files.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
