# XLSX - Excel workbooks without Excel

`lib/xlsx.jdb` writes `.xlsx` files with a bold header row, column
widths, number formats, formulas, booleans and frozen panes, and reads
any `.xlsx` back as typed 2D arrays. The archive goes through
`ZIP.WRITE` and `ZIP.READ`; no Excel and no other program is involved.

Stands in for: openpyxl.

## Quick start

```basic
IMPORT XLSX

DIM wb = XLSX.NEW()
DIM s = XLSX.SHEET(wb, "Sales", [["Item", "Qty", "Price"], ["Bolt", 40, 0.15], ["Nut", 12, 0.05], ["Total", "=SUM(B2:B3)", ""]])
XLSX.COLUMN(s, "A", {"width": 18})
XLSX.COLUMN(s, "C", {"width": 10, "format": "#,##0.00"})
XLSX.FREEZE(s, 1)
XLSX.WRITE("sales.xlsx", wb)

DIM book = XLSX.READ("sales.xlsx")        ' {"Sales": [[...], ...]}
PRINT book{"Sales"}[1][2]
PRINT JOIN(XLSX.SHEETS("sales.xlsx"), ", ")
```

## API

| Call | What it does |
|------|--------------|
| `NEW()` | An empty workbook. |
| `SHEET(wb, name$, rows, [header])` | Adds a sheet from a 2D array; the first row is bold unless `header` is `FALSE`. Returns the sheet for `COLUMN` and `FREEZE`. |
| `COLUMN(sheet, col, opts)` | `col` is a letter or a 1-based index; `opts` carries `width` (characters) and `format` (an Excel number format code). |
| `FREEZE(sheet, rows, [cols])` | Keeps the first rows and columns in view. |
| `WRITE(path$, wb)` | Writes the file; returns the number of archive parts. |
| `READ(path$)` | Every sheet as a 2D array, keyed by sheet name. |
| `SHEETS(path$)` | The sheet names in workbook order. |
| `COL_INDEX(letters$)` / `COL_LETTERS$(index)` | `"AA"` is 27 and back. |

## Cells

Writing: a string goes to the shared strings table, a number is a
number, a boolean a boolean, a string starting with `=` is a formula,
`NONE` leaves the cell empty.

Reading: numbers and booleans come back typed, strings unescaped,
rows are padded to the widest row, a row the writer skipped is an empty
row, a sparse row keeps its columns. A formula cell yields the value
Excel cached; a file written by this module has none, so those cells
read back empty. Dates arrive as the serial number Excel stores (days
since 1899-12-30).

## Notes

- The archive uses stored entries, which every reader accepts; the file
  is as large as its XML. Compression is a separate ticket on the ZIP
  builtin.
- openpyxl opens what the module writes and reports the bold font, the
  widths, the number format and the frozen pane. A workbook written by
  openpyxl reads back as expected and is kept as a fixture under
  `tests/jdlibs/fixtures/`.
- Styles are minimal: one bold font, one number format per distinct
  code. Fills, borders, merged cells and charts are not written, and are
  ignored on read.

## Tests and demos

- `tests/jdlibs/xlsx_selftest.jdb`
- `jdb/demos/jdlibs/xlsx_demo.jdb` (an inventory workbook with three sheets, read back and printed)
- `jdb/demos/jdlibs/service_report.jdb` (with CONF, LOGGER and JWT)
