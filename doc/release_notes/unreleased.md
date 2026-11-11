# Unreleased (since v1.3.0)

Range: `v1.3.0..HEAD`.

## New

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

## Changed

## Fixed

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
