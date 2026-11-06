# jdTexti

A plain-text editor on native Windows forms, and a Markdown editor with a
rendered view you can type in. Needs a FORMS build.

```
jdbasic jdtexti.jdb [file.txt | file.md]
jdbasic -c jdtexti.jdb            (jdtexti.jdb.props gives the .exe its icon)
```

## Text

Multi-level undo, Find / Replace (match case, whole word, wrap around, up or
down), Go To line, word wrap, zoom (Ctrl+wheel, Ctrl+Plus / Minus / 0),
spell check, tab width 2 / 4 / 8, dark mode, page setup and printing, and
the eight most recent files. A file keeps its encoding (UTF-8 with or
without BOM, UTF-16 LE / BE, ANSI) and its line endings (CRLF or LF; a file
with both keeps the ending of every line that was not edited); both can be
changed from the File menu, and choosing a line ending there makes them all
the same. A change made to the open file by another program is noticed when
either pane gets the focus again.

## Markdown

A `.md` or `.markdown` file opens as its source on the left and the rendered
document on the right. Both panes are editable and kept in step:

- An edit in the rendered view rewrites only the Markdown blocks it reached.
  Every other byte of the source stays as it was, so a diff of the saved file
  shows exactly what was changed.
- An edit in the source redraws only the blocks that changed.
- The other pane follows the caret to the same block.
- Saving writes the source pane, in the file's own encoding and line endings.
- Printing from the rendered view (focused, or shown alone) prints it as
  shown, with headings, tables and pictures; in dark mode it goes to paper
  drawn light. From the source pane the source prints as plain text.
- Dark mode darkens both panes. Saving under another folder resolves
  relative image paths against the new one.
- Undo and Redo (Ctrl+Z, Ctrl+Y) step one history, the source's, from
  either pane: an edit in the view is one step there, a typing burst one
  step per sync, and the view is redrawn from the source.

View menu: **Plain Text**, **Markdown Split** (Ctrl+M), **Markdown
Rendered** (Ctrl+Shift+M, the view alone).

Format > Markdown and the button bar above the panes:

| Command | Keys | Button |
|---|---|---|
| Bold, italic, strikethrough, inline code | Ctrl+B, Ctrl+I, Ctrl+Shift+X, Ctrl+Shift+C | B, I, S, `</>` |
| Link | Ctrl+K | Link |
| Heading 1 to 3, paragraph | Ctrl+1..3, Ctrl+Shift+P | H1-H3, Text |
| Bullet list, numbered list, quote, code block | Ctrl+Shift+8, Ctrl+Shift+7, Ctrl+Shift+Q, Ctrl+Shift+K | List, 1. List, Quote, Code |
| Insert table, image, horizontal rule | Ctrl+Shift+T, Ctrl+Shift+G, Ctrl+Shift+R | Table, Image, Rule |
| Table: insert row, insert column | Ctrl+Shift+W, Ctrl+Shift+U | |

The menu ticks and the buttons are drawn bold for the format under the caret:
the block kind in either pane, the inline formats in the rendered view.

In the source pane the inline commands wrap the selection in Markdown
markers; in the view they set the format, and the Markdown follows. Inserted
blocks go after the block under the caret, with the blank lines that keep
them apart. An image inside the document's folder is written with a path
relative to the document.

Tables are drawn with a border round every cell. Type in a cell as
anywhere else; the Markdown row follows. A new row goes below the row the
caret is in, a new column right of its cell, from either pane. A cell
holds one line: Enter inside a cell is taken back.

A numbered list counts on by itself: an item added, made or removed in the
view renumbers the items after it. A list the edit did not reach keeps its
numbers as written.

Built on two library modules: `lib/mdrich.jdb` (Markdown blocks to rich text
and back) and `lib/mdsync.jdb` (the two-way sync).
