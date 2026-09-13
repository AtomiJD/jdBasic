# DOCX - write, fill and read Word files

`lib/docx.jdb` writes Word documents without Word, fills the `{{name}}`
placeholders of a template, and reads the paragraphs and tables of a
document back. A `.docx` file is a ZIP archive of XML parts; the module
writes those parts itself, stores them with `ZIP.WRITE` the way XLSX does,
and reads them with the XML module.

Stands in for: python-docx, docxtpl.

## Quick start

```basic
IMPORT DOCX

DIM doc = DOCX.NEW()
DOCX.TITLE(doc, "Offer 2026-0413")
DOCX.HEADER(doc, "Paper & Toner Ltd")
DOCX.FOOTER(doc, "Page {page} of {pages}")
DOCX.HEADING(doc, "Offer 2026-0413")
DOCX.PARAGRAPH(doc, "We offer the items below for **209.80 EUR**, valid *until 31 October*.")
DOCX.TABLE(doc, [["Item", "Qty", "Amount"], ["Copy paper A4", "10", "45.90"]])
DOCX.BULLETS(doc, ["free delivery above 100 EUR", "within three working days"])
DOCX.NUMBERED(doc, ["check the offer", "order by e-mail"])
DOCX.WRITE(doc, "offer.docx")

PRINT JOIN(DOCX.PLACEHOLDERS("letter_template.docx"), ", ")        ' name, order, amount
PRINT DOCX.FILL("letter_template.docx", "letter.docx", {"name": "Ms Miller", "order": "B-0815", "amount": "209.80"})

DIM back = DOCX.READ("letter.docx")
PRINT JOIN(back{"paragraphs"}, CHR$(10))
```

## API

### Writing

| Call | What it does |
|------|--------------|
| `NEW()` | An empty document. |
| `HEADING(doc, text$, [level])` | A heading of level 1 (default), 2 or 3, in Word's built-in heading styles. |
| `PARAGRAPH(doc, text$, [align$])` | A paragraph. `**bold**` and `*italic*` in the text become formatted runs; a tab and a line break stay what they are. `align$` is `left`, `center`, `right` or `justify`. |
| `BULLETS(doc, items)` | A bullet list, one paragraph per item, with the same marks. |
| `NUMBERED(doc, items)` | A numbered list; every call starts again at 1. |
| `TABLE(doc, rows, [header])` | A table with grid lines from an array of rows of text; with `header` (TRUE) the first row is bold and repeats at the top of every page. Short rows are filled with empty cells. |
| `PAGEBREAK(doc)` | Starts a new page. |
| `HEADER(doc, text$)` / `FOOTER(doc, text$)` | The text at the top and the bottom of every page; `{page}` and `{pages}` become Word's page number fields. |
| `TITLE(doc, text$)` | The title in the document properties. |
| `WRITE(doc, path$)` | Writes the file; answers the number of parts (9, plus the header and the footer). |

The page is A4 with Word's default margins, the text Calibri 11 pt.

### Templates

| Call | What it does |
|------|--------------|
| `PLACEHOLDERS(path$)` | The `{{name}}` placeholders of a document, its headers and footers, each once. |
| `FILL(src_path$, dst_path$, values)` | A copy of the document with every placeholder that has a value in the map replaced, and escaped for XML; answers how many were replaced. Placeholders without a value stay. |

Word often splits what you typed as one placeholder into several runs, for
a spell check mark, a change of formatting or an edit made later. FILL
reads each paragraph across all its runs, so `{{ customer }}` is found
even as `{{ cust` in one run and `omer }}` in the next; the value takes the
formatting of the run the placeholder starts in. A name may hold letters,
digits, `_`, `.` and `-`, with spaces inside the braces allowed.

### Reading

| Call | What it does |
|------|--------------|
| `READ(path$)` | A map with `paragraphs`, the texts of the body's paragraphs in order, and `tables`, each table as an array of rows of cell texts. |
| `PARAGRAPHS(path$)` / `TABLES(path$)` | One of the two. |

A paragraph's text joins its runs; a tab and a line break come back as
`CHR$(9)` and `CHR$(10)`, a page break adds nothing, as in python-docx. Field codes and
deleted text are left out, the result of a field is kept. Paragraphs
inside a table belong to their cell, joined by line breaks; a content
control's paragraphs and tables count as the body's.

## Notes

- A written document reads back in python-docx with its styles, runs,
  list numbering, table, header, footer, alignment and title.
- Images, footnotes, comments and sections are not written; FILL keeps
  whatever a template already has, since it changes only the text of runs.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/docx_selftest.jdb`, with a Word-style file whose
placeholders are split across runs
(`tests/jdlibs/fixtures/docx_word_style.docx`).
Demo: `jdb/demos/jdlibs/docx_demo.jdb`.
