# CONSOLE - tables, styles, progress and spinners

`lib/console.jdb` renders the things a script prints while it works:
a table with automatic widths, coloured text that degrades to plain
text, a horizontal rule, a boxed panel, a key and value block, a
progress bar with an ETA and a spinner. Stdout only; the full-screen
layer is `TUI.*`.

Stands in for: tabulate, rich (the console part), tqdm.

## Quick start

```basic
IMPORT CONSOLE

CONSOLE.RULE("build")
CONSOLE.TABLE([["widget", 3, 9.5], ["gadget", 12, 0.25]], {"headers": ["item", "qty", "price"]})
PRINT CONSOLE.STYLE$("done", "bold green")

DIM bar = CONSOLE.PROGRESS(200, "packing")
DIM i = 0
FOR i = 1 TO 200
    CONSOLE.ADVANCE(bar)
NEXT i
CONSOLE.FINISH(bar)
```

## API

| Call | What it does |
|------|--------------|
| `TABLE(rows, [opts])` / `TABLE$(rows, [opts])` | Prints or returns a table. `rows` is a 2D array or an array of maps (headers are the union of the keys). Options below. |
| `STYLE$(text$, spec$)` | Wraps the text in the escapes for a space separated spec: `bold faint italic underline blink inverse strike`, the eight colours, `bright_red`, `on_blue`, `on_bright_white`. |
| `BOLD$ FAINT$ RED$ GREEN$ YELLOW$ BLUE$ CYAN$ MAGENTA$` | Shortcuts for one style each. |
| `STRIP$(text$)` / `VISIBLE(text$)` | The text without escapes, and its visible length. |
| `FIT$(text$, width, [align$])` | Pads (`l`, `r`, `c`) or cuts with a `~`, counting visible characters. |
| `RULE([title$], [fill$])` / `RULE$` | A rule across the width, the title centred. |
| `PANEL(text$, [title$], [width])` / `PANEL$` | The text wrapped inside a box. |
| `KV(items, [indent])` / `KV$` | Key and value lines from a map or `[[key, value], ...]`, keys padded to the longest. |
| `WRAP(text$, width)` | The words as an array of lines; a longer word is cut. |
| `PROGRESS(total, [label$], [width])` | A bar; `ADVANCE(bar, [steps])` moves it, `FINISH(bar)` completes it, `PROGRESS$(bar)` is the current line as text. |
| `SPINNER(label$)` | A spinner; `SPIN(s)` shows the next frame, `DONE(s, [final$])` ends the line. |
| `DURATION$(seconds)` | `3s`, `2m05s`, `1h02m`. |
| `PLAIN(flag)` / `IS_PLAIN()` | Escapes off or on. Off by itself when `NO_COLOR` is set or `TERM` is `dumb`. |
| `WIDTH()` / `SET_WIDTH(n)` | The width for rules and panels: what was set, else `COLUMNS`, else 80. |

## Table options

| Key | Meaning |
|-----|---------|
| `headers` | Column titles; with map rows also the columns to show and their order. |
| `align` | One of `l`, `r`, `c` per column. Numbers are right aligned by default. |
| `border` | `box` (default), `plain`, `markdown`, `none`. |
| `widths` | A maximum per column; `0` leaves a column alone. Longer cells are cut with a `~`. |
| `max_width` | The whole table; the widest column gives way until it fits. |
| `title` | A line above the table. |

Cells render numbers with `STR$`, booleans as `TRUE`/`FALSE`, arrays and
maps as JSON, `NONE` as an empty cell. The header is bold in the `box`,
`plain` and `none` styles; a `markdown` table stays free of escapes, so
it can go straight into a file, and its reader marks the header.

## Plain mode

Without a terminal the escapes only add noise, so in plain mode `STYLE$`
returns the text unchanged, the progress bar prints one line per ten
percent instead of redrawing, and the spinner prints its label once and
the final text. Plain mode is on when `NO_COLOR` is set in the
environment, when `TERM` is `dumb`, or after `CONSOLE.PLAIN(TRUE)`.

## Notes

- The width is not read from the terminal; set `COLUMNS` in the
  environment or call `SET_WIDTH` when 80 is wrong.
- On Linux and macOS a redrawn line becomes visible on the next `SLEEP`
  or newline, the way `PRINT` with a trailing semicolon behaves there.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/console_selftest.jdb`. Demos:
`jdb/demos/jdlibs/console_demo.jdb`, and reporting DF frames in
`sales_dashboard.jdb` and `log_digest.jdb`.
