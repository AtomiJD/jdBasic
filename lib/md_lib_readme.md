# MD - Markdown to HTML

`lib/md.jdb` turns the CommonMark subset that documents actually use
into HTML: headings, paragraphs with hard breaks, nested bullet and
numbered lists, code fences with a language class, blockquotes, rules,
tables with column alignment, and the inline spans. Text is HTML
escaped unless the raw switch is on.

Stands in for: markdown, mistune.

## Quick start

```basic
IMPORT MD

PRINT MD.RENDER$("# Title" + CHR$(10) + "Some *text* with `code`.")
DIM page$ = MD.FILE$("README.md")
PRINT MD.RENDER$(text$, {"raw": TRUE})      ' hand-written HTML passes through
```

## API

| Call | What it does |
|------|--------------|
| `RENDER$(text$, [opts])` | The document as HTML. Blocks are separated by a newline; there is no trailing newline. |
| `FILE$(path$, [opts])` | The same for a file. |
| `INLINE$(text$, [raw])` | One line of inline Markdown, for a title or a table cell. |
| `ESCAPE$(text$)` | `& < > "` as entities. |

`opts` is a map; `raw` (default `FALSE`) lets a block that starts with a
tag, and inline tags in text, pass through unescaped.

## What is recognised

**Blocks**: `#` to `######` headings (closing hashes dropped);
paragraphs, with a hard break after two trailing spaces or a backslash;
`-`, `*`, `+` bullets and `1.` or `1)` numbers, nested by indentation,
continuation lines indented to the text, a blank line between items of
the same kind keeps the list; ``` and `~~~` fences, the word after the
fence becoming `class="language-word"`; `>` blockquotes holding any
block; `---`, `***`, `___` rules; tables with a `|---|:--:|--:|`
delimiter row, alignment as a `style` attribute, `\|` inside a cell.

**Inline**: `*em*`, `_em_`, `**strong**`, `__strong__`, `~~del~~`,
`` `code` `` (double backticks around a backtick), `[text](url "title")`,
`![alt](src "title")`, `<https://autolink>`, backslash escapes for the
punctuation. An underscore inside a word and a star with spaces around
it stay text.

Not in the subset: setext headings, reference links, footnotes,
nested blockquotes inside list items, and raw HTML unless `raw` is set.

## The fixture

`tests/jdlibs/fixtures/md_fixture.md` renders byte for byte to
`md_fixture.html`; the self test checks it on both backends.

Self test: `tests/jdlibs/md_selftest.jdb`. Demo: `jdb/demos/jdlibs/md_demo.jdb`.
