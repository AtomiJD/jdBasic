# XML - XML into a tree, queried with paths, built and written back

`lib/xml.jdb` reads XML into a tree, answers a subset of XPath over it,
builds documents from nothing, and writes them back out. It is strict
where XML is strict: a document that is not well-formed is refused with
the line and column of the first fault.

A document is an array of nodes, and every call takes the document and a
node number, the same shape HTMLDOM uses. Nothing points back, so a tree
can be passed around or held in a map without a cycle.

Stands in for: lxml, xml.etree.ElementTree, xmltodict.

## Quick start

```basic
IMPORT XML

DIM inv = XML.PARSEFILE("invoice.xml")
PRINT XML.VALUE$(inv, "/ubl:Invoice/cac:LegalMonetaryTotal/cbc:PayableAmount")
PRINT XML.VALUE$(inv, "//cbc:PayableAmount/@currencyID")

DIM lines = XML.FINDALL(inv, "//cac:InvoiceLine")
DIM i = 0
FOR i = 0 TO LEN(lines) - 1
    PRINT XML.VALUE$(inv, "cbc:ID", "", lines[i]); "  "; XML.VALUE$(inv, "cbc:LineExtensionAmount", "", lines[i])
NEXT i

DIM doc = XML.DOCUMENT("order")
DIM top = XML.ROOT(doc)
XML.SETATTR(doc, top, "number", "4711")
DIM item = XML.ELEMENT(doc, top, "item", "Widget")
XML.SETATTR(doc, item, "qty", "2")
XML.WRITEFILE(doc, "order.xml")
```

## API

### Reading

| Call | What it does |
|------|--------------|
| `PARSE(xml$, [opts])` | The document tree. Node 0 is the document itself. Throws on a document that is not well-formed. |
| `PARSEFILE(path$, [opts])` | The same from a file. A declared ISO-8859-1 or windows-1252 file is decoded to UTF-8; UTF-16 is recognised by its byte order mark. |

`opts` is a map. `{"keep_space": TRUE}` keeps the whitespace between
elements as text nodes; by default a text that is only whitespace is
dropped, so an indented file gives the same tree as a compact one.

### The tree

| Call | What it does |
|------|--------------|
| `SIZE(doc)` | How many nodes there are. |
| `ROOT(doc)` | The root element, or -1. |
| `KIND$(doc, id)` | `document`, `element`, `text`, `cdata`, `comment` or `pi`. |
| `NAME$(doc, id)` | The name as written, prefix included; the target of a processing instruction. |
| `LOCAL$(doc, id)` / `PREFIX$(doc, id)` | The two halves of the name. |
| `URI$(doc, id)` | The namespace of an element, from the nearest declaration of its prefix, or `""`. |
| `PARENT(doc, id)` | The node above, or -1. |
| `CHILDREN(doc, id)` | Every child node, in order. |
| `ELEMENTS(doc, id, [test$])` | The child elements, or those that match a name test (see below). |
| `ATTR$(doc, id, name$, [fallback$])` / `HAS(doc, id, name$)` | One attribute, and whether it is there. Names are compared as written. |
| `ATTRNAMES(doc, id)` / `ATTRS(doc, id)` | The attribute names in written order, or every attribute as a map. |
| `TEXT$(doc, id)` | For an element or the document, every text and CDATA node below it joined. For a text, CDATA, comment or processing instruction node, its content. |

### Paths

| Call | What it does |
|------|--------------|
| `FINDALL(doc, path$, [root])` | Every element the path reaches, in document order. |
| `FIND(doc, path$, [root])` | The first of them, or -1. |
| `VALUES(doc, path$, [root])` | The text of each element reached; the attribute values when the path ends in `@name`; the direct text when it ends in `text()`. |
| `VALUE$(doc, path$, [fallback$], [root])` | The first of `VALUES`, or the fallback. |

A path that starts with `/` is taken from the document; any other one from
`root`, which is the document unless given.

Supported:

- steps separated by `/`, and `//` for any depth below
- `*`, `.` and `..`
- a trailing `@name`, `@*` or `text()`
- predicates, applied one after the other: `[n]` (counted from 1 among the
  matching children of one parent, as XPath counts), `[last()]`, `[@name]`,
  `[@name='v']`, `[child]`, `[child='v']`, `[text()='v']`, each comparison
  also with `!=`

Name tests:

| Test | Matches |
|------|---------|
| `cbc:ID` | the name as written, prefix included |
| `ID` | the local name, in any namespace |
| `{urn:...}ID` | the local name in that namespace, whatever prefix the file uses |
| `cbc:*` / `{urn:...}*` | every element with that prefix, or in that namespace |

The unprefixed form is the practical one for invoices and feeds: it reads
the same file whichever prefixes the sender chose. The braced form is the
exact one.

Not supported: unions (`|`), axes by name (`following-sibling::`),
functions other than `last()` and `text()`, arithmetic, and comparisons
other than `=` and `!=`.

### The map view

| Call | What it does |
|------|--------------|
| `TOMAP(doc, [id])` | The xmltodict shape: a map with the name of the element, or of the root for the document, as its only key. |

Below that key, an element without attributes or child elements is its
text. Any other element is a map: `"@name"` for each attribute, `"#text"`
for its own text when there is some, and one key per child name, holding
an array when the name repeats.

```basic
DIM m = XML.TOMAP(XML.PARSE("<order id='7'><item>a</item><item>b</item><total cur='EUR'>9.5</total></order>"))
DIM order = m{"order"}
PRINT order{"@id"}          ' 7
PRINT LEN(order{"item"})    ' 2
```

### Building

| Call | What it does |
|------|--------------|
| `DOCUMENT(name$)` | A new document with a root element of that name. |
| `ELEMENT(doc, parent, name$, [text$])` | A new element under `parent`, with an optional text inside it. Returns its number. |
| `ADDTEXT(doc, parent, text$)` / `ADDCDATA(...)` / `ADDCOMMENT(...)` | A text, CDATA or comment node. |
| `SETATTR(doc, id, name$, value$)` | Sets an attribute; one that is already there keeps its place. |
| `DELATTR(doc, id, name$)` | Removes an attribute. |
| `SETTEXT(doc, id, text$)` | Replaces everything inside an element with one text. |
| `DETACH(doc, id)` | Takes a node out of its parent. |

Namespaces are attributes like any other: set `xmlns` or `xmlns:p` on the
element that declares them and write the prefixed names as they should
appear.

### Writing

| Call | What it does |
|------|--------------|
| `MARKUP$(doc, [id], [indent$])` | The markup of a node; for the document, with the XML declaration in front. |
| `WRITEFILE(doc, path$, [indent$])` | The document to a file, UTF-8. |
| `ESCAPE$(text$)` / `UNESCAPE$(text$)` | Text to markup and back. |

`indent$` is repeated once per level and defaults to two spaces; `""`
writes everything on one line. An element that holds text is written on
one line with its children, so indenting never adds whitespace to the
text. Attributes keep the order they were written or set in, so the same
tree always gives the same file.

## What the parser refuses

The message is `XML line L, column C: ...` and names what went wrong:

- a closing tag that does not match the open element, or closes nothing
- an element left open at the end
- a second root element, text outside the root, no root at all
- an attribute twice on one element, one without a value, an unquoted
  value, a `<` inside a value
- a comment, CDATA section, processing instruction, document type or tag
  that is never finished
- a name that starts with a digit, `-` or `.`

## Entities

`&amp; &lt; &gt; &quot; &apos;` and the numeric references `&#228;` and
`&#xE4;` are decoded in text and in attribute values; a code point above
ASCII comes out as UTF-8. An entity that a DTD would have to define
(`&nbsp;` in XHTML, say) stays as it was written.

Line breaks become `\n` while reading. Inside an attribute value a line
break or a tab becomes a space, as the standard says; a `&#10;` reference
keeps its line break, and writing produces that reference again.

## Notes

- An invoice of 2000 lines, 580 KB and 20000 nodes, parses in about half
  a second interpreted and a quarter of a second compiled. A `//` path
  over all of it takes under 0.2 s, writing it back 0.13 s interpreted.
  Hold the result of `FINDALL` rather than calling it inside a loop, and
  query a line relative to its node.
- There is no DTD processing and no external entity is ever loaded, so a
  hostile file cannot reach the disk or the network.
- A document type declaration, internal subset included, is skipped. The
  XML declaration is read past and written back as UTF-8.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/xml_selftest.jdb`, which reads the KoSIT
XRechnung 3.0 examples `tests/jdlibs/fixtures/xrechnung_ubl.xml` and
`tests/jdlibs/fixtures/xrechnung_cii.xml`.
Demo: `jdb/demos/jdlibs/xml_demo.jdb`.
