# DF - data frames

`lib/df.jdb` holds a table as named columns and works on it whole:
load a CSV, add computed columns, filter and sort rows, group and
aggregate, join two frames, pivot, summarise, write it back, print it
as a table. A frame is a plain map, `{"names": [...], "cols": [[...],
...]}`, so any function that takes arrays can read the columns
directly. Every operation returns a new frame; the input is never
changed.

Stands in for: pandas (the everyday subset).

## Quick start

```basic
IMPORT DF

DIM sales = DF.READCSV("sales.csv")
sales = DF.WITH(sales, "revenue", DF.COL(sales, "qty") * DF.COL(sales, "price"))
DIM by_region = DF.GROUPBY(sales, "region", {"revenue": ["sum", "revenue"], "orders": ["count"]})
DF.SHOW(DF.SORT(by_region, "revenue", TRUE))
```

## API

### Building a frame

| Call | What it does |
|------|--------------|
| `FROM(matrix, names)` | From a 2D array of rows; a short row is padded with `NONE`. |
| `FROMROWS(rows)` | From an array of maps; the columns are the union of the keys. |
| `FROMCOLS(columns)` | From a map of name to array. |
| `READCSV(path$, [delim$], [types])` | From a CSV with a header row; numbers are typed. `types` is the array `CSVREADER` takes. |

### Shape and access

| Call | What it does |
|------|--------------|
| `NAMES(df)`, `NROWS(df)`, `NCOLS(df)`, `SHAPE(df)` | The column names, the counts, `[rows, cols]`. |
| `HAS(df, name$)` | Whether a column exists. |
| `COL(df, name$)` | The column array itself. |
| `ROW(df, i)` | One row as a map. `ROWS(df)` gives every row. |
| `MATRIX(df)` | The rows as a 2D array. |
| `HEAD(df, [n])`, `TAIL(df, [n])` | The first or last rows, five by default. |
| `SLICE(df, start, [n])` | Rows from an index; without a count, to the end. |

### Columns

| Call | What it does |
|------|--------------|
| `WITH(df, name$, source)` | A column added or replaced. `source` is an array of the frame's length or a function of the row map. |
| `SELECT(df, names)` | The named columns in that order; one name or a list. |
| `DROP(df, names)` | Every column but those. |
| `RENAME(df, old$, new$)` | A column renamed. |

### Rows

| Call | What it does |
|------|--------------|
| `WHERE(df, mask)` | The rows where the mask is true, so `DF.WHERE(df, DF.COL(df, "price") < 10)`. |
| `FILTER(df, pred@)` | The rows for which the predicate on the row map is true. |
| `SORT(df, name$, [desc])` | Ordered by a column, numbers or strings; ties keep their order. |
| `DISTINCT(df, name$)` | The distinct values of a column in first-seen order. |

### Grouping and combining

| Call | What it does |
|------|--------------|
| `GROUPBY(df, keys, aggs)` | One row per distinct key, keys first, in first-seen order. `keys` is a name or a list; `aggs` maps an output name to `[how, column]`. |
| `JOIN(a, b, keys, [how$])` | Rows of `a` with the matching rows of `b`. `keys` is a shared name or `[a_name, b_name]`; `how$` is `inner` or `left`. A name both sides have gets the suffix `_b`. |
| `PIVOT(df, rows$, cols$, value$, [how$])` | One row per value of `rows$`, one column per value of `cols$`, each cell the aggregate of `value$`; an empty cell is `NONE`. |
| `DESCRIBE(df)` | Count, mean, min, max and stdev of every numeric column. |

`how` is one of `sum`, `mean`, `min`, `max`, `median`, `stdev`, `first`,
`last` or `count`; `count` needs no column.

### Output

| Call | What it does |
|------|--------------|
| `WRITECSV(df, path$, [delim$])` | The frame as CSV with a header row. |
| `TEXT$(df, [n], [opts])` | The first `n` rows as a `CONSOLE` table, ten by default, `0` for all; `opts` reaches `CONSOLE.TABLE$`. |
| `SHOW(df, [n], [opts])` | The same, printed. |

## Speed

The workshop sales set, 20000 rows by 5 columns: load 180 ms
interpreted and 140 ms compiled; group by region 40 ms and 7 ms; sort
45 ms and 5 ms. The reductions run on whole columns, so the grouping
loop only gathers.

Self test: `tests/jdlibs/df_selftest.jdb`. Demo: `jdb/demos/jdlibs/df_demo.jdb`.
