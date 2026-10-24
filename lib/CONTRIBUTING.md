# Writing a module for lib/

`lib/` is the module library that ships with jdBasic: one file per
module, written in jdBasic, reached with `IMPORT`. This page is the
shape every module in it has, as a checklist, and the way a module of
yours gets in. `tools/check_lib.jdb` checks the same list, so run it
before you open a pull request:

```
jdBasic tools/check_lib.jdb gif          # one module
jdBasic tools/check_lib.jdb gif --run    # and run its self test, interpreted and compiled
jdBasic tools/check_lib.jdb --all        # every module, what the CI runs
```

## The module file

`lib/<name>.jdb`, lower case, one file, no helpers in other files.

- **A header comment** that says what the module does, shows the
  calls in a short example, and names the version:

  ```basic
  ' gif.jdb - animated GIF89a files written and read without GFX: LZW,
  ' a global palette, the loop extension; a frame is an IMG image map.
  '
  '   IMPORT GIF
  '   GIF.WRITE("anim.gif", [frame0, frame1], 100)
  '
  ' version: 1.0.0
  ' requires: IMG ^1

  EXPORT MODULE GIF
  ```

  `version:` is semver and moves with the file (see below).
  `requires:` is optional: PKG reads the module's `IMPORT` lines on its
  own, the line is for a version range on one of them. Say what the
  module stands in for from the Python world, it is how the index
  describes it.
- **`EXPORT MODULE NAME`**, upper case, the file name. Public calls are
  `EXPORT FUNC` / `EXPORT SUB`; helpers are plain `FUNC` / `SUB` and
  stay invisible outside.
- **English identifiers and comments.** A comment says what the code
  does, never what it used to do or who asked for it.
- **No builtin name as a variable, parameter or function name.**
  Identifiers are case-insensitive, so `DIM count`, `DIM left`, `DIM now`,
  `FUNC SCAN` all collide. `jdBasic --lint lib/<name>.jdb` names every
  such collision; the checker fails on one.
- **Answers:** a function that answers a string ends in `$`; one that
  builds a container answers a map or an array the caller owns.
  Trailing parameters may carry a literal default (`opts = 0`); a map
  option is tested with `TYPEOF(opts) = "OBJECT"`.
- **Both backends.** The interpreter is loose; `-c` is STRICT and
  EXPLICIT, and the self test runs through both in the parity sweep.
  What that asks of a module: every `DIM` typed or initialised
  (`DIM n = 0`, `DIM out = []`, `DIM m AS MAP`); no `LAMBDA` that reads a
  local of the enclosing FUNC (write a loop, or `LAMBDA USE(x) ...`);
  no `IMPORT` inside a FUNC; a builtin that exists only in one backend
  behind `OS.FEATURE`.
- **Bytes.** `UNPACK(STR$(n) + "b", s$)` gives the bytes of a string as
  an array, `JOIN(CHR$(bytes), "")` gives them back as a string, NULs
  included; `PACK$` and `UNPACK` read and write numbers in both byte
  orders; `BINREADER$` and `BINWRITER` move files. Nothing needs a
  base64 detour. Glue binary pieces with `+`, not with `JOIN` over an
  array of them: compiled, JOIN stops a piece at its first NUL (trakr
  #718).
- **No em-dash**, in code or prose; write `-`.

## The readme page

`lib/<name>_lib_readme.md`, the page `lib/README.md` links to. Its
sections, in this order:

1. `# NAME: one line that says what it is`
2. a paragraph on what it does and what it builds on, then
   `Stands in for: <the Python libraries it replaces>`
3. `## Quick start` with a program that runs as written
4. `## API`, a table of every exported call with what it answers
   (a module with several areas, like SVG, has one `##` per area)
5. `## Notes` on the choices and the edges (what is not done)
6. `## Speed` when the module does real work on data: a small table
   of measured times, interpreted and compiled
7. `## Files`: the module, its test, its demo, its fixtures

## The row in lib/README.md

One row in the table under "The modules", in the place where it reads
best among its neighbours:

```
| `gif.jdb` | Pillow (GIF), imageio | animated GIF89a files written and read without GFX: ... | [gif_lib_readme.md](gif_lib_readme.md) |
```

## The self test

`tests/jdlibs/<name>_selftest.jdb`, built on TESTKIT, ends with
`TESTKIT.REPORT()` and prints `n passed, 0 failed`. It runs offline,
from the repository root or from its own folder (a fixture path falls
back from `tests/jdlibs/fixtures/` to `fixtures/`), and it compiles
with `-c`. A test that compares against a reference implementation
keeps the reference's numbers in a fixture file and says in its header
how they were made.

## The demo

`jdb/demos/jdlibs/<name>_demo.jdb`, a program that shows the module
doing its job, offline. A module whose demos carry another name lists
them in its header: `' demo: llm_facts.jdb, llm_tools_demo.jdb`.

## Versions

`1.0.0` when a module enters the library. A change that keeps every
call working raises the patch or minor number; a call that changes its
arguments or its answer raises the major. PKG reads the number from the
header and `jdpkg index lib` publishes it, so a program elsewhere can
say `requires: GIF ^1`.

## The way in

1. Write the module in your own repository with the files above in the
   same places (`lib/`, `tests/jdlibs/`, `jdb/demos/jdlibs/`). It is
   usable from there straight away: `jdBasic jdb/tools/jdpkg.jdb install
   https://github.com/you/yourmodule.git` puts it into `~/.jdbasic/lib`,
   where `IMPORT` finds it on every machine.
2. Run the checker against your tree: `jdBasic tools/check_lib.jdb
   gif --run` from a checkout of jdBasic with your files copied in.
3. Open a pull request with the module, its page, its row, its test
   and its demo, and say in the description what the checker and the
   gate said. Small and focused: one module per pull request.

What the review looks at is what the checker cannot: whether the
module fits the language (APL-style array calls over loops, the idioms
`languages.md` documents), whether the API reads like the rest of the
library, and whether the test proves the module rather than exercising
it.
