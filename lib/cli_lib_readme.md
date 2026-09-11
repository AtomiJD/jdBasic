# CLI - command line parsing

`lib/cli.jdb` turns the argument vector into a map with every default
already filled in: flags, options with values, numbers, repeatable
options, positionals, subcommands with their own options, and the help
text for all of it.

Stands in for: argparse, click.

## Quick start

```basic
IMPORT CLI

DIM spec = CLI.NEW("mytool", "does a thing")
CLI.VERSION(spec, "1.2.0")
CLI.FLAG(spec, "verbose", "v", "chatty output")
CLI.OPT(spec, "out", "o", "output file", "out.txt")
CLI.NUM(spec, "retries", "r", "how often to try again", 3)
CLI.MANY(spec, "tag", "t", "may be given more than once")
CLI.ARG(spec, "source", "the file to read", TRUE)

DIM build_cmd = CLI.CMD(spec, "build", "compile the thing")
CLI.FLAG(build_cmd, "release", "R", "optimise")

DIM got = CLI.PARSE(spec, CLI.ARGV())
IF CLI.DONE(spec, got) THEN END CLI.STATUS(got)
PRINT got{"out"}; " "; got{"verbose"}; " "; got{"args"}[0]
```

Accepted on the command line: `--name value`, `--name=value`, `-n value`,
`-n`, clustered short flags `-abc`, and `--` to stop reading options.

## API

| Call | What it does |
|------|--------------|
| `NEW(name$, about$)` | A new spec; `INIT(spec, name$, about$)` fills a map the caller already has. |
| `VERSION(spec, text$)` | What `--version` prints. |
| `FLAG(spec, long$, short$, help$)` | A switch, `FALSE` unless given. |
| `OPT(spec, long$, short$, help$, fallback$)` | An option with a string value. |
| `NUM(spec, long$, short$, help$, fallback)` | An option with a numeric value; a non-number is an error. |
| `MANY(spec, long$, short$, help$)` | A repeatable option, collected into an array. |
| `ARG(spec, name$, help$, required)` | A positional, in declaration order. |
| `CMD(spec, name$, about$)` | A subcommand; returns its own spec for `FLAG` / `OPT` / `NUM` / `MANY` / `ARG`. |
| `ARGV()` | `OS.ARGS()` without the program path, on both backends. |
| `PARSE(spec, argv)` | The result map. Never ends the program. |
| `DONE(spec, got)` | Prints help, the version or the error with the usage line, and answers `TRUE` when the run should stop. |
| `STATUS(got)` | The exit status a shell expects: 0 for help or version, 2 for a wrong command line. |
| `HELP$(spec)` | The generated help text. |

The result map carries every option by its long name, `ok`, `error`,
`help`, `wantversion`, `args` (the positionals) and, with subcommands,
`command`.

## Notes

- `PARSE` never ends the program by itself, so a test can drive it and
  read `got{"error"}`.
- Positionals land in `got{"args"}` in the order they were declared; a
  required one that is missing sets `got{"error"}` and names it.
- A short name may be empty when only the long form is wanted.

## Tests and demo

- `tests/jdlibs/cli_selftest.jdb`
- `jdb/demos/jdlibs/cli_demo.jdb` (runs four command lines by itself when given none)
