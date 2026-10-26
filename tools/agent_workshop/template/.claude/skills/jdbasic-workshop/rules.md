# jdBasic rules for agents

jdBasic looks like BASIC and behaves in places like APL. Models get the
same few things wrong every time. Read this once per session.

## 1. Verify, never guess

- `jdb_doc NAME` answers whether a builtin exists and what its arguments
  are, in which order. Query the full name (`MAP.EXISTS`, not `MAP`).
- Probe with `jdb_eval "PRINT ..."` before building on a signature, an
  index base or a return shape.
- `jdb_check` lints source without running it.
- The reference is `doc/languages.md` next to the jdbasic executable. A name
  that is not in it and does not probe cleanly does not exist.

## 2. Names you must not use

Identifiers are case-insensitive: `v` and `V` are the same variable.

- Keywords (the linter refuses them): `CLS STEP LINE ON STOP USE`
- Builtin names (they parse, then break later, silently):
  `CD PLACE TICK VAL LEN SUM MIN MAX DAY MONTH YEAR SORT COUNT MAP FUNC
  EXIT STRING INTEGER DOUBLE`, and any other name in `languages.md`
- Constants: `TRUE FALSE NONE NULL NOTHING MATH.PI MATH.E`. There is no
  `PI`, `E`, `TAU`, `INF` or `NAN`; a bare `PI` reads as NONE.

When the natural name is taken, qualify it: `n_items`, `tick_ms`,
`line_seg`, `val_str`.

## 3. Indexing

- `IOTA` is 1-based: `IOTA(5)` is `[1,2,3,4,5]`. `IOTA(5, 0)` is `0..4`.
- Arrays are 0-based: `arr[0]` is the first element.
- String functions are 0-based: `MID$(s, 0, 1)` is the first character.
- `INSTR` answers a 0-based position and `-1` when absent: test `>= 0`.
  `LEFT$(s, INSTR(s, d))` is the part before `d`.

## 4. Syntax that differs from other BASICs

- Strings have no escapes: no `\n`, no `\"`. Use `CHR$(10)` and `CHR$(34)`.
  A single quote `'` starts a comment.
- `APPEND` is a function: `arr = APPEND(arr, v)`.
- Map access uses braces: `m{"key"}`. A missing key reads as NONE; test it
  with `TYPEOF(v) = "NONE"`, never `v = NONE` (that is TRUE for anything).
- Higher-order map is `SELECT(fn@, arr)` or `SELECT(LAMBDA x -> x * 2, arr)`.
  `MAP` is the hashmap type.
- `AND`/`OR` evaluate both sides. Use `ANDALSO`/`ORELSE` when the right side
  could fail.
- `EXITFOR EXITDO EXITFUNC CONTINUEFOR CONTINUEDO` are single words.
- `DATEADD(part$, num, date)`: the number comes before the date.
- `TICK()` is milliseconds for timing; `TIMER` is whole seconds.
- Plain `b = a` may share array storage. `b = a + 0` makes a copy.
- `CSVREADER` reads numbers only. For text columns use `TXTREADER$` and
  `SPLIT`, or `IMPORT DF` and `DF.READCSV`.

## 5. Idiom before loop

jdBasic is vectorised. Before writing a `FOR` loop look for the one-shot:

- element-wise arithmetic and comparison on whole arrays
- `SELECT FILTER REDUCE` with `LAMBDA`
- `AGG TALLY GRADE CUMSUM UNIQUE SORT TAKE DROP`
- `-"abc"` turns a string into an array of characters

The vectorised form is usually shorter and faster. A loop is the fallback.

## 6. Scope in the workshop

Every `jdb_eval` chunk is its own unit. A FUNC or LAMBDA sees the globals of
the chunk it was defined in, not those created by earlier chunks, and a
LAMBDA does not see the locals of the FUNC around it. Pass values in as
parameters. `jdb_loadws` replays everything as one unit, which hides the
problem until the next fresh session.

## 7. Comments

Comment only what the code does not already say. No change history in
comments.
