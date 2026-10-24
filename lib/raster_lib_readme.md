# RASTER: SVG drawn into pixels, without a window

`lib/raster.jdb` turns SVG markup into an IMG image, so a chart or a
drawing becomes a PNG on a machine with no display: a build without GFX,
a server, a CI runner, the MCP workshop. It draws what `SVG.RENDER` and
the SVG building calls write: rectangles, circles, ellipses, lines,
polylines, polygons, paths with lines, curves and arcs, strokes with
caps and round joints, groups with transforms, the `<style>` block and
`style` attributes, opacity, linear and radial gradients, and text from
a TrueType font.

Stands in for: cairosvg, resvg, the `svg2png` step of a report pipeline.

## Quick start

```basic
IMPORT SVG

DIM c = SVG.CHART("bar")
SVG.SETOPT(c, "title", "Sales 2026")
SVG.LABELS(c, ["Jan", "Feb", "Mar", "Apr"])
SVG.SERIES(c, "north", [120, 95.5, 143, 80])
SVG.SERIES(c, "south", [60, 110, 70, 130])
SVG.WRITEPNG(SVG.RENDER(c), "sales.png", {"scale": 2})

' any drawing the same way
DIM d = SVG.NEW(200, 120)
SVG.ADDRECT(d, 10, 10, 80, 50, {"fill": "#4e79a7", "rx": 8})
SVG.ADDTEXT(d, 100, 30, "Hello", {"text_anchor": "middle", "font_weight": "bold"})
DIM img = SVG.TOIMAGE(d)
```

```basic
' markup from anywhere
IMPORT RASTER, IMG

DIM img = RASTER.RENDER(TXTREADER$("logo.svg"), {"scale": 3, "background": "none"})
IMG.WRITEPNG(IMG.RESIZE(img, 96, 96), "logo96.png")
```

## API

| Call | What it does |
|------|--------------|
| `RENDER(markup$, [opts])` | The markup as an RGBA image map, the shape IMG uses (`width`, `height`, `channels`, `pixels`). |
| `WRITEPNG(markup$, path$, [opts])` | The same, written as a PNG with the unfiltered encoder (the fast one; a chart is a few KB either way). |
| `FONT(path$)` | The TrueType file text is drawn with from now on; `""` goes back to the search below. |

`SVG.TOIMAGE(doc, [opts])` and `SVG.WRITEPNG(doc, path$, [opts])` take a
drawing or a rendered chart handle and call these.

`opts` is a map:

| Key | Default | Meaning |
|-----|---------|---------|
| `scale` | 1 | Multiplies the size: 2 gives a crisp picture on a dense screen or in a document. |
| `background` | `"#fff"` | The page colour under the drawing; `"none"` leaves it transparent, so the PNG composes over anything. |
| `font` | | A TrueType file for this render only. |

The size comes from the `width` and `height` attributes of the `svg`
element, or from its `viewBox` when they are missing or percentages; a
`viewBox` is mapped onto the size, uniformly scaled and centred.

## How it draws

Every shape becomes polygons first: circles and ellipses by segments,
curves and arcs flattened to pieces short enough for the size they are
drawn at, a stroke as one strip per piece plus a round joint at every
corner. One filler draws them all: it accumulates the signed area of
each edge per pixel and sums along the row, so a pixel gets the fraction
of it the shape covers, edges come out smooth, and a hole wound the
other way round stays open (the nonzero rule, as the browser uses it).
Colour is composited with that coverage times the opacity over what is
already on the page.

### Style

Presentation attributes, then the rules of every `<style>` block that
match the element (by tag, class and ancestors, heavier rules last),
then the `style` attribute, in that order. `@media` blocks are left out,
so a chart comes out in its light colours. `fill`, `stroke`,
`stroke-width`, `stroke-linecap`, the two paint opacities, `font-size`,
`font-weight` and `text-anchor` pass from a group to what is inside it;
`opacity` multiplies down the groups. Colours are `#rgb`, `#rrggbb`,
`#rrggbbaa`, `rgb()`, `rgba()`, the common names, `none`, and
`url(#id)` for a gradient of the document.

### Text

Text is drawn from glyph outlines, which `TTF.OUTLINE` reads from the
font, scaled by `font-size` and placed by `text-anchor`; a `font-weight`
of 600 or bold thickens the outlines. The font is the first of: the
`font` option, the file given to `FONT`, `jdbasic_default.ttf` next to
the running program (the GFX builds ship it, and `-c` copies it next to a
compiled program), `resources/fonts/JetBrainsMono-Regular.ttf` under the
working directory. Without any, a five by seven bitmap font draws the
text in dots, so labels stay readable and a test on a bare machine still
passes. `font-family` is read but not looked up: every text is drawn in
the one font.

## Not drawn

Filters, masks, clip paths, patterns, markers, `use` and `symbol`,
`stroke-dasharray`, `tspan` positions (a `tspan` contributes its text),
and `textPath`. Such markup is skipped or drawn plain; nothing throws.
Fonts with CFF outlines (OpenType `.otf`) are not TrueType and fall
back to the bitmap font.

## Speed

A 640x400 bar chart with two series, title, legend and axes, Windows
x64, one core:

| Step | Interpreted | Compiled (`-c`) |
|------|-------------|-----------------|
| `RENDER` | 0.2 s | 0.02 s, plus 0.13 s reading the font before the first text |
| `WRITEPNG` on top of it | 0.17 s | 0.06 s |

The filler visits only the columns an edge touched on each row and
writes an opaque pixel without the blend, which is what makes a chart
cheap: most of its pixels are inside a bar or nowhere near a shape.

## Files

- `lib/raster.jdb`, on `lib/xml.jdb` for the markup, `lib/img.jdb` for
  the image and the PNG, `lib/ttf.jdb` for the glyph outlines.
- `tests/jdlibs/raster_selftest.jdb` probes pixels at known coordinates:
  fills, coverage at a half-pixel edge, strokes, transforms, gradients,
  style rules, opacity, arcs, text with and without a font, the
  viewBox, and a chart written as a PNG and read back.
