# IMG - images as pixel arrays

`lib/img.jdb` reads and writes PNG without GFX and works on the pixels as
plain arrays: crop, resize (nearest and bilinear), quarter turns, flips,
grayscale, convolution kernels, palette quantizing, paste and alpha
compositing, and data URIs for HTML pages. The pixel math follows Pillow
12.3 and is checked against it exactly. It builds on the natives
`CODEC.INFLATE$`, `CODEC.DEFLATE$`, `CODEC.CRC32`, `CODEC.BASE64_*`,
`PACK$`/`UNPACK` and `BINREADER$`/`BINWRITER`.

Stands in for: Pillow's `Image.open`/`save` for PNG, `crop`, `resize`,
`transpose`, `convert("L")`, `ImageFilter.Kernel`, `quantize` and
`alpha_composite`.

## Quick start

```basic
IMPORT IMG

DIM photo = IMG.READPNG("photo.png")
PRINT IMG.WIDTH(photo); "x"; IMG.HEIGHT(photo); " channels "; IMG.CHANNELS(photo)

DIM thumb = IMG.RESIZE(photo, 160, 120, "bilinear")
DIM gray = IMG.GRAY(thumb)
DIM edges = IMG.EDGES(gray)
IMG.WRITEPNG(edges, "edges.png", {"filter": "adaptive", "level": 9})

DIM small = IMG.QUANTIZE(thumb, 16)
IMG.WRITEPNG(small, "small.png", {"palette": 1})

DIM page$ = "<img src='" + IMG.DATAURI$(thumb) + "'>"

' pixels in, pixels out
DIM px = ZEROS(4 * 3 * 3)
px[0] = 255
DIM tiny = IMG.FROMPIXELS(4, 3, 3, px)
PRINT IMG.GETPIXEL(tiny, 0, 0)
```

## The image map

An image is a map `{"width", "height", "channels", "pixels"}`. `channels`
is 1 (gray), 2 (gray and alpha), 3 (RGB) or 4 (RGBA); `pixels` is a flat
array of `height * width * channels` values from 0 to 255, row by row, the
channels of a pixel side by side (the layout of a numpy `[h, w, c]` array).
Every function returns a new image and leaves its inputs alone. To change
pixels, take `PIXELS(img)` (a fresh copy), write into it and wrap it with
`FROMPIXELS`.

## API

| Call | What it does |
|------|--------------|
| `READPNG(path$)` | Reads a PNG file, see `DECODE`. |
| `DECODE(png$)` | An image from PNG bytes: bit depths 1, 2, 4, 8 and 16, color types gray, RGB, palette, gray+alpha and RGBA, all five filters, Adam7 interlace, any number of IDAT chunks. Every chunk CRC is checked; a bad CRC, a missing IHDR or IEND, or short data raises. |
| `PNGINFO(png$)` | A map with `"width"`, `"height"`, `"depth"`, `"colortype"`, `"interlace"`, `"channels"` (what `DECODE` returns) and `"chunks"` (the chunk names in order). |
| `ENCODE$(img, [opts])` | PNG bytes, 8 bits per sample: gray, gray+alpha, RGB or RGBA by channel count. `opts`: `"filter"` `none`, `sub`, `up`, `average`, `paeth` or `adaptive` (default: per row the filter with the smallest sum of absolute signed residuals, the libpng heuristic), `"level"` 0 to 9 (6), `"palette"` 1 to write a 3 or 4 channel image with at most 256 colors as a palette PNG (PLTE, plus tRNS when a color is not opaque). |
| `WRITEPNG(img, path$, [opts])` | `ENCODE$` to a file; the number of bytes. |
| `DATAURI$(img, [opts])` | `data:image/png;base64,...` for an `<img src>`. |
| `BLANK(w, h, [c], [value])` | A new image, channels 4 by default, filled with `value`: one number for every channel or an array with one value per channel. |
| `FROMPIXELS(w, h, c, pixels)` | An image from a flat array; values are rounded and clipped to 0..255, a wrong count raises. |
| `WIDTH(img)`, `HEIGHT(img)`, `CHANNELS(img)` | The size and channel count. |
| `PIXELS(img)` | A copy of the pixel array. |
| `GETPIXEL(img, x, y)` | The channel values of one pixel as an array. |
| `CROP(img, x, y, w, h)` | The `w` by `h` window at `x`, `y` (Pillow's `crop((x, y, x + w, y + h))`); parts outside the source are 0. |
| `RESIZE(img, w, h, [method$])` | `"bilinear"` (default) or `"nearest"`, see below. The same size gives a copy. |
| `TURN(img, [quarter_turns])` | Quarter turns counterclockwise (1 = `ROTATE_90`, 2 = `ROTATE_180`, 3 = `ROTATE_270`); negative turns go clockwise. |
| `FLIPX(img)`, `FLIPY(img)` | Mirror left-right (`FLIP_LEFT_RIGHT`) and top-bottom (`FLIP_TOP_BOTTOM`). |
| `GRAY(img)` | One channel of ITU-R 601-2 luma as Pillow's `"L"`: `(r*19595 + g*38470 + b*7471 + 32768) >> 16`. Alpha is dropped; gray+alpha keeps its gray. |
| `TOCHANNELS(img, c)` | Another channel count: gray spreads to RGB, RGB turns into luma, a missing alpha becomes 255, a dropped one is ignored. |
| `KERNEL(img, weights, size, [scale], [offset])` | Convolution as `ImageFilter.Kernel((size, size), weights, scale, offset)`: size 3 or 5, weights row by row, `scale` 0 or missing means the weight sum (1 when that is 0). Every channel is filtered, alpha included; the outer 1 (3x3) or 2 (5x5) pixels are copied, and an image smaller than the kernel comes back unchanged. |
| `BLUR(img)`, `SHARPEN(img)`, `EDGES(img)` | Pillow's `BLUR` (5x5 ring, scale 16), `SHARPEN` and `FIND_EDGES` kernels. |
| `QUANTIZE(img, [colors], [method$])` | At most `colors` (1 to 256, default 256) RGB colors. `"mediancut"` (default): the distinct colors weighted by pixel count go into one box; the box with the widest channel range is sorted along that channel and split where half its pixels lie, until there are `colors` boxes or no box holds two colors; each box becomes its weighted mean color, rounded. `"web"`: every color channel rounded to the 216-color web palette steps 0, 51, ... 255. Alpha is kept as it is. This is not Pillow's `quantize` (whose median cut differs in tie rules and palette order); the tests check its properties. |
| `COLORCOUNT(img)` | The number of distinct pixel values (all channels). |
| `PASTE(dst, src, x, y)` | `src` copied over `dst` at `x`, `y`, converted to the channels of `dst`, cut off at the edges (Pillow's `paste` without a mask). |
| `COMPOSITE(dst, src, [x], [y])` | `src` laid over `dst` with Pillow's `alpha_composite` integer math; both are taken as RGBA, the result is RGBA the size of `dst`. |

### Resampling

`"nearest"` is Pillow's `NEAREST`: the source column of output column `x`
is `int(scale * 0.5 + x * scale)`, with `scale = src / dst` added up step
by step as Pillow does.

`"bilinear"` is Pillow's `BILINEAR` (the resize path, not the affine
transform): a triangle filter whose support grows with the downscale factor
(so shrinking averages every covered pixel, no aliasing), coefficients
normalized and turned into 22-bit fixed point, a horizontal pass and then a
vertical pass, each skipped when that size stays. Gray+alpha and RGBA are
premultiplied by alpha first and divided back afterwards (Pillow's `La` and
`RGBa` modes), so transparent pixels do not bleed color.

### PNG decoding details

- 16-bit samples keep the high byte (Pillow's `I;16 >> 8` for gray, its
  `RGB`/`RGBA` conversion for color).
- Gray at 1, 2 and 4 bits is scaled to 0..255 (`* 255`, `* 85`, `* 17`).
- Palette images come back as RGB, or RGBA when a tRNS chunk is present.
- A tRNS chunk on gray or RGB adds an alpha channel: 0 where the sample
  equals the transparent value at the original bit depth, 255 elsewhere.

## Pillow agreement

`tests/jdlibs/img_selftest.jdb` (348 assertions, interpreted and compiled)
compares against fixtures made with Pillow 12.3:

| Area | Checked | Result |
|------|---------|--------|
| Decode | 23 PNGs in `fixtures/img_png`: gray 1/2/4/8/16, gray+alpha 8/16, RGB 8/16, palette 1/2/4/8, RGBA 8/16, tRNS on gray, RGB and palette, Adam7 on gray 2, RGB 8, palette 4, RGBA 16 and gray+alpha 8; rows cycle filters 0 to 4, IDAT split every 23 bytes | identical pixels |
| Geometry | crop, both flips, the three quarter turns | identical |
| Resize | nearest and bilinear RGBA to 5x4, 26x18, 7x13 and the same size; bilinear RGB 6x11 and gray 20x5 | identical |
| Color | `convert("L")` | identical |
| Kernels | `BLUR`, `SHARPEN`, `FIND_EDGES`, a 3x3 Gaussian, an offset kernel, a 3x3 box (rounding) and a 5x5 kernel with a fractional offset | identical |
| Composite | `alpha_composite` of two 13x9 RGBA images with varied alpha | identical |
| Encode | every filter mode on 1 to 4 channels, level 0 and 9, palette PNG | round trips exactly; the written files open in Pillow with identical pixels and modes `L`, `LA`, `RGB`, `RGBA`, `P`, every chunk CRC valid, and pypng reads them |

The 160x120 bilinear thumbnail of a 640x480 RGBA image also equals
Pillow's own resize of the PNG this module wrote.

## Speed

640x480 RGBA, Windows x64, one core:

| Step | Interpreted | Compiled (`-c`) |
|------|-------------|-----------------|
| `DECODE` | 1.8 s | 0.18 s |
| `ENCODE$` adaptive, level 6 (184 KB) | 7.1 s | 1.4 s |
| `ENCODE$` paeth, level 6 (203 KB) | 2.8 s | 0.67 s |
| `ENCODE$` sub, level 1 (47 KB) | 1.7 s | 0.56 s |
| `ENCODE$` none, level 0 (1.2 MB) | 1.5 s | 0.55 s |
| `RESIZE` to 160x120 bilinear | 1.6 s | 0.08 s |
| `GRAY` | 0.14 s | 0.009 s |
| `EDGES` on the gray image | 0.95 s | 0.05 s |

DEFLATE itself takes about 40 ms of that; the rest is the per-pixel work.
For large images in an interpreted script, `"filter": "sub"` or `"paeth"`
saves most of the adaptive cost.

## Compiled programs

In a program built with `-c`, hold the result of an IMG call in a
variable before passing it to the next IMG call:

```basic
DIM rgba = IMG.TOCHANNELS(thumb, 4)
DIM out = IMG.COMPOSITE(rgba, badge, 10, 10)   ' not IMG.COMPOSITE(IMG.TOCHANNELS(thumb, 4), ...)
```

Passed inline, the nested image map can arrive empty in the compiled
program and the outer call raises "an image map is needed". Interpreted
programs accept both forms.

## Files

- `lib/img.jdb` - the module
- `tests/jdlibs/img_selftest.jdb` - TESTKIT selftest
- `tests/jdlibs/fixtures/img_png/`, `img_decode.tsv`, `img_ops.tsv` - Pillow fixtures
- `jdb/demos/jdlibs/img_demo.jdb` - paints a scene, writes and reads it
  back, then thumbnail, grayscale, edges, 16 colors, a composited badge and
  a turn, as PNG files and one HTML page with data URIs
