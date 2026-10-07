# XLSX: Excel workbooks without Excel

`lib/xlsx.jdb` writes `.xlsx` files with a bold header row, column
widths, number formats, cell fills, formulas, booleans and frozen
panes, and reads any `.xlsx` back as typed 2D arrays. The archive goes through
`ZIP.WRITE` and `ZIP.READ`; no Excel and no other program is involved.

Stands in for: openpyxl.

## Quick start

```basic
IMPORT XLSX

DIM wb = XLSX.NEW()
DIM s = XLSX.SHEET(wb, "Sales", [["Item", "Qty", "Price"], ["Bolt", 40, 0.15], ["Nut", 12, 0.05], ["Total", "=SUM(B2:B3)", ""]])
XLSX.COLUMN(s, "A", {"width": 18})
XLSX.COLUMN(s, "C", {"width": 10, "format": "#,##0.00"})
XLSX.STYLE(s, "A4:C4", {"fill": "DDEBF7", "bold": TRUE, "format": "#,##0.00"})
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
| `STYLE(sheet, ref, opts)` | `ref` is a cell (`"B7"`) or a range (`"A7:H7"`); `opts` carries `fill` (`RRGGBB`, `AARRGGBB`, a leading `#` allowed), `bold` and `format`, which beats the column's. |
| `FREEZE(sheet, rows, [cols])` | Keeps the first rows and columns in view. |
| `WRITE(path$, wb)` | Writes the file; returns the number of archive parts. |
| `READ(path$)` | Every sheet as a 2D array, keyed by sheet name. |
| `SHEETS(path$)` | The sheet names in workbook order. |
| `EDIT(path$)` | Opens a workbook that exists, for changing single cells. See **Editing**. |
| `SETCELL(h, sheet$, ref$, value)` | Writes one cell of an opened workbook. |
| `SAVEAS(h, path$)` | Writes the opened workbook back; returns the number of archive parts. |
| `COL_INDEX(letters$)` / `COL_LETTERS$(index)` | `"AA"` is 27 and back. |

## Editing

`READ` answers values and `WRITE` builds an archive from scratch, so reading a
workbook, changing a number and writing it back keeps the numbers and nothing
else: formulas, fills, column widths, charts and every sheet the caller did
not rebuild are gone, because the new archive was never told about them.

`EDIT` keeps the archive instead and patches only the cells asked for, so the
parts it never looks at travel unchanged.

```basic
DIM h = XLSX.EDIT("report.xlsx")
XLSX.SETCELL(h, "Sales", "B2", 99)
XLSX.SETCELL(h, "Sales", "D2", "=B2*C2")
XLSX.SETCELL(h, "Sales", "A9", "a new row")
XLSX.SAVEAS(h, "report.xlsx")
```

A number, a boolean and `NONE` write as themselves; text writes as an inline
string, which leaves `sharedStrings.xml` and its counts alone; text that
starts with `=` writes a formula. A cell keeps its own style, so a value set
into a filled, formatted cell still looks the way the sheet was designed.
Setting a value on a formula cell drops the formula - that is what asking for
a literal there means. A cell or a row that is not in the sheet yet is
created in order, which Excel requires. A sheet name that is not in the
workbook throws.

What `EDIT` does not do: add or remove sheets, change a style, or recalculate
anything. A formula cell it writes carries no cached value, so a reader that
does not calculate shows it empty until Excel opens the file.

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
  widths, the number format, the fills and the frozen pane. A workbook written by
  openpyxl reads back as expected and is kept as a fixture under
  `tests/jdlibs/fixtures/`.
- Styles are minimal: one bold font, one number format per distinct
  code, one solid fill per distinct colour, and one `cellXfs` record per
  distinct combination of the three. A number format set on a cell wins
  over the one its column carries. Borders, merged cells and charts
  are not written, and every style is ignored on read.

## Tests and demos

- `tests/jdlibs/xlsx_selftest.jdb`
- `jdb/demos/jdlibs/xlsx_demo.jdb` (an inventory workbook with three sheets, read back and printed)
- `jdb/demos/jdlibs/service_report.jdb` (with CONF, LOGGER and JWT)
