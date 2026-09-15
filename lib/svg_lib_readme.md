# SVG - drawings and charts without a window

`lib/svg.jdb` writes SVG: drawings built from shapes, paths, text,
groups, transforms and gradients, and charts (line, bar, stacked bar,
scatter, pie and donut) with axes, ticks, a legend and colours that read
on light and dark pages. Nothing needs a window or a graphics build, so
it works under `-c`, on a server, and straight into a jdweb page, where
the markup can be written into the HTML as it is.

Stands in for: svgwrite, pygal.

## Quick start

```basic
IMPORT SVG, DF

DIM sales = DF.READCSV("sales.csv")                 ' region, q1, q2
DIM c = SVG.CHART("bar")
SVG.SETOPT(c, "title", "Sales by region")
SVG.FROMDF(c, sales, "region", ["q1", "q2"])
SVG.WRITEFILE(SVG.RENDER(c), "sales.svg")

DIM d = SVG.NEW(300, 200)
DIM sky$ = SVG.GRADIENT$(d, "sky", ["#4e79a7", "#76b7b2"], 90)
SVG.ADDRECT(d, 0, 0, 300, 200, {"fill": sky$})
SVG.GROUP(d, {"transform": SVG.TRANSLATE$(150, 100) + " " + SVG.ROTATE$(15)})
SVG.ADDCIRCLE(d, 0, 0, 50, {"fill": "#f28e2b"})
SVG.ENDGROUP(d)
SVG.ADDTEXT(d, 12, 24, "Tom & Jerry", {"fill": "#fff", "font_size": 18})
PRINT SVG.MARKUP$(d)

PRINT "<td>" + SVG.SPARKLINE$([3, 5, 4, 8, 6, 9]) + "</td>"
```

## Drawings

A drawing is a handle from `NEW`. Every shape takes an optional map of
attributes: a string is written as it is (escaped), a number rounded to
two decimals, and an underscore in a name becomes a hyphen, so
`{"stroke_width": 2, "fill_opacity": 0.5}` writes `stroke-width="2"
fill-opacity="0.5"`.

| Call | What it does |
|------|--------------|
| `NEW(width, height)` | A new drawing; its view box is its size. |
| `ADDRECT(doc, x, y, w, h, [attrs])` | A rectangle; `rx` rounds its corners. |
| `ADDCIRCLE(doc, cx, cy, r, [attrs])` / `ADDELLIPSE(doc, cx, cy, rx, ry, [attrs])` | A circle, an ellipse. |
| `ADDLINE(doc, x1, y1, x2, y2, [attrs])` | A line. |
| `ADDPOLYLINE(doc, xs, ys, [attrs])` / `ADDPOLYGON(doc, xs, ys, [attrs])` | Through the points of two arrays, open or closed. |
| `ADDPATH(doc, d$, [attrs])` | A path from its data, `"M 10 10 L 90 10 Q 50 60 10 10 Z"`. |
| `ADDTEXT(doc, x, y, text$, [attrs])` | Text at its baseline; `text_anchor` is `start`, `middle` or `end`. |
| `GROUP(doc, [attrs])` / `ENDGROUP(doc)` | Everything added between them goes into one group, so a transform, a fill or an opacity applies to all of it. Groups can nest; `MARKUP$` closes any left open. |
| `TITLE(doc, text$)` | A tooltip for the group it is in, or for the drawing. |
| `STYLE(doc, css$)` | A style sheet in the drawing's definitions. |
| `GRADIENT$(doc, id$, colors, [angle])` | A linear gradient through the colours at an angle in degrees (0 left to right, 90 top to bottom); answers `url(#id)` to use as a fill or a stroke. |
| `RADIAL$(doc, id$, colors)` | A radial gradient from the centre; answers `url(#id)`. |
| `RAW(doc, markup$)` | Markup written as it is. |
| `TRANSLATE$(x, y)` / `ROTATE$(deg, [cx, cy])` / `SCALE$(sx, [sy])` | Transform texts to join with spaces for a `transform` attribute. |
| `MARKUP$(doc)` | The drawing as SVG markup. |
| `WRITEFILE(doc, path$)` | The markup into a file. |

## Charts

```basic
DIM c = SVG.CHART("line", 640, 400)      ' line, bar, stackedbar, scatter, pie, area, histogram, box
SVG.SETOPT(c, "title", "Visitors")
SVG.LABELS(c, ["Jan", "Feb", "Mar"])
SVG.SERIES(c, "web", [120, 135.5, 150])
SVG.SERIES(c, "shop", [80, "", 90])       ' not a number: a gap
DIM page$ = "<div>" + SVG.MARKUP$(SVG.RENDER(c)) + "</div>"
```

| Call | What it does |
|------|--------------|
| `CHART(kind$, [width], [height])` | A chart, 640 by 400 by default. |
| `LABELS(chart, labels)` | The categories along the x axis; the slices of a pie. |
| `SERIES(chart, name$, values)` | A series, one value per label; a cell that is not a number is a gap. A pie takes its first series. |
| `POINTS(chart, name$, xs, ys)` | A series of points for a scatter chart, or for a line or area chart with numbers along x. |
| `BINS(chart, name$, values, [bins])` | A histogram series: `bins` (10) equal-width bins from the smallest to the largest value, as `HISTOGRAM` and `numpy.histogram` count them. |
| `BINCOUNTS(chart, name$, counts, edges)` | A histogram series from counts and one edge more, as `HISTOGRAM` and `HISTEDGES` answer them. |
| `CURVE(chart, name$, xs, ys)` | A line through points without dots, over a chart with numbers along x. |
| `FITLINE(chart, name$, coeffs)` | A polynomial drawn across the x range, highest power first as `FIT.POLYFIT` answers it. |
| `REFLINE(chart, axis$, value, [label$])` | A dashed reference line at a value of the `y` axis or, with numbers along x, of the `x` axis. |
| `NOTE(chart, x, y, text$)` | A marked point with a text; on a chart with categories x is the category's index. |
| `BOXSTATS(values)` | A map of `n`, `q1`, `median`, `q3`, `iqr`, `whislo`, `whishi`, `mean` and `outliers`, the numbers a box plot draws. |
| `FROMDF(chart, frame, x_col$, y_cols)` | A DF frame: the x column becomes the labels (the x values of a scatter chart), each column named in `y_cols` (a name or a list) a series. |
| `SETOPT(chart, key$, value)` | An option, see below. |
| `RENDER(chart)` | Draws the chart into a new drawing; answers its handle. |
| `SPARKLINE$(values, [width], [height], [color$])` | A small line of the values, 120 by 24 by default, as markup for a table cell or a sentence. |

| Kind | Draws |
|------|-------|
| `line` | a line per series across the labels, a dot per value, broken at gaps |
| `bar` | the series side by side for every label, from zero |
| `stackedbar` | the series on top of each other, negative values below zero |
| `scatter` | a dot per point on two numeric axes |
| `pie` | a slice per label from the first series with its share; a donut with `donut` |
| `area` | a line per series filled down to zero, across the labels or along numbers from `POINTS`; stacked with `stacked` |
| `histogram` | a bar per bin from `BINS` or `BINCOUNTS`, as wide as the bin, on a numeric x axis |
| `box` | a box per series from q1 to q3 with the median, whiskers and outliers as circles |

| Option | Means |
|--------|-------|
| `title`, `x_title`, `y_title` | the texts above the chart and along its axes |
| `legend` | `top` (default) or `none` |
| `y_min`, `y_max` | the ends of the y axis instead of the rounded data range |
| `decimals` | decimals of the axis labels; by default as many as the tick step needs |
| `dots` | `0` leaves the dots off a line chart |
| `values` | `1` writes the numbers above the bars |
| `donut` | the hole of a pie as a part of its radius, up to 0.9 |
| `colors` | the series colours, a list separated by commas |
| `background` | a colour behind the chart; transparent by default |
| `x_min`, `x_max` | the ends of a numeric x axis |
| `y_log`, `x_log` | `1` for a log scale with a tick every decade; `x_log` needs numbers along x; values of zero or below are left out |
| `stacked` | `1` stacks the series of an area chart |

The y axis rounds the data range out to steps of 1, 2 or 5 times a power
of ten, about five ticks, and a bar chart always includes zero. Long
category labels are thinned out so they do not overlap. A log axis runs
from the decade at or below the smallest value to the decade at or above
the largest.

Box plots follow matplotlib's `boxplot` defaults: quartiles by linear
interpolation (`numpy.percentile`'s default method), whiskers at the most
extreme values within 1.5 times the interquartile range of the box, the
values beyond drawn as outliers. Reference lines and notes widen the axes
to include themselves on the numeric charts; on bar and category line
charts they are drawn when they fall inside the axis.

```basic
DIM h = SVG.CHART("histogram")
SVG.BINS(h, "ms", times, 20)
SVG.REFLINE(h, "x", SVG.BOXSTATS(times){"median"}, "median")
DIM s = SVG.CHART("scatter")
SVG.POINTS(s, "measured", xs, ys)
SVG.FITLINE(s, "fit", FIT.POLYFIT(xs, ys, 2))
```

Every mark carries its data as attributes, `data-series`, `data-label`,
`data-value` (and `data-x` on a scatter point, `data-lo`/`data-hi` on a
bin, `data-q1`, `data-median`, `data-q3`, `data-whislo`, `data-whishi`
on a box, `data-ref` and `data-note` on reference lines and notes), and a
`<title>` tooltip,
so a chart can be checked or scripted in the page. Text and grid take
their colours from a style sheet with a `prefers-color-scheme: dark`
rule, and the series colours (Tableau 10) are mid-toned, so the same
chart reads on a light and a dark page.

## Notes

- Label widths are estimated at 7 pixels per character of a 12 pixel
  system font; nothing is measured, because nothing is rendered.
- Numbers in the markup are rounded to two decimals.
- A self-contained file: no fonts, scripts or images are referenced.
- Everything works compiled with `-c`, with markup identical to the
  interpreter's.

Self test: `tests/jdlibs/svg_selftest.jdb` reads every chart back with XML
and measures its marks against the data, histogram bins, box statistics,
log ticks and fitted polynomials against numpy and matplotlib values in
`tests/jdlibs/fixtures/svg_chart_*.tsv`, and the classic charts byte for
byte against `svg_chart_classic_*.svg`; with Edge or Chrome installed it
also renders one to PNG. Demos: `jdb/demos/jdlibs/svg_demo.jdb` writes a
report page with five charts and sparklines from a DF frame;
`jdb/demos/jdlibs/chart_demo.jdb` a page with a histogram, box plots, a
stacked area, a log scale, a fitted curve and reference lines.
