---
title: Train jdBasic - Lesson 04 - Strings
hook: Slice, search and split text the BASIC way
tags: strings, csv, split, instr, text-parsing, beginner, lesson-4
---

Lesson 04 of Train jdBasic - the string toolkit. Concatenation, slicing,
searching, casing, and a SPLIT that turns one line of CSV into an array
ready to use.

## What you'll learn

- `+` to concatenate, `LEN()` to measure
- String variables end with `$` (a BASIC convention worth keeping)
- `LEFT$` / `RIGHT$` / `MID$` for slicing (positions are zero-based, same as arrays)
- `INSTR$(haystack$, needle$)` returns the index, or `-1` if not found
- `UCASE$` / `LCASE$` / `TRIM$` for the obvious housekeeping
- `SPLIT(s$, delim$)` returns an array of pieces - one line of CSV in, ready-to-index out

## Code from the lesson

```basic
DIM line$ = "Atomi,38,Heidelberg"
DIM parts = SPLIT(line$, ",")
PRINT "Name:  "; parts[0]
PRINT "Age:   "; parts[1]
PRINT "City:  "; parts[2]
PRINT
PRINT UCASE$(parts[0]); " lives in "; parts[2]
```

The same pattern handles real-world text files line by line.

## Next up

Lesson 05 - your own functions and subroutines, code you can name and
reuse.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
