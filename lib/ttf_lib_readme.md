# TTF: TrueType fonts for layout and embedding

`lib/ttf.jdb` reads what a layout and a PDF need from a TrueType font
file: its metrics, the advance width of every glyph, and the character
map from Unicode code points to glyphs. PDFGEN uses it to embed fonts
and to measure text in them.

Stands in for: the reading side of fontTools.

## Quick start

```basic
IMPORT TTF

DIM serif = TTF.LOAD("fonts/SourceSerif4-Regular.ttf")
PRINT serif{"name"}, serif{"upem"}, serif{"glyphs"}
PRINT TTF.WIDTH(serif, "Hello") * 11 / TTF.UPEM(serif); " points at 11 pt"
PRINT TTF.GLYPHS(serif, "Grüße")
PRINT TTF.MISSING(serif, "ok ✓ 中")          ' code points without a glyph
```

## API

| Call | What it does |
|------|--------------|
| `LOAD(path$)` | A font map: `"data"` (the file), `"name"` (the PostScript name), `"upem"`, `"ascent"`, `"descent"`, `"cap"` (cap height), `"bbox"` `[x0, y0, x1, y1]`, `"italic"` (degrees), `"monospace"`, `"glyphs"` (the count) and `"widths"` (one advance width per glyph), all in font units. |
| `CODEPOINTS(text$)` | The Unicode code points of a UTF-8 text. |
| `GLYPH(font, code)` | The glyph for a code point; 0, the font's `.notdef`, when it has none. |
| `GLYPHS(font, text$)` | One glyph per character of a text. |
| `WIDTH(font, text$)` | The advance width of a text in font units; divide by `UPEM` and multiply by the size in points. |
| `UPEM(font)` | Font units per em. |
| `MISSING(font, text$)` | The code points above the space that the font has no glyph for. |

## Notes

- TrueType outlines only (`glyf`); an OpenType file with CFF outlines is
  refused with a message.
- The character map comes from a Unicode subtable of format 12 when the
  font has one, format 4 otherwise.
- Kerning and ligatures are not applied; widths are the plain advances.
- Tables are read with `UNPACK`, so a font loads in a few milliseconds.

Self test: `tests/jdlibs/ttf_selftest.jdb`.
