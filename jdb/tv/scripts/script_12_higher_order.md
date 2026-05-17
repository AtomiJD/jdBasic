---
title: Train jdBasic - Lesson 12 - Higher-Order Functions
hook: SELECT, FILTER, REDUCE - functions as data
tags: higher-order, lambda, select, filter, reduce, pipe, closures, functional, lesson-12
---

Lesson 12 of Train jdBasic - functions become data. Pass them as
arguments, build them inline as lambdas, chain them with the pipe
operator, and trade a stack of FOR loops for one line of intent.

## What you'll learn

- `Twice@` (the trailing `@`) passes the function itself instead of calling it
- `SELECT(fn@, arr)` maps a function across an array (the classic "map")
- `FILTER(pred@, arr)` keeps elements where the predicate returns TRUE
- `REDUCE(fn@, arr)` collapses an array into a single value
- Lambdas (`LAMBDA x : expr`) for one-off transformations without naming them
- The pipe operator threads a value through a series of stages, with `?` as the placeholder
- Closures via `LAMBDA USE(n) ...` bake an outer variable into the returned function

## Code from the lesson

```basic
FUNC Twice(x)
    RETURN x * 2
ENDFUNC

DIM nums = [1, 2, 3, 4, 5]
PRINT SELECT(Twice@, nums)
```

Prints `[2, 4, 6, 8, 10]`. Same idea drives FILTER and REDUCE - hand them
a function reference, get a transformed array (or a single value) back.

## Next up

Lesson 13 - the web: fetching real data from an HTTP API and parsing the
JSON response.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
