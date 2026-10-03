# PPTX: write and read PowerPoint files

`lib/pptx.jdb` writes PowerPoint presentations without PowerPoint and
reads the text of their slides back. A `.pptx` file is a ZIP archive of
XML parts; the module writes those parts itself and packs them with
`ZIP.WRITE`, the way DOCX writes Word files.

Every slide uses one blank layout and draws its own text boxes,
pictures and tables, so a deck does not depend on a template. The
slides are 16:9 with a white background, titles in dark blue Calibri,
text in dark grey.

Stands in for: python-pptx (its common slide types).

## Quick start

```basic
IMPORT PPTX

DIM deck = PPTX.NEW("Quarterly Review")
DIM n = PPTX.TITLE_SLIDE(deck, "Quarterly Review", "Sales team, October")
n = PPTX.SLIDE(deck, "Highlights", ["Revenue up 8 %", "  in the north", "Two new customers"])
PPTX.IMAGE(deck, "chart.png")
PPTX.NOTES(deck, "Thank the team first.")
n = PPTX.SLIDE(deck, "By region", [])
PPTX.TABLE(deck, [["Region", "Q4"], ["North", "1.3 M"], ["South", "1.1 M"]])
PRINT PPTX.WRITE(deck, "review.pptx")

DIM back = PPTX.READ("review.pptx")
PRINT back[1]{"title"}; ": "; JOIN(back[1]{"bullets"}, ", ")
```

## API

| Call | What it does |
|------|--------------|
| `NEW([title$])` | An empty deck; the title goes into the file's properties. |
| `TITLE_SLIDE(deck, title$, [subtitle$])` | A title slide: a large title in the middle and a line below it. Answers the slide number. |
| `SLIDE(deck, title$, bullets)` | A slide with a title and bullet points; a point that starts with two blanks is indented one level. `bullets` may be empty for a slide that only holds a picture or a table. Answers the slide number. |
| `IMAGE(deck, path$)` | A PNG or JPEG on the last slide, fitted with its proportions kept: on the right half when the slide has bullet points, in the middle when it has none. Anything else is an error. |
| `TABLE(deck, rows, [header])` | A table on the last slide from rows of texts; with `header` (TRUE) the first row is white on dark blue. Below the bullet points when the slide has some. |
| `NOTES(deck, text$)` | Speaker notes for the last slide; line breaks start new paragraphs. |
| `COUNT_SLIDES(deck)` | The number of slides so far. |
| `WRITE(deck, path$)` | Writes the file; answers the number of slides. A deck without slides is an error. |
| `READ(path$)` | The slides of a file written by this module, as maps of `title`, `subtitle`, `bullets`, `table` (rows of texts), `pictures` (a count) and `notes`. |

`IMAGE`, `TABLE` and `NOTES` work on the slide added last; calling them
before the first slide is an error.

## Limits

- One layout, no slide masters of your own, no themes to choose from.
- No charts as PowerPoint objects; draw them with `SVG` or a picture
  and add the picture.
- `READ` reads the shapes this module writes; a deck made in PowerPoint
  gives its texts only where they sit in shapes named the same way.

## Tests

`tests/jdlibs/pptx_selftest.jdb` writes a deck with every kind of
slide, reads it back, checks that every XML part is well-formed and the
pictures and notes are in the package, and checks the errors. It runs
in the interpreter and compiled.
