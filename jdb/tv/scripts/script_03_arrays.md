---
title: Train jdBasic - Lesson 03 - Arrays
hook: Drop the FOR loop, work on the whole array
tags: arrays, vector, broadcast, iota, reductions, beginner, lesson-3
---

Lesson 03 of Train jdBasic - where the language really starts to feel
different from the BASICs you remember. Arrays are first-class citizens,
and most operations work on the whole array at once.

## What you'll learn

- Array literals: `[10, 20, 30]` with zero-based indexing and `LEN()`
- Scalar broadcast: `scores * 2` doubles every element, no FOR loop
- Element-wise math between same-length arrays: `a + b`
- Element-wise comparisons that return a boolean array (one TRUE/FALSE per element)
- Reductions to a single value: `SUM`, `MEAN`, `MIN`, `MAX`
- `IOTA(n)` to generate a `1..n` range without typing them out

## Code from the lesson

```basic
DIM temps = [20, 25, 18, 22, 28, 30, 19]
DIM avg = MEAN(temps)
DIM diff = temps - avg
PRINT "Week:     "; temps
PRINT "Average:  "; avg
PRINT "Diff:     "; diff
PRINT "Hottest:  "; MAX(temps); " degrees"
PRINT "Coolest:  "; MIN(temps); " degrees"
```

Three lines of real work, no FOR loop in sight.

## Next up

Lesson 04 - strings: slicing, searching, splitting.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
