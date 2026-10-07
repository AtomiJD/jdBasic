# Working with data: files, tables and reports

[Docs home](../README.md) · [Tour](../tour.md) · [Language reference](../languages.md) · [Module library](../../lib/README.md)

This guide walks through the jobs you would do with Python and pandas: read a
CSV or JSON file, pick columns, filter, group and sum, handle dates, and write
the result as CSV, as a text report and as JSON. One small sales table runs
through every step.

The first example writes `sales.csv` next to the program, and the later
examples read it, so run the first one once before the others. The last
example deletes the files again.

## 1. Read a CSV file

`CSVREADER` loads a CSV file into a 2D array, one row per line. With the third
argument `TRUE` it skips the header line; `CSVHEADER` returns that line as an
array of column names. Each cell gets a real type: integers, doubles and
strings.

```basic
DIM lines = ["date,region,item,qty,price", "2026-01-05,North,Pen,10,1.5", "2026-01-12,South,Pad,4,3.25", "2026-01-30,North,Pad,2,3.25", "2026-02-03,East,Pen,25,1.5", "2026-02-17,South,Ink,6,7", "2026-03-02,North,Ink,3,7"]
TXTWRITER "sales.csv", JOIN(lines, CHR$(10)) + CHR$(10)

PRINT CSVHEADER("sales.csv")
DIM rows = CSVREADER("sales.csv", ",", TRUE)
PRINT LEN(rows); " rows"
PRINT rows[0]
PRINT TYPEOF(rows[0][0]); " "; TYPEOF(rows[0][3]); " "; TYPEOF(rows[0][4])
```

```text
[date, region, item, qty, price]
6 rows
[2026-01-05, North, Pen, 10, 1.5]
STRING INT64 FLOAT64
```

A fourth argument forces column types, for example `"STRING"` for ZIP codes
with leading zeros or `"DATE"` for date columns (section 5 uses it). The
options are listed under [File I/O Functions](../languages.md#file-io-functions).

## 2. Read plain text and JSON

For a file that is not CSV, `TXTREADER$` returns the whole file as one string
and `SPLIT` cuts it into lines and fields. The fields are strings; `VAL` turns
one into a number. The final newline leaves an empty last line, which the
`FILTER` drops.

```basic
DIM text$ = TXTREADER$("sales.csv")
DIM lines = FILTER(LAMBDA s$ -> s$ <> "", SPLIT(text$, CHR$(10)))
PRINT LEN(lines); " lines"
DIM fields = SPLIT(lines[1], ",")
PRINT fields
PRINT TYPEOF(fields[3]); " "; VAL(fields[3]) * 2
```

```text
7 lines
[2026-01-05, North, Pen, 10, 1.5]
STRING 20
```

A file written on Windows may end its lines with `CHR$(13) + CHR$(10)`; remove
the carriage returns first with `REPLACE$(text$, CHR$(13), "")`.

JSON goes through two functions: `JSON.STRINGIFY$` turns arrays and maps into
JSON text, and `JSON.PARSE$` turns JSON text back into something you index like
an array or a map. This example writes the sales targets per region and reads
them back.

```basic
DIM targets = [{"region": "North", "target": 40}, {"region": "South", "target": 60}, {"region": "East", "target": 30}]
TXTWRITER "targets.json", JSON.STRINGIFY$(targets)
PRINT TXTREADER$("targets.json")

DIM loaded = JSON.PARSE$(TXTREADER$("targets.json"))
PRINT LEN(loaded); " records"
PRINT loaded[1]{"region"}; " "; loaded[1]{"target"}
```

```text
[{"region":"North","target":40},{"region":"South","target":60},{"region":"East","target":30}]
3 records
South 60
```

## 3. Columns as arrays

`SLICE(rows, 1, n)` takes column `n` (0-based) out of the table as a 1D array.
Arithmetic on arrays works element by element, so `qty * price` is one
expression, and the reducers `SUM`, `MEAN`, `MAX`, `MIN` and `LEN` fold a
whole column.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM region = SLICE(rows, 1, 1)
DIM amount = SLICE(rows, 1, 3) * SLICE(rows, 1, 4)
PRINT region
PRINT amount
PRINT SUM(amount); " "; MEAN(amount); " "; MAX(amount); " "; LEN(amount)
```

```text
[North, South, North, East, South, North]
[15, 13, 6.5, 37.5, 42, 21]
135 22.5 42 6
```

### Filter

A comparison against an array gives an array of booleans, a mask. Multiplying
by the mask keeps the matching values and zeroes the rest, so `SUM(amount *
mask)` is a conditional sum. `COUNT(array, value)` counts matches. `FILTER`
keeps the elements, or whole rows, for which a lambda answers `TRUE`.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM region = SLICE(rows, 1, 1)
DIM amount = SLICE(rows, 1, 3) * SLICE(rows, 1, 4)

DIM north = (region = "North")
PRINT north
PRINT SUM(amount * north); " from "; COUNT(region, "North"); " sales"
PRINT FILTER(LAMBDA a -> a > 20, amount)
PRINT FILTER(LAMBDA r -> r[1] = "North", rows)
```

```text
[TRUE, FALSE, TRUE, FALSE, FALSE, TRUE]
42.5 from 3 sales
[37.5, 42, 21]
[[2026-01-05, North, Pen, 10, 1.5], [2026-01-30, North, Pad, 2, 3.25], [2026-03-02, North, Ink, 3, 7]]
```

### Sort

`SORT` sorts a vector, descending when the second argument is `TRUE`. `GRADE`
returns the indices that would sort it (NumPy's `argsort`); negate the column
to get a descending order, then pick the rows in that order with `SELECT`.
`XSORT(rows, column, descending)` sorts a table by one column directly.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM amount = SLICE(rows, 1, 3) * SLICE(rows, 1, 4)

PRINT SORT(amount)
PRINT SORT(amount, TRUE)
DIM order = GRADE(-amount)
PRINT order
DIM ranked = SELECT(LAMBDA i -> rows[i], order)
PRINT ranked[0]
PRINT XSORT(rows, 3, TRUE)[0]
```

```text
[6.5, 13, 15, 21, 37.5, 42]
[42, 37.5, 21, 15, 13, 6.5]
[4, 3, 5, 0, 1, 2]
[2026-02-17, South, Ink, 6, 7]
[2026-02-03, East, Pen, 25, 1.5]
```

## 4. Group and summarise

`AGG(keys, values, fn)` groups the values by the matching key and applies `fn`
to each group. The result is a two-column table `[[key, result], ...]` in the
order the keys first appear. The reducer receives the whole group as an array,
so a lambda or a builtin reference such as `LEN@` or `MEAN@` works. `TALLY`
counts the distinct values of one array, like pandas `value_counts`.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM region = SLICE(rows, 1, 1)
DIM item = SLICE(rows, 1, 2)
DIM amount = SLICE(rows, 1, 3) * SLICE(rows, 1, 4)

DIM totals = AGG(region, amount, LAMBDA g -> SUM(g))
PRINT totals
PRINT AGG(region, amount, LEN@)
PRINT AGG(region, amount, MEAN@)
PRINT TALLY(item)
```

```text
[[North, 42.5], [South, 55], [East, 37.5]]
[[North, 3], [South, 2], [East, 1]]
[[North, 14.166667], [South, 27.5], [East, 37.5]]
[[Pen, 2], [Pad, 2], [Ink, 2]]
```

`GROUPBY(fn, array)` buckets whole rows instead. It returns a map from the key
(as a string) to the list of rows with that key. `GROUPBY` runs in the
interpreter only; a program you compile with `-c` uses `AGG`.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM by_item = GROUPBY(LAMBDA r -> r[2], rows)
PRINT TYPEOF(by_item); " with "; MAP.SIZE(by_item); " keys"
PRINT by_item{"Ink"}
```

```text
OBJECT with 3 keys
[[2026-02-17, South, Ink, 6, 7], [2026-03-02, North, Ink, 3, 7]]
```

To rank the groups, sort the table by its second column with `GRADE`, as in
section 3. `FRMV$` with a format string prints one formatted line per row; the
format uses C++ `{}` specifiers.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM totals = AGG(SLICE(rows, 1, 1), SLICE(rows, 1, 3) * SLICE(rows, 1, 4), LAMBDA g -> SUM(g))

DIM order = GRADE(SELECT(LAMBDA r -> -r[1], totals))
DIM best = SELECT(LAMBDA i -> totals[i], order)
PRINT best
PRINT FRMV$(best, "{:<6} {:>7.2f}")
```

```text
[[South, 55], [North, 42.5], [East, 37.5]]
South    55.00
North    42.50
East     37.50
```

## 5. Dates

Pass `["DATE"]` as the type list and `CSVREADER` turns the first column into
`DateTime` values. `CVDATE` (or its other name `CDATE`) parses one
`"YYYY-MM-DD"` string. `FORMAT_DATE` writes a date with `strftime` specifiers.

`DATEADD(part$, count, date)` takes the count before the date. `DATEDIFF("D",
d1, d2)` counts calendar days on the local clock, so two midnights are always a
whole number of days apart. `EOMONTH` gives the last day of the month, and
`DAY(EOMONTH(d))` the number of days in it.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE, ["DATE"])
DIM dates = SLICE(rows, 1, 0)
PRINT TYPEOF(dates[0]); " "; FORMAT_DATE(dates[0], "%d.%m.%Y")

DIM due = DATEADD("D", 30, dates[0])
PRINT "due: "; FORMAT_DATE(due, "%Y-%m-%d")
PRINT "next month: "; FORMAT_DATE(DATEADD("M", 1, CDATE("2026-01-30")), "%Y-%m-%d")
PRINT "span: "; DATEDIFF("D", dates[0], dates[LEN(dates) - 1]); " days"
PRINT "month end: "; FORMAT_DATE(EOMONTH(dates[4]), "%Y-%m-%d")
PRINT "days in Feb 2028: "; DAY(EOMONTH(CVDATE("2028-02-01")))
PRINT YEAR(dates[4]); " "; MONTH(dates[4]); " "; DAY(dates[4]); " "; WEEKDAY(dates[4])
```

```text
DATE 05.01.2026
due: 2026-02-04
next month: 2026-02-28
span: 56 days
month end: 2026-02-28
days in Feb 2028: 29
2026 2 17 2
```

A month added to January 30 lands on February 28: the day is clamped to the
last day of a shorter month. `WEEKDAY` counts from 0 for Sunday. The units
`"H"`, `"N"` and `"S"` count elapsed time, so an hour count across a daylight
saving change depends on the time zone; keep those out of output you compare.

### Group by month

Turn each date into a `"YYYY-MM"` key and group with `AGG`. `AGG` only lists
months that have sales; for a complete calendar, build the months with
`DATERANGE(start, end, "M")` and sum each one with a mask. April has no sales
and shows up as zero.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE, ["DATE"])
DIM amount = SLICE(rows, 1, 3) * SLICE(rows, 1, 4)
DIM ym = SELECT(LAMBDA d -> FORMAT_DATE(d, "%Y-%m"), SLICE(rows, 1, 0))
PRINT ym
PRINT AGG(ym, amount, LAMBDA g -> SUM(g))

DIM months = DATERANGE(CVDATE("2026-01-01"), CVDATE("2026-04-01"), "M")
DIM labels = SELECT(LAMBDA m -> FORMAT_DATE(m, "%Y-%m"), months)
DIM per_month = SELECT(LAMBDA k$ -> SUM(amount * (ym = k$)), labels)
PRINT FRMV$(ZIP(labels, per_month), "{}  {:>6.2f}")
```

```text
[2026-01, 2026-01, 2026-01, 2026-02, 2026-02, 2026-03]
[[2026-01, 34.5], [2026-02, 79.5], [2026-03, 21]]
2026-01   34.50
2026-02   79.50
2026-03   21.00
2026-04    0.00
```

The full list of date functions is under
[System and Time Functions](../languages.md#system-and-time-functions).

## 6. Maps as records

A row as an array is compact, but `r[3]` says less than `r{"qty"}`. A map
literal inside `SELECT` turns every row into a record, and the result is an
array of maps, the shape `JSON.PARSE$` also returns. `FILTER` and `SELECT` work
on it the same way.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM sales = SELECT(LAMBDA r -> {"region": r[1], "item": r[2], "amount": r[3] * r[4]}, rows)
PRINT sales[0]
PRINT sales[0]{"amount"}

DIM big = FILTER(LAMBDA s -> s{"amount"} > 20, sales)
PRINT SELECT(LAMBDA s -> s{"item"} + " " + s{"region"}, big)
```

```text
{"region": "North", "item": "Pen", "amount": 15}
15
[Pen East, Ink South, Ink North]
```

A map is also a lookup table. This one maps each region to its target from
`targets.json`. A missing key reads as `NONE`: test it with `MAP.EXISTS` or
`TYPEOF`, or give a default with `??`.

```basic
DIM goal AS MAP
FOR EACH t IN JSON.PARSE$(TXTREADER$("targets.json"))
    goal{t{"region"}} = t{"target"}
NEXT
PRINT goal
PRINT goal{"South"}
PRINT MAP.EXISTS(goal, "West"); " "; TYPEOF(goal{"West"})
PRINT goal{"West"} ?? 0
```

```text
{"North": 40, "South": 60, "East": 30}
60
FALSE NONE
0
```

`MAP.KEYS`, `MAP.VALUES` and `MAP.ITEMS` turn a map back into arrays; see
[Map Functions](../languages.md#map-functions) and
[`??`](../languages.md#-the-left-side-unless-it-is-absent).

## 7. Write the results

### CSV

`CSVWRITER file$, table, delimiter$, header` writes a 2D array, with an
optional header row.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM totals = AGG(SLICE(rows, 1, 1), SLICE(rows, 1, 3) * SLICE(rows, 1, 4), LAMBDA g -> SUM(g))

CSVWRITER "region_totals.csv", totals, ",", ["region", "total"]
PRINT TXTREADER$("region_totals.csv")
```

```text
region,total
North,42.5
South,55
East,37.5
```

### A text report

`FORMAT$` formats several values at once with C++ `{}` specifiers: `{:<8}`
left-aligns in 8 columns, `{:>9.2f}` right-aligns a number with two decimals.
`LPAD$` and `RPAD$` pad one string to a width, and `REPEAT$` draws a rule. The
report joins the totals with the targets map from section 6.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM totals = AGG(SLICE(rows, 1, 1), SLICE(rows, 1, 3) * SLICE(rows, 1, 4), LAMBDA g -> SUM(g))
DIM goal AS MAP
FOR EACH t IN JSON.PARSE$(TXTREADER$("targets.json"))
    goal{t{"region"}} = t{"target"}
NEXT

DIM nl$ = CHR$(10)
DIM report$ = RPAD$("Region", 8) + LPAD$("Total", 9) + LPAD$("Target", 8) + LPAD$("Done", 7) + nl$
report$ = report$ + REPEAT$("-", 32) + nl$
FOR EACH r IN totals
    report$ = report$ + FORMAT$("{:<8}{:>9.2f}{:>8}{:>6.0f}%", r[0], r[1], goal{r[0]}, 100 * r[1] / goal{r[0]}) + nl$
NEXT
report$ = report$ + REPEAT$("-", 32) + nl$
report$ = report$ + RPAD$("All", 8) + LPAD$(FORMAT$("{:.2f}", SUM(SLICE(totals, 1, 1))), 9) + nl$
TXTWRITER "report.txt", report$
PRINT TXTREADER$("report.txt")
```

```text
Region      Total  Target   Done
--------------------------------
North       42.50      40   106%
South       55.00      60    92%
East        37.50      30   125%
--------------------------------
All        135.00
```

### JSON

Build the result as maps and arrays, then `JSON.STRINGIFY$` it.

```basic
DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM totals = AGG(SLICE(rows, 1, 1), SLICE(rows, 1, 3) * SLICE(rows, 1, 4), LAMBDA g -> SUM(g))

DIM summary AS MAP
summary{"period"} = "2026-Q1"
summary{"regions"} = SELECT(LAMBDA r -> {"region": r[0], "total": r[1]}, totals)
TXTWRITER "summary.json", JSON.STRINGIFY$(summary)
PRINT TXTREADER$("summary.json")
```

```text
{"period":"2026-Q1","regions":[{"region":"North","total":42.5},{"region":"South","total":55},{"region":"East","total":37.5}]}
```

## 8. Larger tools

### Data frames: the DF module

[DF](../../lib/df_lib_readme.md) keeps a table as named columns and covers the
everyday pandas subset: computed columns, filters, sorting, group by with
aggregates, joins, pivots and console tables. The whole sales summary is four
calls.

```basic
IMPORT DF

DIM sales = DF.READCSV("sales.csv")
sales = DF.WITH(sales, "amount", DF.COL(sales, "qty") * DF.COL(sales, "price"))
DIM by_region = DF.GROUPBY(sales, "region", {"total": ["sum", "amount"], "orders": ["count"]})
DF.SHOW(DF.SORT(by_region, "total", TRUE))
```

It prints:

```
+--------+-------+--------+
| region | total | orders |
+--------+-------+--------+
| South  |    55 |      2 |
| North  |  42.5 |      3 |
| East   |  37.5 |      1 |
+--------+-------+--------+
```

### Excel files: the XLSX module

[XLSX](../../lib/xlsx_lib_readme.md) writes `.xlsx` workbooks from 2D arrays
and reads them back, without Excel. `MVINS` puts the header row on top of the
data.

```basic
IMPORT XLSX

DIM rows = CSVREADER("sales.csv", ",", TRUE)
DIM wb = XLSX.NEW()
DIM sheet = XLSX.SHEET(wb, "Sales", MVINS(rows, 0, 0, CSVHEADER("sales.csv")))
XLSX.FREEZE(sheet, 1)
XLSX.WRITE("sales.xlsx", wb)

DIM book = XLSX.READ("sales.xlsx")
PRINT book{"Sales"}[1]
```

`READ` answers values, so reading a workbook someone else designed, changing a
number and writing it back with `WRITE` would keep the numbers and drop the
formulas, fills and column widths - the new file was never told about them.
To change cells in a workbook that exists, open it instead:

```basic
DIM h = XLSX.EDIT("report.xlsx")
XLSX.SETCELL(h, "Sales", "B2", 99)
XLSX.SAVEAS(h, "report.xlsx")
```

Everything the module does not touch travels unchanged, including the sheets
it never looked at.

### SQLite

A build with the `SQLITE` flag has the `SQL.*` functions; `OS.FEATURE("SQLITE")`
tells you whether yours does. `SQL.TABLE` returns rows as arrays and
`SQL.COLUMNS` the column names; both also work in a compiled program.
`SQL.QUERY` returns one map per row and runs in the interpreter only. The
[DB module](../../lib/db_lib_readme.md) builds the SQL for you. Details are in
[SQLite](../languages.md#sqlite-build-flag-sqlite).

```basic
DIM db = SQL.OPEN("sales.db")
SQL.EXEC(db, "CREATE TABLE IF NOT EXISTS sales(day TEXT, region TEXT, item TEXT, qty INTEGER, price REAL)")
FOR EACH r IN CSVREADER("sales.csv", ",", TRUE)
    SQL.EXEC(db, FORMAT$("INSERT INTO sales VALUES ('{}', '{}', '{}', {}, {})", r[0], r[1], r[2], r[3], r[4]))
NEXT
DIM sql$ = "SELECT region, SUM(qty * price) AS total FROM sales GROUP BY region ORDER BY total DESC"
PRINT SQL.COLUMNS(db, sql$)
PRINT SQL.TABLE(db, sql$)
DIM recs = SQL.QUERY(db, sql$)
PRINT recs[0]{"region"}
SQL.CLOSE(db)
```

### Clean up

`KILL` deletes a file. This removes everything the examples wrote.

```basic
FOR EACH f$ IN ["sales.csv", "targets.json", "region_totals.csv", "report.txt", "summary.json", "sales.xlsx", "sales.db"]
    IF FILE.EXISTS(f$) THEN KILL f$
NEXT
PRINT FILE.EXISTS("sales.csv")
```

```text
FALSE
```

## Where next

- [Vectors, matrices and data](../howto-vector-matrix-data.md): the array
  idioms behind this guide, with reshaping, running totals and grid rendering.
- [APL-style array programming](../APL_pipeline.md): broadcasting and
  whole-array updates in depth.
- [Python idioms](../idioms-from-python.md): a lookup table from Python to
  jdBasic for strings, lists, dicts, files and JSON.
- [Module library](../../lib/README.md): DF, XLSX, DB, DT (dates), CONSOLE
  (tables) and the rest.
- [Array & Matrix Functions](../languages.md#array--matrix-functions) and
  [File I/O Functions](../languages.md#file-io-functions) in the language
  reference.
