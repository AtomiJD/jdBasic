# MDSYNC: a Markdown source and its rendered view, kept in step

`lib/mdsync.jdb` binds a `FORM.RICHTEXT` to a Markdown source and keeps
the two in step both ways. An edit in the rendered view rewrites only the
blocks it reached and answers the span of the source that changed, so an
editor that shows the source next to it replaces just that span (its undo
and scroll position stay); an edit of the source redraws only the blocks
that changed. Every other byte of the file stays as it was. It is built
on [MDRICH](mdrich_lib_readme.md) and needs a FORMS build.

Stands in for: the source sync of WYSIWYG Markdown editors (Typora,
Obsidian's live preview, Milkdown).

## Quick start

```basic
IMPORT MDSYNC

DIM doc = MDSYNC.OPEN(view, md$, {"base": "docs"})

SUB RTVIEW_CHANGE(e)
    DIM d = MDSYNC.VIEWCHANGED(doc)
    IF d{"changed"} THEN
        FORM.SET(src_box, "SELSTART", d{"start"})
        FORM.SET(src_box, "SELLENGTH", d{"old_len"})
        FORM.SET(src_box, "SELTEXT", d{"text"})
    ENDIF
ENDSUB

SUB TXTSOURCE_CHANGE(e)
    DIM redrawn = MDSYNC.SOURCECHANGED(doc, FORM.GET(src_box, "TEXT"))
ENDSUB
```

Both calls are cheap when nothing changed, so each pane can answer the
other's echo without a guard.

## API

| Call | What it does |
|------|--------------|
| `OPEN(rt, md$, [opts])` | Binds the control, draws the source; `opts{"base"}` is the image folder. Answers the document map. |
| `VIEWCHANGED(doc)` | After an edit in the view: reconciles, redraws paragraphs whose look no longer fits their block, answers `{changed, start, old_len, text}`. |
| `SOURCECHANGED(doc, md$)` | After an edit of the source: redraws the blocks that changed; `TRUE` when the view changed. |
| `SETKIND(doc, k, kind$, [level])` | Turns block `k` into `para`, `heading` (with a level), `item`, `ordered`, `quote` or `code`, keeping its text. Answers the span. |
| `SETLINK(doc, url$)` | Makes the view's selection a link to `url$`. Answers the span. |
| `INSERT(doc, k, md$)` | Puts Markdown in after block `k` (`-1`: at the top) as blocks of their own, with the blank lines that keep them apart; the view caret goes to the first of them. Answers the span. |
| `TABLEPOS(doc)` | Where the view caret is in a table: `{block, row, col}`, row 0 the header; block -1 outside a table. |
| `SOURCETABLEPOS(doc, pos)` | The same for a source offset. |
| `ADDROW(doc, [from_source, pos])` | An empty row below the caret's row (below the header for the header row); the view caret goes to its first cell. Answers the span. |
| `ADDCOL(doc, [from_source, pos])` | An empty column right of the caret's cell, `---` in the delimiter row. Answers the span. |
| `SOURCE$(doc)` | The current source. |
| `BLOCKS(doc)` | The current blocks (see MDRICH). |
| `VIEWRUNS(doc)` | The `RUNS` of every view paragraph, one array per block, a table as one: what `MDRICH.SAME` and `MDRICH.MD$` read. |
| `BLOCKAT(doc, pos)` | The block whose paragraph holds a view offset (`SELSTART`). |
| `VIEWPOS(doc, k)` | The view offset where block `k` starts. |
| `SOURCEPOS(doc, k)` | The source offset where block `k`'s own lines start. |
| `BLOCKOFSOURCE(doc, pos)` | The block a source offset falls in. |

`BLOCKAT` with `VIEWPOS`, `SOURCEPOS` and `BLOCKOFSOURCE` let an editor
keep both panes on the same block when either one scrolls or moves its
caret.

## How an edit in the view is read

1. The view's paragraphs are compared with the last known ones: the
   same paragraphs at the start and at the end are untouched, and the
   paragraphs under the selection count as touched too (a format change
   leaves the text alone).
2. The touched paragraphs are read with `RUNS`. With as many paragraphs
   as blocks before, each block is rewritten from its paragraph
   (`MDRICH.BODY$`), unchanged ones byte for byte.
3. More paragraphs than before (Enter) make new blocks: after a heading a
   paragraph, after an item the next item (an ordered one numbered on),
   after a quote a quote, else a paragraph. Empty paragraphs in front of
   unchanged ones (Enter at a paragraph's start) are the new ones. Fewer
   paragraphs (Backspace at a paragraph's start) keep the first block's
   kind.
4. Blank lines after the touched range stay at its end; a blank line is
   added where two blocks would otherwise read back as one (before a
   paragraph, after an item, between a paragraph and a rule).
5. Paragraphs whose look no longer fits their block (a paragraph split
   off a heading, a new item without its number) are redrawn, and the
   caret keeps its distance from the end of its paragraph.

## Notes

- **Tables** are RichEdit tables. A table is one view paragraph: its
  rows with their structure characters, and the empty paragraph that
  keeps it from a table after it. Typing in a cell reconciles like any
  paragraph; a break typed into a cell is undone, a Markdown cell holds
  one line. A redraw that reaches a table draws the whole document, since
  RichEdit widens a selection that touches a table past its end; the
  caret is put back, the view's undo starts over.
- **Line endings**: a RICHTEXT reads and writes CRLF, so an editor that
  shows the source in one passes CRLF text in and converts on save; the
  spans then fit the source pane's offsets directly.
- **Replacing a paragraph break with text** keeps the break in RichEdit;
  deleting it joins the paragraphs. Backspace and Delete delete.
- **Ordered lists count on**: after an edit in the view, `SETKIND` or
  `INSERT`, the ordered items from the edit on take the number after the
  one in front of them, and are part of the span and redrawn. Past the
  edit the walk ends at the first item that already has its number, so a
  list the edit did not reach keeps its numbers as written (also `1. 1. 1.`).
  `SOURCECHANGED` never renumbers: the source is the writer's.

## Files

| File | What |
|------|------|
| `lib/mdsync.jdb` | the module |
| `tests/jdlibs/mdsync_selftest.jdb` | open, a view edit and its span, a source edit; skips without FORMS |
| `tests/forms/mdsync_selftest.jdb` | typing, formats, Enter after a paragraph, heading and items, joining, source edits, kind changes, links, renumbering, inserts, tables (cells, rows, columns, two in a row, one at the end); both backends |
| `jdb/demos/jdlibs/mdsync_demo.jdb` | source and rendered view side by side, edited both ways |
