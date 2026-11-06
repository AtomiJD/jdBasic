# MDRICH: Markdown as editable rich text

`lib/mdrich.jdb` is the layer between a Markdown file and a
`FORM.RICHTEXT` that shows it rendered. It cuts the source into blocks
that tile it byte for byte, draws each block as one RTF paragraph, and
writes the `RUNS` of a paragraph the user edited back as Markdown in the
block's own marker style. A paragraph whose view did not change gives
its block's source back unchanged, so saving touches only the blocks an
edit reached; a file with `__bold__`, `* bullets` and setext headings
keeps them everywhere else.

Stands in for: the Markdown round trip of WYSIWYG editors
(prosemirror-markdown, remark-stringify, Typora's source sync).

## Quick start

```basic
IMPORT MDRICH

DIM md$ = TXTREADER$("README.md")
DIM blocks = MDRICH.BLOCKS(md$)
FORM.SET(rt, "TEXTRTF", MDRICH.DOC$(blocks, {"base": "."}))

' after an edit in paragraph k of the view:
DIM src$ = MDRICH.MD$(blocks[k], runs_of_paragraph_k)
```

`runs_of_paragraph_k` is the part of `FORM.GET(rt, "RUNS")` between two
runs whose text is CRLF; the view has exactly one paragraph per block.

## API

| Call | What it does |
|------|--------------|
| `BLOCKS(md$)` | The blocks of the source, an array of maps. `SOURCE$` of them is `md$`. |
| `SOURCE$(blocks)` | The sources put together. |
| `RTF$(b, [opts])` | One block as an RTF paragraph (paragraph format and content, no `\par`); a table as RichEdit table rows. |
| `JOINRTF$(parts, kinds)` | The `RTF$` parts of consecutive blocks with what goes between them: `\par`, nothing after a table, an empty paragraph between two tables. |
| `CELLS(row$)` | The cells of one table source line, outer pipes dropped, trimmed. |
| `DOC$(blocks, [opts])` | The whole document as RTF for `TEXTRTF`. |
| `FRAGMENT$(b, [opts])` | One block as a stand-alone RTF document for `SELRTF`. |
| `HEAD$([opts])` | The RTF header: fonts and the colour table; `opts{"dark"}` gives light text and dark quote and code colours. |
| `SAME(b, runs)` | `TRUE` when the runs show what the block drew. |
| `MD$(b, runs)` | The block's source for the runs: unchanged when `SAME`, else rewritten. |
| `BODY$(b, runs, [drawn$])` | The same without lead and tail; `drawn$` is the kind the paragraph still looks like when that differs (a paragraph split off a heading). |
| `NEWBLOCK(kind$, [model])` | An empty block for the editor: `para`, `heading`, `item` (continuing model's list, the next number), `quote`, `code`. |

`opts{"base"}` is the folder image paths are relative to.

## A block

| Key | Meaning |
|-----|---------|
| `kind` | `heading`, `para`, `item`, `quote`, `code`, `table`, `rule`, `image`, `raw` (HTML, YAML front matter between a first-line `---` and the next `---` or `...`), `blank` (a document of blank lines) |
| `lead`, `body`, `tail` | Blank lines before (the first block only), the block's own lines, blank lines after |
| `src`, `start`, `end` | `lead + body + tail` and its byte offsets in the source |
| `eol` | The block's line ending, LF or CRLF |
| `text` | The inline content; lines joined with LF, table cells with TAB |
| `segs` | The view: segments with `text` and the flags `b`, `i`, `s`, `code`, `link`, `mark`, `raw` |
| `links`, `images` | Each link's text and destination, each inline image's source |
| `bmark`, `imark` | The bold and italic markers the block uses first |

Kind keys: `level` and `style` (`atx`, `setext`) for a heading;
`indent`, `marker`, `sep`, `cont`, `ordered`, `number`, `list_start`
for an item; `prefix` for a quote; `fence`, `info`, `indent` for code
(an empty fence is an indented block); `delim`, `pipes`, `ncols` for a
table; `alt`, `path` for an image.

## How the view encodes Markdown

| Markdown | In the RICHTEXT |
|----------|-----------------|
| heading | bold, 22, 18, 15, 13, 12 or 11.5 pt |
| `**b**`, `*i*`, `~~s~~` | bold, italic, strikethrough |
| `` `code` `` | Consolas on light grey (`backcolor` 240,240,240) |
| link | blue (0,102,204), underlined; the destination stays with the block |
| image line | the picture, PNG or JPEG up to 8 MB, read through `base`; anything else shows `[alt]` in grey. Text typed next to the picture is kept with it (the image then sits inline in a paragraph); deleting the picture deletes the image line. Local paths are read as written, as any Markdown preview does; network and device paths (`\\host`, `//host`, `\??\`) and URLs never are |
| inline image | `[alt]` in grey (127,127,128) |
| HTML, reference links, footnotes | verbatim in grey (127,128,128) |
| bullet item | a RichEdit bullet, indented per level |
| numbered item | its marker (`3.`) as text in its own colour (127,127,130) and a tab; RUNS of that colour are skipped, so the number is the block's, not content |
| line break inside a paragraph | a line break (`\line`, VT in `RUNS`) |
| table | a RichEdit table with a border round every cell: a column as wide as its longest cell, aligned by the delimiter row's colons, header row bold; a row has as many cells as its source line. In `TEXT` a row reads U+FFF9, CRLF, the cells each ended by 0x07, U+FFFB, CRLF; `RunSegs` turns that back into cells split by TAB and rows by LF |
| quote, code block, raw block | grey text, Consolas on grey, Consolas grey |

## Notes

- **Edited blocks are rewritten whole**: markers the block used come back
  (`__` stays `__`), plain text is escaped where it would turn into
  markup (`*`, a backtick, `#` or `- ` at a line start, `[` before `](`),
  and a result that would read back differently is escaped throughout.
- **Bold and italic together** use the other italic character inside bold
  (`**_x_**`) so the markers cannot run together.
- **Links in the view are only their look**: the RichEdit link field puts
  hidden characters into the control positions, so the URL stays in
  `links`. A link's text is matched to the block's links by text, then
  by order; a new link without one gets `()`.
- **Tables in the view**: a `\par` right after a table's last `\row`
  makes RichEdit drop the paragraph after it, so `JOINRTF$` puts
  nothing there; two tables in a row get an empty paragraph between
  them, or RichEdit draws them as one.
- **White space keeps no format**: whether the space beside a bold word is
  bold does not count as a change, Markdown cannot say it.
- **Not in the subset**: nested blocks inside list items and quotes (they
  show as text lines), reference link definitions (a paragraph), HTML
  inline formatting. They survive untouched and are written back as text
  when their block is edited.

## Speed

`doc/languages.md`, 375 KB in 2014 blocks, on the development machine,
interpreted: `BLOCKS` 0.96 s, `DOC$` 0.41 s, `TEXTRTF` 0.19 s, `RUNS`
1.5 s. An edit costs one paragraph's `RUNS` and one `MD$`.

## Files

| File | What |
|------|------|
| `lib/mdrich.jdb` | the module |
| `tests/jdlibs/mdrich_selftest.jdb` | blocks, views, unchanged and edited runs, no window needed |
| `tests/forms/mdrich_view_selftest.jdb` | the same through a real RICHTEXT (FORMS build) |
| `jdb/demos/jdlibs/mdrich_demo.jdb` | a Markdown file in a window, edited, written back |
