# Unreleased (since v1.3.0)

Range: `v1.3.0..HEAD`.

## New

- **`FORM.PRINTAREA()`** (#1116): the width and height inside the page
  setup's margins in twips, for the printer and paper chosen last or the
  default printer, so a program can lay content out to the page before it
  prints. `MDRICH` takes it as `opts{"table_width"}`: a wider table is
  narrowed to fit, its wide columns wrapping inside the cell. jdTexti
  prints and copies the rendered view that way, drawn light also in dark
  mode, and has a right-click menu in both panes. `CLIPBOARD.SET text$,
  [rtf$], [html$]` puts formatted versions next to the plain text, the
  richest first; jdTexti copies the view as HTML, RTF and text, so a plain
  Ctrl+V in Word keeps headings, lists and ruled tables where Word took the
  text before. `MD.RENDER$` renders a table indented under a list item
  inside that item.
- **Images without a window** (#1109, #1114): `IMAGE.SIZE` reads the width
  and height of a JPEG, PNG, GIF or BMP from its header, `IMAGE.CROP` and
  `IMAGE.RESIZE` (Windows) cut and scale a photo. All of them, and
  `IMAGE.CONVERT`, take a JPEG the way its EXIF orientation displays it, so
  a phone photo stays upright, and write no metadata. `IMAGE.META` reads the
  EXIF, GPS position, XMP and PNG text of a JPEG or PNG on every platform,
  `IMAGE.STRIP` removes all of it or only the position without touching the
  image, and `IMAGE.SETMETA` sets or removes the artist, copyright,
  description, software and date.
- **`tools/hooks/check_images.ps1`** (#1115): an agent commit that takes an
  image with a GPS position, a photographer or owner name, a camera serial
  or a private term in its metadata is blocked; an untracked image goes into
  `.gitignore`, a tracked one is reported. The check itself is
  `tools/hooks/image_privacy.jdb <repo> <list-file>`, usable from any
  repository.
- **`jdbasic --repl-script <file>`**: the REPL fed from a file, or from
  standard input with `-`. Each line is echoed behind the prompt and
  executed as if typed, then the program exits; a REPL session can be
  repeated by a test or a tool without a terminal.
- **`tools/verify.sh`**: one command after a change. It rebuilds the
  interpreter and the runtime library when a source under `src/` is newer
  than them and runs `tools/gate_run.jdb --smokes`.
- **`tools/gate_run.jdb --smokes`** compiles and starts the two GUI programs
  of the gate; a first step `binary` reports `STALE` when a source is newer
  than the binaries the gate is about to test.
- **`doc/signatures.md`**: every documented builtin as its signature alone,
  grouped by the section of the reference that describes it, written by
  `tools/gen_signatures.jdb` and checked by CI. `--core` prints the
  sections every program draws on, for an agent's session start.
- **`tools/gate_run.jdb`** also checks what is generated from the reference:
  the builtin table against the help text, the contents and index of
  `languages.md`, and `signatures.md`.
- **`ON "TEXTINPUT"`** (#998): a `SCREEN` window hands a handler the text a
  key press produced, `info[0]{"text"}` as UTF-8, with the keyboard layout,
  Shift, AltGr and dead keys applied. Before, a program had only key codes
  and could not read an umlaut or a shifted character. The window sends text
  from the moment a handler is registered.
- **`IMAGE.CONVERT(src$, dst$)`** (#1057): converts an image file to the
  format of the target's extension (PNG, JPEG, BMP, TIFF, GIF) without a
  window, through the Windows Imaging Component. Windows only.
- **MDRICH** embeds GIF, BMP and TIFF pictures as PNG, so jdTexti shows them
  in the view and in print; RTF itself has no GIF picture. Pictures are read
  once per file version, and the document is joined in one step, so drawing
  a long document with many pictures again is faster than before. A
  picture is converted only when its first bytes say GIF, BMP or TIFF, so a
  document cannot hand its files to any other installed image decoder.
- **Form `PLACEMENT`**: `FORM.GET(frm, "PLACEMENT")` answers where a form
  sits and whether it is maximized as one text, `FORM.SET` puts it back and
  fits it to the monitors attached now. A program that stores it opens the
  way it was closed, and a monitor that is gone or smaller does not leave
  the window out of reach.
- **Edit field `MARGIN`**: `FORM.SET(h, "MARGIN", n)` keeps room left and
  right of the text of a `TEXTBOX` or `RICHTEXT`.
- **jdTexti** opens where and as large as it was closed, keeps a margin
  beside the text, and opens a Markdown file in the layout chosen last.
- **`FORM.SHAPE`** fires `MOUSEDOWN`, `MOUSEUP` and `MOUSEMOVE` and keeps the
  mouse while the left button is down, so a thin shape can be dragged as a
  splitter bar; `CURSOR` takes `SIZEWE` and `SIZENS`. jdTexti uses it for
  the bar between source and view, whose position it remembers.

## Changed

## Fixed

- **A module FUNC or SUB keeps an undeclared assignment local** (#966): a
  module function that assigns a name it never declared (`h = OPEN(...)`)
  gets a local of that name and no longer writes the importing program's
  global `h`. The importing program's `OPTION "EXPLICIT"` no longer refuses
  such a module; the module's own `OPTION "EXPLICIT"`, or `-c`, does.
- **Modules that import each other** (#983): a top-level read of the other
  module's variable or CONST before that module has set it
  (`DIM a[OTHER.SIZE]` in the module imported second) is refused when the
  program loads, with the name and the line, instead of running on with an
  array of the wrong size.
- **`HTTP.SERVER.START` on a port in use answers FALSE on Linux and macOS**
  (#1001): the listening socket took SO_REUSEPORT, so a second program
  bound the same port, START answered TRUE and the two shared the
  connections. START now binds before it returns, a refused START leaves
  no server behind, and `NET.UDP` refuses a port in use as on Windows.

- **Native compiler (`-c`), a variable declared AS INTEGER is an integer**
  (#979): `DIM n AS INTEGER = cells[i]` typed `n` as a double, so `n * n`
  lost its low bits above 2^53 and `DIM a[n]` could run out of memory; the
  same for a cell of an `UNPACK` answer and for `DIM n AS INTEGER = 2.7`,
  which is now 2 as in the interpreter. A global declared AS a number that a
  SUB sets from an array cell keeps its type: a module's `EXPORT DIM w AS
  INTEGER` read 4630263366890291200 for 36 in the importing program. Under
  `OPTION "STRICT"` such an assignment is reported like one to a local
  (`wrap with CINT()`).
- **Interpreter, reading a long string** (#978): `BYTEAT`, `LEFT$`, `RIGHT$`,
  `MID$` and `INSTR` copied the whole string on every call, 1.8 ms per call
  on a 13 MB file read with `BINREADER$`. They read it where it lies now
  (0.0005 ms).
- **`GFX.SAVE_SCREENSHOT` on a scaled `SCREEN`** (#980): the image showed the
  top-left part of the screen magnified. It shows the whole screen at the
  size it has on the display, and a region is given in drawing coordinates.
