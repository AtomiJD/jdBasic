---
title: Train jdBasic - Lesson 11 - REPL Workflow
hook: Tools that make you fast
tags: repl, tooling, pretty, lint, savews
---

In this episode we leave the language behind for a moment and tour the
**REPL commands** that turn jdBasic into a workflow, not just a parser.

## What you'll learn

- `NEW` clears the program buffer; `CLEARWS` wipes the whole workspace
- `PRETTY` normalises keyword casing and indentation in place
  - `PRETTY STYLE VB` for Pascal-cased keywords if all-caps is too loud
- `LINT` catches undeclared assignments out of the box - no `OPTION "EXPLICIT"` needed
- `TRON` / `TROFF` toggle per-statement trace logging
- `SAVE` / `LOAD` round-trip source code
- `SAVEWS` / `LOADWS` freeze source **and** every live variable into one workspace file

## Code from the lesson

```basic
DIM names = ["Atomi", "Jaydee", "World"]
FOR EACH n IN names
    PRINT "Hello, "; n
NEXT n
```

After running, `SAVEWS "greet_ws"` writes the whole workspace; a later
`LOADWS "greet_ws"` brings back the array without re-running the program -
perfect for picking up an exploration session the next day.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Lesson scripts live under `jdb/tv/scripts/`
