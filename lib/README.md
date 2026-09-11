# lib - the jdBasic module library

Reusable modules written in jdBasic itself, the layer above the builtins. A
module here is a single `.jdb` file that starts with `EXPORT MODULE NAME` and
marks its public functions with `EXPORT FUNC` / `EXPORT SUB`.

## Using one

```basic
IMPORT TESTKIT
TESTKIT.EQ(2 + 2, 4, "addition")
```

`IMPORT` searches the script's own directory first, then a `modules/`
subdirectory of it, then the working directory, then every entry in
`JDBASIC_PATH`, then `<user home>/.jdbasic/lib`, and finally `<directory of
jdBasic.exe>/lib`. The full table is in `doc/languages.md` under `IMPORT`.

## Installing

Copy the module into one of the last two locations:

- `~/.jdbasic/lib` for this user only
- next to the interpreter, in `<install dir>/lib`, to ship it with jdBasic

During development, `JDBASIC_PATH=D:\usr\dev\cc\lib` points at this directory
without copying anything.

## Contents

| Module | Stands in for | Notes |
|--------|---------------|-------|
| `testkit.jdb` | pytest | assertions, suites, TAP and JUnit output, non-zero exit on failure |
| `cli.jdb` | argparse, click | flags, options with defaults, positionals, subcommands, generated help |

Tests for these modules live in `tests/jdlibs`.
