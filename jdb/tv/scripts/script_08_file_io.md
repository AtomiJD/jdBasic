---
title: Train jdBasic - Lesson 08 - File I/O
hook: Save and reload data in two commands
tags: file-io, txtwriter, txtreader, persistence, csv, beginner, lesson-8
---

Lesson 08 of Train jdBasic - until now every value vanished when the
program ended. Time to put data on disk and read it back. jdBasic keeps
the API tiny: no streams, no buffers, no close handles.

## What you'll learn

- `TXTWRITER filename$, content$` writes a whole string to a text file
- `TXTREADER$(filename$)` returns the whole file as a string
- For multi-line files: glue with `CHR$(10)` on write, `SPLIT(..., CHR$(10))` on read
- Binary friends: `BINWRITER` / `BINREADER` preserve raw bytes (including zeros)

## Code from the lesson

```basic
DIM todos$ = "Buy bread" + CHR$(10) + "Walk dog" + CHR$(10) + "Record lesson"
TXTWRITER "todo.txt", todos$

DIM raw$ = TXTREADER$("todo.txt")
DIM lines = SPLIT(raw$, CHR$(10))
PRINT "You have "; LEN(lines); " todos:"
FOR i = 0 TO LEN(lines) - 1
    PRINT "  - "; lines[i]
NEXT i
```

The whole save-and-load cycle in seven lines.

## Next up

Lesson 09 - graphics: opening a window, drawing shapes, setting colours,
putting pixels on the screen.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
