# HTMLDOM - HTML into a tree, with CSS selectors

`lib/htmldom.jdb` parses the HTML that is actually served, not the HTML
the standard describes: unclosed tags, tags that close each other,
unquoted attribute values, boolean attributes, comments, raw text in
`script` and `style`, entities, stray markup. The result is a tree you
query with CSS selectors.

A document is an array of nodes, and every call takes the document and a
node number. Nothing points back, so a tree can be passed around, held in
a map, or written as JSON without a cycle.

Stands in for: beautifulsoup, lxml.html.

## Quick start

```basic
IMPORT HTMLDOM, REQ

DIM site = REQ.NEW("https://jdbasic.org")
DIM answer = REQ.GET(site, "/guide")
DIM page = HTMLDOM.PARSE(answer{"body"})
DIM links = HTMLDOM.FINDALL(page, "nav.toc a")
DIM i = 0
FOR i = 0 TO LEN(links) - 1
    PRINT HTMLDOM.ATTR$(page, links[i], "href"); "  "; HTMLDOM.FLAT$(page, links[i])
NEXT i

PRINT HTMLDOM.PICK$(page, "h1")
PRINT LEN(HTMLDOM.FINDALL(page, "pre > code"))
```

## API

### Reading

| Call | What it does |
|------|--------------|
| `PARSE(html$)` | The document tree. Node 0 is the document itself. |
| `PARSEFILE(path$)` | The same from a file. |

### The tree

| Call | What it does |
|------|--------------|
| `SIZE(doc)` | How many nodes there are. |
| `KIND$(doc, id)` | `document`, `element`, `text` or `comment`. |
| `TAG$(doc, id)` | The tag name, lowered. |
| `PARENT(doc, id)` | The node above, or -1. |
| `CHILDREN(doc, id)` / `ELEMENTS(doc, id)` | Every child, or only the element children. |
| `ATTRS(doc, id)` | Every attribute as a map; names are lowered, values are not. |
| `ATTR$(doc, id, name$, [fallback$])` / `HAS(doc, id, name$)` | One attribute, and whether it is there at all. |

### Text and markup

| Call | What it does |
|------|--------------|
| `TEXT$(doc, id)` | The text of the subtree, entities decoded, whitespace as it stands. |
| `FLAT$(doc, id)` | The same squeezed to one line and trimmed: what a heading or a link reads as. |
| `HTML$(doc, id)` | The markup of the subtree. |
| `INNER$(doc, id)` | The markup inside it. |
| `ESCAPE$(text$)` / `UNESCAPE$(text$)` | Text to markup and back. |

### Selectors

| Call | What it does |
|------|--------------|
| `FINDALL(doc, selector$, [root])` | Every match under `root` (the document by default), in document order. |
| `FIND(doc, selector$, [root])` | The first match, or -1. |
| `PICK$(doc, selector$, [fallback$])` | `FLAT$` of the first match, or the fallback. |
| `MATCHES(doc, id, selector$)` | Whether one node matches. |

Supported: `tag`, `*`, `#id`, `.class` (several in a row), `[attr]`,
`[attr=value]`, `[attr^=value]`, `[attr$=value]`, `[attr*=value]`,
`[attr~=word]`, a descendant (a space), a direct child (`>`),
`:first-child`, `:last-child`, `:nth-child(n)`, `:first-of-type`,
`:nth-of-type(n)`, and several selectors separated by a comma. A value
in an attribute test may be quoted.

Not supported: sibling combinators (`+`, `~`), `:not()`, the `an+b` form
of `nth`, and pseudo-elements.

## What the parser does with broken HTML

- A tag left open is closed at the end of the document, and by the
  closing tag of anything that encloses it.
- A tag closes the ones it cannot sit inside: a second `<li>` ends the
  first, `<tr>` ends the open cell and row, `<dt>` and `<dd>` end each
  other, and any block element ends an open `<p>`.
- A closing tag for nothing open is dropped.
- `area base br col embed hr img input link meta param source track wbr`
  take no children; `<br/>` is the same as `<br>`.
- `script` and `style` keep their content as text, so a `<` or a `</b>`
  inside a string does not start a tag.
- A `<` that begins nothing is text.
- A doctype and a processing instruction are skipped; a comment becomes
  a node of its own and carries no text.

## Entities

`&amp; &lt; &gt; &quot; &apos;` and the numeric forms `&#65;` and
`&#x41;` are decoded in text and in attribute values. The ones with a
plain equivalent get it, so a report or a CSV stays readable: a
non-breaking space becomes a space, a dash becomes `-`, an ellipsis
becomes three dots, `&copy;` becomes `(c)`. Everything else known comes
out as UTF-8. An entity that is not known stays as it was written.

## Notes

- Whitespace between elements is kept as text nodes, so `TEXT$` of a
  container separates the words the way the page does.
- A selector search walks the tree; for a page of a few thousand nodes
  that is a few milliseconds. Hold the result of `FINDALL` rather than
  calling it inside a loop.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/htmldom_selftest.jdb`, which reads the saved
documentation page `tests/jdlibs/fixtures/docpage.html`.
Demo: `jdb/demos/jdlibs/htmldom_demo.jdb`.
