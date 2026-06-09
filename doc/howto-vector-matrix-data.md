# HowTo: Vectors, Matrices & Data in jdBasic

jdBasic is **array-first** (APL heritage). Before reaching for a `FOR` loop, ask:
*is there a whole-array operation that does this?* The array form is almost always
shorter, faster, and clearer. This guide collects the data-wrangling patterns —
building, transforming, grouping, sorting, reshaping, dating and rendering — with
runnable one-liners.

> Companion demos: `jdb/demos/data/` (agg, tally, eomonth, daterange, mvins),
> `jdb/demos/apl/oneliners.jdb`, `jdb/demos/apl/array_idioms.jdb`.
> Authoring rules: the `jdbwrite` skill. Full reference: `doc/languages.md`.

---

## 0. The golden rule — loop last

A `FOR` loop is the fallback, not the first move. Almost every "walk the data and
do X" is a one-liner with `SELECT` / `FILTER` / `REDUCE` / `AGG`, an element-wise
math op, or a reshape. The case studies in §8 turn real nested loops from the
console games into single expressions.

---

## 1. Make data

| Need | Op |
|---|---|
| 1..n | `IOTA(n)` (1-based!), `IOTA(n,0)` for 0..n-1 |
| start..end by step | `RANGE(start, end, step)` |
| n evenly-spaced reals | `LINSPACE(a, b, n)` |
| filled array / shape | `ZEROS([r,c])`, `ONES(n)`, `RESHAPE(data, [r,c])` |
| string → char array | unary `-"abc"` → `["a","b","c"]` (UTF-8 aware) |
| string → tokens | `SPLIT(s$, delim$)`; inverse `JOIN(arr, delim$)` |
| a vector of dates | `DATERANGE(start, end, [unit$], [step])` |

```basic
PRINT IOTA(5)                  ' [1, 2, 3, 4, 5]
PRINT -"abc"                   ' [a, b, c]
PRINT RESHAPE(IOTA(6), [2,3])  ' [[1,2,3],[4,5,6]]
```

---

## 2. Transform element-wise — no loop

All numeric functions vectorise: `IOTA(10) * 2`, `SIN(x / 6)`, `CLAMP(v, 0, 9)`,
`ROUND(x, 2)` all apply across the whole array.

| Need | Op |
|---|---|
| map | `SELECT(fn@, arr)` (postfix `@` makes a funcref) |
| filter | `FILTER(pred@, arr)` |
| fold | `REDUCE(fn@, arr, init)` |
| chain | the `\|>` pipeline with `?` as the slot |

```basic
IOTA(10) |> FILTER(LAMBDA v -> v > 5, ?) |> SELECT(LAMBDA v -> v * 10, ?)
' -> [60, 70, 80, 90, 100]
```

`MAP` is the **hashmap type**, not the higher-order map — use `SELECT`.

---

## 3. Group & summarise (verdichtung)

| Need | Op |
|---|---|
| group + reduce → table | `AGG(keys, values, fn@)` → `[[key, fn(group)], ...]`, one O(n) pass |
| distinct + counts | `TALLY(arr)` → `[[value, count], ...]` |
| bucket into a map | `GROUPBY(keyfn@, arr)` |
| whole-array reducers | `SUM PRODUCT MIN MAX MEAN MEDIAN STDEV VARIANCE` |
| running totals | `CUMSUM`, `CUMPROD`, `SCAN(fn@, arr)` |

```basic
region = ["W","E","W","N","E"] : sales = [120, 90, 60, 200, 75]
AGG(region, sales, LAMBDA g -> SUM(g))   ' [[W,180],[E,165],[N,200]]
TALLY(-"banana")                          ' [[b,1],[a,3],[n,2]]
```

`AGG` keeps the key's **type** (numbers stay numbers); `GROUPBY` stringifies keys.
The reducer receives the whole value-group as an array, so any reducer works
(`SUM`, `MEAN`, `MAX`, `LAMBDA g -> MAX(g) - MIN(g)`, ...).

**Per-row / per-column reduce:** `SUM`/`MIN`/... fold the *whole* array. To reduce
each row of a matrix, map over the rows:

```basic
SELECT(LAMBDA row -> MIN(row), board)    ' min of every row
```

---

## 4. Sort & order

| Need | Op |
|---|---|
| sort a 1-D vector | `SORT(v)` |
| sort indices (APL grade-up) | `GRADE(v)` — the indices that would sort `v` |
| sort a matrix by a dimension | `XSORT(m, dim, descending_bool)` |
| distinct values | `UNIQUE(v)` |
| reverse | `REVERSE(v)` |

**Sort a table by one column** with `GRADE` + an index map. Negate the key column
to sort descending:

```basic
t = TALLY(-"the sea")
order = GRADE(SELECT(LAMBDA r -> -r[1], t))   ' descending by count
SELECT(LAMBDA i -> t[i], order)               ' table reordered
```

---

## 5. Reshape & matrix surgery

| Need | Op |
|---|---|
| transpose | `TRANSPOSE(m)` |
| rotate 90° | `REVERSE(TRANSPOSE(m))` |
| insert a row / column | `MVINS(m, dim, idx, value)` — `dim 1`=col, `0`=row; `idx==count` appends; value = vector or scalar (broadcast) |
| replace a row / column | `MVLET(m, dim, idx, value)` |
| extract a row / column | `SLICE(m, dim, idx)` |
| stack vectors → matrix | `STACK(dim, v1, v2, ...)`, `ZIP(a, b, ...)` |
| flatten nesting | `FLATTEN(x)` |

**Swap two rows** with destructuring (the indexed-target form):

```basic
[a[i], a[j]] = [a[j], a[i]]
```

**Swap two columns** — columns are the *inner* axis, so map over the rows, or
transpose into a row-swap and back:

```basic
SELECT(LAMBDA r -> [r[1], r[0]], a)            ' direct, 2 columns
' general: TRANSPOSE -> swap the two former-columns (now rows) -> TRANSPOSE
t = TRANSPOSE(a) : [t[0],t[1]] = [t[1],t[0]] : a = TRANSPOSE(t)
```

**Scatter-assign** writes a value at *vector* coordinates — the trick behind the
one-line plots: `canvas[Yvector, Xvector] = "*"`.

---

## 6. Dates as data

| Need | Op |
|---|---|
| parse / build | `CVDATE("YYYY-MM-DD")`, `DATE.UTC(y,m,d)` |
| format | `FORMAT_DATE(date, "%Y-%m-%d")` (strftime specifiers) |
| extract | `YEAR DAY MONTH HOUR MINUTE SECOND WEEKDAY` (vectorise over a date array) |
| shift | `DATEADD(part$, num, date)` — **num BEFORE date** |
| difference | `DATEDIFF(part$, d1, d2)` |
| month end / days-in-month | `EOMONTH(date, [offset])`; `DAY(EOMONTH(d))` = days in month, leap-safe |
| date vector | `DATERANGE(start, end, [unit$], [step])` — D/W/M/Y are DST-safe calendar steps |

**Group dates by month**: key each date with `YEAR(d)*100 + MONTH(d)` and `AGG`.
The reducer still holds the real dates, so days-in-month for the bucket is
`DAY(EOMONTH(group[0]))` — no rebuilding a date from the `YYYYMM` integer.

```basic
ym = SELECT(LAMBDA d -> YEAR(d)*100 + MONTH(d), dates)
AGG(ym, dates, LAMBDA g -> LEN(g))      ' count per month, in calendar order if sorted
```

---

## 7. Render grids & tables

`FRMV$(matrix)` right-aligns a 2-D array into a string; with a format it runs
`FORMAT$(fmt, col0, col1, ...)` per row using C++20 `{}` specifiers:

```basic
PRINT FRMV$(table, "  {:<8} {:>5}")     ' left-pad text col, right-pad number col
```

**Plot into a character canvas** — blank matrix + computed Y from X + scatter:

```basic
W=40:H=20:C=RESHAPE([" "],[H,W]):X=IOTA(W)-1:Y=INT((SIN(X/6)+1)*((H-1)/2)):C[Y,X]="*":PRINT FRMV$(C)
```

**Neighbour sums** over a grid (cellular automata, minesweeper) — `CONVOLVE(grid,
kernel, wrap_mode)` with a 3×3 ones kernel:

```basic
CONVOLVE(mines, [[1,1,1],[1,0,1],[1,1,1]], 0)   ' 8-neighbour mine counts
```

---

## 8. Pulling APL out of loops — case studies

Lifted from the console games in `jdb/demos/games/`. See `jdb/demos/apl/array_idioms.jdb`.

### 8.1 Rotate a Tetris piece
A 4-deep copy loop collapses to one expression:
```basic
FUNC rotate90(m) : RETURN REVERSE(TRANSPOSE(m)) : ENDFUNC
```

### 8.2 Minesweeper neighbour counts
The classic `FOR dr / FOR dc` over the 8 neighbours of every cell is a single
convolution:
```basic
counts = CONVOLVE(mines, [[1,1,1],[1,0,1],[1,1,1]], 0)
```

### 8.3 Tetris line clear
"Scan each row; if full, shift everything down" becomes filter-and-refill:
```basic
kept = FILTER(LAMBDA row -> MIN(row) = 0, board)      ' rows with a gap survive
cleared = LEN(board) - LEN(kept)
FOR r = 1 TO cleared : kept = MVINS(kept, 0, 0, RESHAPE([0],[COLS])) : NEXT
```

### 8.4 Hand-rolled helpers
`FUNC sMAX(arr) ... ENDFUNC` that returns the larger of two elements is just `MAX`.

---

## 9. Gotchas (quick recap — full list in the `jdbwrite` skill)

- **0/1-based:** `IOTA` is 1-based; `MID$` and `INSTR` are 0-based.
- **`DATEADD(part$, num, date)`** — the count comes *before* the date.
- **Reserved identifiers:** never name a variable `CLS`, `PI`, `E`, `STEP`, `LINE`,
  `ON`, `TICK`, `VAL`, or any builtin. Identifiers are case-insensitive.
- **Array copy:** bare `=` can share storage; force a fresh copy with `+ 0`.
- **Short-circuit:** use `ANDALSO` / `ORELSE` when the right side could fault.
- **Verify, don't guess:** `jdb_doc <name>` / `jdb_eval "PRINT ..."` confirm a
  builtin's signature and arg order before you build on it.
