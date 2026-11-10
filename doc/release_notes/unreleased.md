# Unreleased (since v1.3.0)

Range: `v1.3.0..HEAD`.

## New

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
