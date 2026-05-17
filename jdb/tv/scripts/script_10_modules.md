---
title: Train jdBasic - Lesson 10 - Modules + Season 1 Wrap-Up
hook: Code worth reusing, plus a 10-lesson recap
tags: modules, export, import, code-reuse, finale, season-1, lesson-10
---

Lesson 10 of Train jdBasic - the final episode. We package a couple of
functions as a reusable module, save it as its own file, import it from
a fresh program, and call its exports with the dotted module name. Then
a quick walk back through the whole series.

## What you'll learn

- `EXPORT MODULE NAME` declares a module
- `EXPORT FUNC ...` / `EXPORT SUB ...` make functions visible to importers
- `SAVE filename` writes the module to disk (lowercase filename matches the module name)
- `IMPORT MATHX` in another file loads the module
- Call exports with `MATHX.PERCENT(...)` - dotted module-name syntax
- Plus a clamp/percent demo module that builds on everything from the series

## Code from the lesson

```basic
' mathx.jdb
EXPORT MODULE MATHX

EXPORT FUNC PERCENT(part, whole)
    RETURN (part / whole) * 100
ENDFUNC
```

```basic
' another program
IMPORT MATHX
PRINT MATHX.PERCENT(75, 200)
```

Two files, one IMPORT line, and the exports are yours.

## Train jdBasic - the season

01 PRINT and variables, 02 IF and FOR, 03 arrays and vector ops, 04
strings, 05 functions and recursion, 06 maps, 07 INPUT and DO, 08 file
I/O, 09 graphics, 10 modules. Everything you need to start building real
programs.

## Links

- jdBasic source + docs: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
- More at jdbasic.org

Thank you for watching the series.
