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
| `req.jdb` | requests | sessions with base URL, headers, auth and cookies; query building, JSON and form bodies, multipart upload, retries with backoff |
| `schema.jdb` | pydantic | map validation with defaults, coercion, nesting and enums; JSON Schema output for structured LLM answers |
| `llmapi.jdb` | openai, anthropic SDKs | one chat client for OpenAI, Anthropic, OpenAI-compatible servers and the local AI.* model: tools, structured output, token usage |
| `logger.jdb` | logging | levels, console, file and JSON lines sinks, size-based rotation, a context map merged into every record |
| `conf.jdb` | python-dotenv, configparser, tomllib | dotenv, INI and a TOML subset into one map shape; dotted GET |
| `jwt.jdb` | PyJWT | HS256 sign, decode and verify with exp, nbf, iss, aud and clock skew |
| `xlsx.jdb` | openpyxl | workbooks written with bold headers, widths, number formats and frozen panes; read back as typed 2D arrays |

Tests for these modules live in `tests/jdlibs`.
