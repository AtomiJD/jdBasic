# PDFGEN - PDF documents written from jdBasic

`lib/pdfgen.jdb` writes PDF files without any other program: pages in the
common sizes, the 14 standard fonts with their real glyph widths, text at
a position, cells and wrapped paragraphs that start a new page when the
page is full, lines and boxes, JPEG images, tables whose header row
repeats on every page, a footer with page numbers, and the document
properties.

Positions and sizes are millimetres, measured from the top left corner of
the page. Text is UTF-8 in jdBasic and is written in WinAnsi, the encoding
the standard fonts carry, so umlauts, the euro sign and typographic quotes
come out right.

Stands in for: fpdf2, reportlab.

## Quick start

```basic
IMPORT PDFGEN

DIM pdf = PDFGEN.DOC()
PDFGEN.SETINFO(pdf, "Rechnung RE-2026-0413", "Papier & Toner GmbH")
PDFGEN.ADDPAGE(pdf)
PDFGEN.USEFONT(pdf, "helvetica", "", 8)
PDFGEN.SETFOOTER(pdf, "Seite {page} von {pages}", "R")

PDFGEN.IMAGE(pdf, "logo.jpg", 20, 15, 25)
PDFGEN.USEFONT(pdf, "helvetica", "B", 18)
PDFGEN.TEXTAT(pdf, 50, 25, "Rechnung RE-2026-0413")

PDFGEN.USEFONT(pdf, "helvetica", "", 10)
PDFGEN.SETXY(pdf, 20, 45)
PDFGEN.MULTICELL(pdf, 0, 5, "Vielen Dank für Ihren Auftrag. Zahlbar innerhalb von 14 Tagen.")
PDFGEN.LN(pdf, 4)

DIM rows = [["1", "Papier A4, 500 Blatt", "10", "45,00 €"], ["2", "Toner", "2", "123,80 €"]]
PDFGEN.TABLE(pdf, rows, {"headers": ["Pos.", "Artikel", "Menge", "Betrag"], "widths": [15, 0, 20, 30], "align": ["R", "L", "R", "R"]})

PDFGEN.WRITEFILE(pdf, "rechnung.pdf")
```

## API

### The document and its pages

| Call | What it does |
|------|--------------|
| `DOC([opts])` | A new document. `opts`: `"size"` (`A4`, `A5`, `A3`, `Letter`, `Legal`; A4 by default), `"landscape"` (FALSE), `"margin"` in mm (20). |
| `ADDPAGE(doc)` | Starts a page and puts the position at the top left margin. Drawing on a document without a page starts one. |
| `PAGENO(doc)` / `PAGECOUNT(doc)` | The page being written, from 1, and the number of pages. |
| `PAGEWIDTH(doc)` / `PAGEHEIGHT(doc)` | The page size in mm. |
| `POSX(doc)` / `POSY(doc)` / `SETXY(doc, x, y)` | The current position, which cells and paragraphs move. |
| `LN(doc, h)` | Back to the left margin, `h` mm further down. |
| `SETINFO(doc, title$, [author$], [subject$])` | The document properties. |
| `SETFOOTER(doc, text$, [align$])` | A line at the bottom of every page in the font current at the call, aligned `"L"`, `"C"` or `"R"`. `{page}` and `{pages}` become the page number and the page count. |

### Fonts and colours

| Call | What it does |
|------|--------------|
| `USEFONT(doc, family$, [style$], [size])` | `helvetica` (also `arial`, `sans`), `times` (`serif`), `courier` (`mono`), `symbol`, `zapfdingbats`; style `""`, `"B"`, `"I"` or `"BI"`; size in points when above 0. |
| `STRWIDTH(doc, text$)` | The width of a text in the current font, in mm, from the font's glyph widths. |
| `SETCOLOR(doc, r, g, b)` | The colour of text, 0 to 255. |
| `SETFILL(doc, r, g, b)` | The fill colour of cells, boxes and table rows; a light grey by default. |
| `SETDRAW(doc, r, g, b)` / `SETLINEWIDTH(doc, mm)` | The colour and width of lines and frames. |

### Text

| Call | What it does |
|------|--------------|
| `TEXTAT(doc, x, y, text$)` | A text with its baseline at `x`, `y`. `{page}` and `{pages}` are replaced. |
| `CELL(doc, w, h, text$, [align$], [border], [fill])` | A cell at the current position, the text on one line aligned `"L"`, `"C"` or `"R"`, with a frame when `border` is not 0 and filled when `fill` is not 0. A width of 0 reaches the right margin. The position moves to the right of the cell; a cell that does not fit above the bottom margin starts a new page. |
| `MULTICELL(doc, w, lineh, text$, [align$])` | A paragraph wrapped into lines of `lineh` mm, each line a cell, new pages as needed. The position ends below it, at its left edge. |
| `SPLITLINES(doc, text$, w)` | The lines a text breaks into at `w` mm: at spaces, at every line break, inside a word longer than a line. |
| `FIT$(doc, text$, w)` | The text cut to `w` mm with `...` when it is too long. |
| `WINANSI$(text$)` | A UTF-8 text as the WinAnsi bytes written into the file. |

A character WinAnsi has no place for becomes `?`; a line break or a tab
inside a one-line text becomes a space. Symbol and ZapfDingbats take their
bytes as given, in their own encodings.

### Lines, boxes and images

| Call | What it does |
|------|--------------|
| `DRAWLINE(doc, x1, y1, x2, y2)` | A line. |
| `BOX(doc, x, y, w, h, [style$])` | A rectangle: `"D"` the frame, `"F"` filled, `"DF"` both. |
| `IMAGE(doc, path$, x, y, [w], [h])` | A JPEG with its top left corner at `x`, `y`. With `w` or `h` at 0 the other follows from the picture's proportions; with both at 0 it is drawn at 96 dots per inch. |

A JPEG is embedded without decoding: its size and colour components (grey,
RGB or CMYK) come from its frame header, and its bytes go into the file
as ASCII hex over the JPEG filter (`[/ASCIIHexDecode /DCTDecode]`), which
every viewer reads. That doubles the space the picture takes and keeps
the document free of binary bytes, so interpreted and compiled programs
write the same file. A file drawn several times is stored once.

### Tables

| Call | What it does |
|------|--------------|
| `TABLE(doc, rows, [opts])` | Rows of cells from the current position down. `opts`: `"headers"` (an array), `"widths"` in mm per column (a 0 or a missing width shares out the rest), `"align"` per column, `"lineh"` (7 mm), `"zebra"` (every second row in the fill colour). |

Cells hold one line each and are cut with `...` to their column. When a
row no longer fits above the bottom margin the table continues on a new
page and draws its header row there again, in bold.

### Writing

| Call | What it does |
|------|--------------|
| `BUILD$(doc)` | The bytes of the PDF file. |
| `WRITEFILE(doc, path$)` | Writes them to a file. |

## Notes

- The file is PDF 1.4 with uncompressed content streams, so it is somewhat
  larger than a compressed one and every text can be found in it with
  `INSTR`. `PDF.TEXT$` reads it back.
- Only the standard fonts: no TrueType embedding, so no characters beyond
  WinAnsi. Only JPEG images: PNG needs decoding and is not supported.
- The widths come from the fonts' metrics, so `STRWIDTH` and the alignment
  of cells agree with what a viewer shows; they match fpdf2 to six decimal
  places.
- Nothing is kept between documents; a document is a map, and every call
  takes it.
- Everything works compiled with `-c`; `PDF.TEXT$` is interpreter-only.

Self test: `tests/jdlibs/pdfgen_selftest.jdb`.
Demo: `jdb/demos/jdlibs/pdfgen_demo.jdb`.
