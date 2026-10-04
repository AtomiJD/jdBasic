<!-- pagebreak -->

## M11 Data Cleaner

### The chore

Every Monday Jonas gets the order list of the week as an export from
the shop system, and every Monday it needs half an hour of repair
before anyone can work with it. The dates come in three forms, because
some orders were typed in by hand. Amounts arrive as text with a
decimal comma, so Excel will not add them up. The same customer is
written as *Müller GmbH* in one row and *Mueller GmbH* in the next. A
few rows appear twice, and every few weeks an IBAN has a typo that
nobody sees until the bank sends the payment back.

### What you get

The Data Cleaner reads the export, a CSV file or a workbook, and
repairs what it can:

- dates in any of the usual forms become ISO dates (`2026-10-05`);
- amounts with a decimal comma or point, thousands separators, a
  currency sign or a minus behind become real numbers;
- spaces are tidied, IBANs are written in groups of four, mail
  addresses in small letters;
- rows that appear twice are dropped.

What it cannot repair it lists: a date that does not exist, an IBAN
whose check digits are wrong, a mail address without a domain. It also
lists customer names that look like the same customer written twice.
Everything lands in one workbook with three sheets: *Clean*, the
repaired table, *Problems*, one row per cell to look at, and *Similar
names*.

### Before you start

Save one export in your work folder and open it in a text editor. Two
things matter. The first is the separator between the fields: a
semicolon in most European exports, a comma in American ones. The
second is the order of day and month in dates such as `05/10/2026`.
The wizard asks for both, and for the column that holds the customer
names.

### The program

The program reads the settings, decides the kind of each column, and
prints what the module found:

<!-- include recipes/medium/M11_data_cleaner/data_cleaner.jdb -->

The module does the cleaning, one cell at a time, and writes the
workbook:

<!-- include recipes/medium/M11_data_cleaner/cleaner.jdb -->

### How it works

1. **Reading the export.** `READ_TABLE` reads a workbook with
   `XLSX.READ` and a CSV file with its own small parser,
   `CSV_FIELDS`. The parser exists because of one common case: a
   customer called *Weber; Sohn & Co* in a file separated by
   semicolons. The export puts such a field in quotes, and a plain
   split at every semicolon would cut the name in two. `CSV_FIELDS`
   walks the line character by character and remembers whether it is
   inside quotes. Two quotes in a row inside a quoted field stand for
   one quote character. The CSV reader built into jdBasic does not
   handle quoted separators yet, which is why the recipe brings its
   own.

2. **One kind per column.** `GUESS_KINDS` looks at the column names:
   a name with *dat* in it is a date, *Amount* or *Betrag* a number,
   and so on. A `[data_cleaner.columns]` table in `work.conf` can
   overrule the guess for any column. Every column the cleaner does
   not know stays text, which means its spaces are tidied and nothing
   else.

3. **Dates.** `ISO_DATE$` tries four regular expressions, one per
   form. ISO dates and dates with dots are clear: the dot is the
   German form, always day first. A slash is the hard case. *05/10/2026*
   is the 5th of October in Europe and the 10th of May in the United
   States, and no program can tell from the text alone. That is the
   `day_first` setting. Month names are compared by their first three
   letters, so *Oct*, *Oct.* and *October* all work. `Ymd$` finally
   asks `DT.DAYS_IN` how many days the month has, which is how the
   31st of February is caught. A workbook can also hold a date as a
   number, the days since the 30th of December 1899; `ISO_DATE$`
   turns that into a date too.

4. **Numbers.** `NUMBER` removes spaces and currency signs, turns
   brackets and a minus behind into a minus, and then decides which
   separator is the decimal one. With `decimal = "auto"` the last
   separator wins: in *1.234,50* the comma comes last, so it is the
   decimal comma and the point separates thousands. Before and after
   that step a regular expression checks the shape of the text. This
   check matters, since `VAL("12abc")` answers 12 without a complaint,
   and a cleaner that turns *12abc* into 12 hides the problem it
   should report.

5. **Problems instead of guesses.** `CELL` answers a map with the
   clean value and a problem text. When the problem text is not empty,
   the cell keeps its old value and `CLEAN` adds a row to the problem
   list: the row number as Excel shows it, the column, the value and
   the reason. The cleaner never invents a value. A wrong IBAN stays
   wrong, but now it is on a list.

6. **Repeated rows.** `CLEAN` builds a key from the clean values of
   each row and keeps a map of the keys it has seen. A row whose key
   is already in the map is dropped. Comparing the clean values is the
   point: *Müller GmbH* with *1.234,50* and *Müller GmbH* with
   *1234.50* are the same row once both are cleaned.

7. **Names that look alike.** `SIMILAR` takes the cells of the name
   column with a `SELECT`, drops the empty ones with a `FILTER` and
   keeps each name once with `UNIQUE`. Then it compares every name
   with every other one using `FUZZY.SCORE` from the fuzzy library.
   The method `token_sort` sorts the words first, so *GmbH Müller* and
   *Müller GmbH* score 100. *Müller GmbH* and *Mueller GmbH* score 87,
   above the cutoff of 85. The recipe only lists such pairs; deciding
   whether they are one customer is your job.

### Run it

With the example export of the recipe's folder:

```
jdbasic data_cleaner.jdb
Rows read: 5
Cells changed: 11
Repeated rows dropped: 1
Problems: 3
  row 4, IBAN: not a valid IBAN (DE00 1234)
  row 4, Mail: not a mail address (weber@example)
  row 6, Date: not a date (31.02.2026)
Names that look alike: 1
  Müller GmbH / Mueller GmbH (87)
Clean workbook: C:/Data/orders_clean.xlsx
```

The clean workbook is written next to the export, with `_clean` added
to its name. With `--dry-run` the recipe prints the summary and writes
nothing.

### Schedule it

The wizard plans the cleaner for Monday at 7:30. Point the export of
the shop system at the same file name every week, and the clean
workbook is ready before the first coffee. If the export arrives at
other times, let the Hot Folder recipe (M10) start the cleaner whenever
a new file lands.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[data_cleaner]`:

```toml
[data_cleaner]
input = "~/Documents/AutomateWork/export.csv"
separator = ";"
day_first = true
name_column = "Customer"
similar = 85

[data_cleaner.columns]
Ordered = "date"
Net = "number"
"Customer no" = "keep"
```

The `columns` table names the kind of a column when the guess from its
name is wrong. `keep` leaves a column exactly as it is, which is right
for article numbers with leading zeros.

The interesting changes are in the code:

- **A different date form.** Some systems write `20261005`. Add one
  more test to `ISO_DATE$`, right before its final `RETURN ""`:

  ```basic
  g = REGEX.FINDALL("^([0-9]{4})([0-9]{2})([0-9]{2})$", s$)
  IF LEN(g) > 0 THEN
      RETURN Ymd$(VAL(g[0][0]), VAL(g[0][1]), VAL(g[0][2]))
  ENDIF
  ```

- **A new kind of column.** A column of German postcodes can get its
  own kind. Add a block to `CELL` before the last line:

  ```basic
  IF kind$ = "postcode" THEN
      IF LEN(REGEX.FINDALL("^[0-9]{5}$", t$)) = 0 THEN
          RETURN Answer(t$, "not a postcode")
      ENDIF
  ENDIF
  ```

  and name the column in `[data_cleaner.columns]` as
  `PLZ = "postcode"`.

- **Repeated rows by a key column.** Two rows with the same order
  number are the same order, even if the amount was corrected. In
  `CLEAN`, replace the line that adds to `key$` with
  `IF col$ = "Order" THEN key$ = now$`. The key is then the order
  number alone, and the second row of each order drops out.

### When it goes wrong

- **Every row is one long field**: the separator is wrong. Open the
  file in a text editor and set `separator` to the character between
  the fields.
- **Dates are off by months**: `day_first` does not match the export.
  Look at a date with a day above 12; if *13/10* turns up as a problem,
  switch the setting.
- **Too many similar names**: raise `similar` to 90. Names like *Weber
  AG* and *Weber KG* are different companies that differ in one
  letter, and a lower cutoff will pair them.
- **Leading zeros are gone**: the column was guessed as a number. Set
  it to `keep` in `[data_cleaner.columns]`.

> **Balance dividend**
> About 30 minutes of repair work every Monday, and the bank's returned
> payment found on Monday morning instead of two weeks later.
