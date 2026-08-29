---
name: jdbwrite
description: Read BEFORE writing or editing ANY jdBasic (.jdb) code, or jdBasic snippets sent through the MCP. Pre-flight checklist that stops the recurring mistakes - reserved-identifier collisions (CLS, PI, E, STEP, LINE, ON, TICK, VAL ...), undocumented syntax, guessed builtin signatures, hand-rolled loops where an APL idiom exists, and noise comments. jdBasic repo at D:\usr\dev\cc.
---

# Writing jdBasic - pre-flight

Read this top to bottom before authoring or editing jdBasic. It exists because the
same handful of mistakes keep recurring. Six rules, then a trap table.

## 1. Verify, never guess - use the MCP
The jdBasic MCP (`jdbasic-stdio-win`) is a live VM. Ask it instead of assuming:
- `jdb_doc <name>` - does this command exist, what are its parameters and their **order**? (Today's `DATEDIFF`/`DATEADD` arg-order bug came from guessing.)
- `jdb_eval "PRINT ..."` - probe a one-liner to confirm a builtin's signature, arg order, 0/1-based indexing and return shape **before** building on it.
- `jdb_check <file>` - lint a finished `.jdb` (faster than `--lint` - the MCP-VM stays warm). Lint before handing code over.
- Probe new **variable names** too: `--lint` won't catch a `DIM pi` shadowing the `PI` constant - eval it.

If a function is not in `doc/languages.md` AND doesn't probe-eval cleanly, do not use it.

## 2. Only documented syntax
Use only syntax/functions from `doc/languages.md` or a known-good example under `jdb/` or `tests/`. Don't invent keywords or assume "it works like other BASICs". (New keywords you add must also land in `languages.md` + `help.txt`.)

## 3. Reserved identifiers - NEVER use as variable / param / function names
Identifiers are **case-insensitive** (`V` and `v` are the same slot).
- Built-in CONSTs - never `DIM`/assign these: `PI  E  TAU  TRUE  FALSE  NULL  INF  NAN`
- Keywords / builtins that bite as names: `CLS  STEP  LINE  ON  TICK  VAL  LEN` - and in general **any builtin function name** (`SUM MIN MAX DAY MONTH YEAR SORT COUNT` ...).

Rule of thumb: if a name appears in `languages.md`/`help.txt`, don't reuse it as your own identifier. When unsure, probe-eval it, or pick a non-colliding name.

**Code is English-only.** All identifiers (variables, params, functions) and comments are written in **English** - `class`, `count`, `amount`, `items`, never German (`klass`, `anzahl`). Both `class` and `count` are fine in jdBasic. When the natural English word IS a reserved name, qualify it in English (`lineSeg`, `tickMs`, `valStr`, `n_items`) - do **not** fall back to German or a misspelling. (German stays only in chat replies to Atomi, never in the code.)

## 4. Idiom first - search languages.md, prefer APL over loops
Before writing a `FOR` loop, look for a one-shot:
- string → char array: unary `-"abc"` → `["a","b","c"]` (UTF-8 aware), not a `MID$` loop.
- map / filter / reduce: `SELECT(fn@, arr)`, `FILTER(pred@, arr)`, `REDUCE`, `AGG`, `TALLY`.
- ranges / grids: `IOTA` (1-based; `IOTA(n,0)` 0-based), `DATERANGE`, `RANGE`, `LINSPACE`.
- element-wise compare, `CUMSUM`, `GRADE` (sort indices), `SORT`, `UNIQUE` beat manual loops.

A `FOR` loop is the fallback, not the first move - the vectorised form is usually shorter **and** faster. If an algorithm feels long, search `languages.md` for a shorter path first.

## 5. Comments - functionality only
Comment **only** what is not obvious from the code or the variable/function names.
- NO bug-story comments ("without this X did Y", "fixed the crash by ...").
- NO "Atomi said / wanted" notes, NO project-name references (vo / emu / rpg / Vertreter).
- A good comment explains a non-obvious **why** or a tricky algorithm step. If the code already says it, delete the comment.

## 6. When it's written
`jdb_check` it (or run it via `build/jdBasic.exe`), confirm it's clean, **then** hand it over. For GUI/graphics output, verify visually with the `jdbeyes` skill rather than trusting an exit code.

## Trap table - the ones that actually bit
| Wrong | Right |
|---|---|
| `DATEADD(date, n)` | `DATEADD(part$, num, date)` - num BEFORE date |
| `MID$(s, 1, 1)` for 1st char | MID$ is 0-based: `MID$(s, 0, 1)` |
| `INSTR(...) > 0` test | INSTR 0-based, `-1` if absent: test `>= 0` |
| `IOTA(n)` expecting 0-based | IOTA is 1-based; `IOTA(n, 0)` for 0-based |
| `APPEND arr, v` (statement) | `arr = APPEND(arr, v)` (it's a function) |
| `MAP` for higher-order map | `SELECT(fn@, arr)`; `MAP` is the hashmap type |
| `AND` / `OR` with a crashy RHS | `ANDALSO` / `ORELSE` short-circuit |
| `TIMER` for per-frame dt | `TICK()` (ms); TIMER is integer seconds |
| `DIM pi`, `DIM cls` | reserved - rename |
| bare `=` array copy, then mutate | force a fresh copy with `+ 0` |
| `LEFT$(s, INSTR(s,d) - 1)` | INSTR is 0-based: `LEFT$(s, INSTR(s,d))` |
| `IF v = NONE` (TRUE for ANY value) | test missing key with `TYPEOF(v) = "NONE"` |
| `"" + obj{"missing"}` -> `"NONE"` | guard with TYPEOF first, then coerce |
