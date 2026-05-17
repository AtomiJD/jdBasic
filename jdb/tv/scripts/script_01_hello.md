---
title: Train jdBasic - Lesson 01 - Hello jdBasic
hook: PRINT and Variables in 5 minutes
tags: hello-world, print, dim, variables, beginner, lesson-1
---

The very first lesson of **Train jdBasic** - the two most fundamental
concepts in any programming language: how to print something to the
screen, and how to store values in variables.

## What you'll learn

- `PRINT` for output - strings, numbers, expressions
- `;` (semicolon) glues without spaces, `,` (comma) lays out columns
- `DIM` to declare variables
- Type annotations: `AS STRING`, `AS INTEGER`, `AS DOUBLE`, `AS BOOLEAN`
- Reassignment with bare `=`

## Code from the lesson

```basic
PRINT "Hello, World!"
PRINT 5 + 7
PRINT "The answer is "; 6 * 7

DIM age = 38
PRINT age

DIM name AS STRING = "Atomi"
PRINT "Hello, "; name

age = age + 1
PRINT name, age, age * 365
```

## Next up

Lesson 02 - `IF` statements and `FOR` loops, the two building blocks of
every program.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
