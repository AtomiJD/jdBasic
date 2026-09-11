# lib - the jdBasic module library

Reusable modules written in jdBasic itself, the layer above the builtins. A
module here is a single `.jdb` file that starts with `EXPORT MODULE NAME` and
marks its public functions with `EXPORT FUNC` / `EXPORT SUB`. Each module has
its own page with the API, the notes that matter and the test and demo that
go with it.

## Using one

```basic
IMPORT TESTKIT, CLI
TESTKIT.EQ(2 + 2, 4, "addition")
DIM got = CLI.PARSE(spec, CLI.ARGV())
```

`IMPORT` takes one name or a comma-separated list. It searches the script's
own directory first, then a `modules/` subdirectory of it, then the working
directory, then every entry in `JDBASIC_PATH`, then `<user home>/.jdbasic/lib`,
and finally `<directory of jdBasic.exe>/lib`. The full table is in
`doc/languages.md` under `IMPORT`.

## Installing

Copy the module into one of the last two locations:

- `~/.jdbasic/lib` for this user only
- next to the interpreter, in `<install dir>/lib`, to ship it with jdBasic

During development, `JDBASIC_PATH=D:\usr\dev\cc\lib` points at this directory
without copying anything. A module that imports another one (LLMAPI uses REQ
and SCHEMA) finds it through the same search.

## The modules

| Module | Stands in for | One line | Page |
|--------|---------------|----------|------|
| `testkit.jdb` | pytest | assertions, suites, TAP and JUnit output, non-zero exit on failure | [testkit_lib_readme.md](testkit_lib_readme.md) |
| `cli.jdb` | argparse, click | flags, options with defaults, positionals, subcommands, generated help | [cli_lib_readme.md](cli_lib_readme.md) |
| `req.jdb` | requests | sessions with base URL, headers, auth and cookies; query building, JSON and form bodies, multipart upload, retries with backoff | [req_lib_readme.md](req_lib_readme.md) |
| `schema.jdb` | pydantic | map validation with defaults, coercion, nesting and enums; JSON Schema output for structured LLM answers | [schema_lib_readme.md](schema_lib_readme.md) |
| `llmapi.jdb` | openai, anthropic SDKs | one chat client for OpenAI, Anthropic, OpenAI-compatible servers and the local AI.* model: tools, structured output, token usage | [llmapi_lib_readme.md](llmapi_lib_readme.md) |
| `logger.jdb` | logging | levels, console, file and JSON lines sinks, size-based rotation, a context map merged into every record | [logger_lib_readme.md](logger_lib_readme.md) |
| `conf.jdb` | python-dotenv, configparser, tomllib | dotenv, INI and a TOML subset into one map shape; dotted GET | [conf_lib_readme.md](conf_lib_readme.md) |
| `jwt.jdb` | PyJWT | HS256 sign, decode and verify with exp, nbf, iss, aud and clock skew | [jwt_lib_readme.md](jwt_lib_readme.md) |
| `xlsx.jdb` | openpyxl | workbooks written with bold headers, widths, number formats and frozen panes; read back as typed 2D arrays | [xlsx_lib_readme.md](xlsx_lib_readme.md) |

## Tests and demos

Every module has a self test under `tests/jdlibs/` (`<name>_selftest.jdb`,
built on TESTKIT) and at least one demo under `jdb/demos/jdlibs/`. The demos
run offline: the ones that need a service start one on `HTTP.SERVER` in the
same process, and `llm_facts.jdb` and `llm_tools_demo.jdb` switch to a real
vendor only when `LLM_PROVIDER` is set.

```
jdBasic tests/jdlibs/req_selftest.jdb
jdBasic jdb/demos/jdlibs/req_demo.jdb
```

## Conventions

- A module is one file, English identifiers, comments that say what the
  code does.
- Public functions are `EXPORT FUNC` / `EXPORT SUB`; helpers are plain
  `FUNC` / `SUB` and stay invisible outside the module.
- A function that answers a string ends in `$`; a function that builds a
  container returns a map or an array the caller owns.
- Trailing parameters may carry a literal default (`opts = 0`); a map
  option is tested with `TYPEOF(opts) = "OBJECT"`.
- A builtin name is never reused as a function or variable name, since
  identifiers are case-insensitive and the loader refuses the collision.
