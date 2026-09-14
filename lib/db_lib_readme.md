# DB - SQLite without hand-written SQL strings

`lib/db.jdb` sits on top of `SQL.*`: a query builder whose values always
become quoted literals, rows as maps, inserts, upserts, updates and
deletes from maps, tables created from a column map or an example row,
transactions, and numbered migrations recorded in a version table. It is
built on `SQL.EXEC`, `SQL.TABLE` and `SQL.COLUMNS`, so it runs compiled
with `-c`. Needs a build with the `SQLITE` flag.

Stands in for: SQLAlchemy Core (the query builder), dataset (rows from
maps, tables from rows), the migration part of Alembic.

## Quick start

```basic
IMPORT DB

DIM h = DB.OPEN("shop.db")
DIM applied = DB.MIGRATE(h, "migrations")

DIM id = DB.INSERT(h, "customers", {"name": "Ada", "city": "London"})
DIM moved = DB.UPSERT(h, "customers", {"name": "Ada", "city": "Oslo"}, "name")

DIM q = DB.FROM("customers c")
DB.SELECT(q, "c.name, total(o.amount) AS spent")
DB.LEFTJOIN(q, "orders o", "o.customer_id = c.id")
DB.WHERE(q, "c.city = ? AND o.placed >= ?", ["Oslo", "2026-09-01"])
DB.GROUPBY(q, "c.id")
DB.ORDERBY(q, "spent DESC")
DB.LIMIT(q, 10)

DIM found = DB.ROWS(h, q)
DIM i = 0
FOR i = 0 TO LEN(found) - 1
    DIM rec = found[i]
    PRINT rec{"name"}; " "; rec{"spent"}
NEXT i
DB.CLOSE(h)
```

## Values and names

Every `?` in a condition or a `STMT` is replaced by the next value of the
array that follows it, written as an SQL literal. A `?` inside quoted text
(`'?'`) or a quoted name (`"a?"`) stays as it is. The number of `?` and
values must match, or the call throws.

| Value | Literal |
|-------|---------|
| text | `'...'` with every `'` doubled; text holding `CHR$(0)` is refused |
| number | the shortest digits (15 to 17 significant) that read back as the same double: `0.1`, `0.1234567`, `6.02214076e23`; whole numbers below 2^53 as digits; a negative number in parentheses, so `5 -?` never turns into a comment |
| `NONE` | `NULL` |
| `TRUE` / `FALSE` | `1` / `0` |

Table and column names that `INSERT`, `INSERTMANY`, `UPSERT`, `UPDATE`,
`DELETE`, `CREATE` and `CREATELIKE` take are quoted as names (`"..."`
with every `"` doubled), so a map key never becomes SQL. The clauses of
the builder (`FROM`, `SELECT`, `JOIN`, `GROUPBY`, `ORDERBY` and the text
of a condition) are SQL written by the program: only the values are
bound.

| Call | What it does |
|------|--------------|
| `QUOTE$(v)` | The literal of a value. |
| `IDENT$(name$)` | A quoted name, e.g. for `FROM(DB.IDENT$("odd table"))`. |

## Connection and statements

| Call | What it does |
|------|--------------|
| `OPEN(path$)` | Opens or creates a database file (`":memory:"` for one in memory) and answers the handle; throws when it cannot. The handle works with `SQL.*` too. |
| `CLOSE(h)` | Closes it. |
| `EXEC(h, sql$, values)` | Runs statements with bound values and answers the rows changed; throws the SQLite message on an error. |
| `BEGIN(h)` / `COMMIT(h)` / `ROLLBACK(h)` | A transaction. `ROLLBACK` outside one does nothing. |

## Query builder

A query is a map. The builder calls change it in place and may come in
any order; `WHERE` and `HAVING` add conditions joined with `AND`.

| Call | What it does |
|------|--------------|
| `FROM(table$)` | A new query over a table, with an alias if wanted (`"customers c"`); selects `*`. |
| `STMT(sql$, values)` | A query from a whole SELECT with bound values; a trailing `;` is dropped. It takes no further clauses. |
| `SELECT(q, cols$)` | The select list. |
| `JOIN(q, table$, on$)` / `LEFTJOIN(q, table$, on$)` | An inner or left join. |
| `WHERE(q, cond$, values)` | A condition with bound values. |
| `GROUPBY(q, cols$)` / `HAVING(q, cond$, values)` | Grouping. |
| `ORDERBY(q, cols$)` | The order, e.g. `"spent DESC, name"`. |
| `LIMIT(q, n)` / `OFFSET(q, n)` | Paging. |
| `SQL$(q)` | The SQL text of the query. |

## Reading

| Call | What it does |
|------|--------------|
| `ROWS(h, q)` | One map per row keyed by column name: text as text, integers and reals as numbers. A NULL leaves its column out of the map, so `MAP.EXISTS(rec, col$)` tells NULL from 0. |
| `FIRST(h, q)` | The first row as a map, or an empty map. |
| `VALUE(h, q)` | The first cell of the first row, `NONE` for no row or a NULL. |
| `COUNT(h, q)` | How many rows the query yields. |
| `MATRIX(h, q)` | The `SQL.TABLE` grid of the query. |
| `COLUMNS(h, q)` | The column names. |
| `TABLES(h)` / `HASTABLE(h, table$)` | The tables of the database. |

Give duplicate column names of a join an alias (`a.id AS left_id`): a
row map holds one value per name.

## Writing

| Call | What it does |
|------|--------------|
| `INSERT(h, table$, rec)` | Inserts the columns of a map and answers the new row id. |
| `INSERTMANY(h, table$, cols, rows)` | Inserts a table of rows (`[["Ada", 36], ["Bob", 41]]`, one column per name in `cols`) in one savepoint: either every row lands or none. Answers the count. |
| `UPSERT(h, table$, rec, key_cols$)` | Inserts the map, or when a row with the same key columns (comma separated, backed by a primary key or unique index) exists, updates its other columns from the map. Answers the rows changed; with only key columns a conflict is left alone. |
| `UPDATE(h, table$, set_map, cond$, values)` | Sets the columns of the map in the rows the condition picks; answers the rows changed. |
| `DELETE(h, table$, cond$, values)` | Removes the rows the condition picks; answers the rows removed. |
| `CREATE(h, table$, col_types)` | Creates the table unless it exists: `{"id": "INTEGER PRIMARY KEY", "name": "TEXT NOT NULL"}`. |
| `CREATELIKE(h, table$, rec)` | Creates the table unless it exists from an example row: text as `TEXT`, whole numbers as `INTEGER`, other numbers as `REAL`, and `id INTEGER PRIMARY KEY` first when the row has no `id`. |

`UPDATE` and `DELETE` refuse an empty condition; pass `"1"` to mean every
row.

## Migrations

`MIGRATE(h, dir$)` applies the files `NNN_name.sql` of a directory whose
number is above the database version, in number order. Each file runs in
its own savepoint together with its row in `db_migrations` (`version`,
`name`, `applied_at`), so a failing file leaves nothing behind and throws
the SQLite message; the files before it stay applied. A second call
answers 0.

| Call | What it does |
|------|--------------|
| `MIGRATE(h, dir$)` | Applies the new files and answers how many ran. |
| `MIGRATESQL(h, steps)` | The same for an array of SQL texts: step `k` (0-based) is version `k + 1`. For a program that carries its schema. |
| `VERSION(h)` | The highest applied version, 0 for a new database. |

- Files without a leading number are ignored; two files with the same
  number are refused.
- A file whose number is not above the version but that never ran (added
  after a later one was applied) is refused rather than skipped.
- A file may hold several statements, comments and text with `;` in it.
  It must not hold `BEGIN` or `COMMIT` itself.
- The version row is written before the file runs, so a second process
  applying the same step at the same time fails on it and rolls back.

## Notes

- Compiled with `-c`, integer cells come back as FLOAT64 numbers; compare
  them as numbers or use `CINT`. Rows are read through SQLite's `typeof`,
  so text, numbers and NULL arrive alike in both backends.
- Compiled with `-c`, a row map taken out of the array `ROWS` answers
  reads by column name, but `MAP.KEYS` on it in the program is empty
  (trakr #417); use `COLUMNS` for the names. Handed to `INSERT` it is
  copied as a whole.
- A self test shows that hostile text (`'); DROP TABLE ...; --`, quotes,
  backslashes, `?`, newlines, UTF-8, `ATTACH DATABASE`) reads back as the
  value it was, with no row added, no table dropped and no database
  attached.

Self test: `tests/jdlibs/db_selftest.jdb`. Demo: `jdb/demos/jdlibs/db_demo.jdb`.
