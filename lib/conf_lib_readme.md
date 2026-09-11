# CONF - dotenv, INI and a TOML subset

`lib/conf.jdb` loads the three configuration formats that actually turn
up next to a program into one nested map shape, and reads it back with
a dotted path and a fallback.

Stands in for: python-dotenv, configparser, tomllib.

## Quick start

```basic
IMPORT CONF

DIM env = CONF.ENV(".env")                ' {"DB_URL": "...", ...}
CONF.ENV(".env", TRUE)                    ' and pushed into the process environment
DIM ini = CONF.INI("app.ini")             ' {"section": {"key": "value"}}
DIM toml = CONF.TOML("app.toml")          ' {"server": {"port": 8080, "hosts": [...]}}

PRINT CONF.GET(toml, "server.port", 80)
PRINT CONF.GET(ini, "server.host", "127.0.0.1")
```

## API

| Call | What it does |
|------|--------------|
| `ENV(path$, [apply])` | dotenv into a flat map; `apply` sets each key with `SETENV`. |
| `INI(path$, [policy$])` | Sections into a map of maps. `policy$` for a duplicate key: `"last"` (default), `"first"`, `"error"`. |
| `TOML(path$)` | The TOML subset below. |
| `ENV_TEXT(text$, [apply])` / `INI_TEXT(text$, [policy$])` / `TOML_TEXT(text$)` | The same from a string. |
| `GET(cfg, path$, [fallback])` | A dotted read; the fallback when a key is missing or the path runs through a scalar. |

## What each format accepts

**dotenv**: `KEY=value` per line, an optional `export ` prefix, `#`
comment lines, single quotes literal, double quotes with `\n`, `\t`,
`\r`, `\"` and `\\`, an unquoted value trimmed and cut at ` #`.

**INI**: `[section]` headers, `key=value` or `key: value`, `;` and `#`
comment lines, values kept as strings. Keys before the first section
land in the `""` section.

**TOML subset**: `[table]` and `[a.b]` headers, `[[array.of.tables]]`,
basic strings with the dotenv escapes, literal strings, integers (with
underscores) as INT64, floats, booleans, arrays with nested arrays,
inline tables, trailing `#` comments. A date or time value is kept as
the text that was written. Multi-line strings, unicode escapes and the
rest of the grammar are not supported and raise or arrive as text.

## Notes

- INI values are strings. Read them into the type of a default with
  `VAL`, `CINT` or a comparison, as `jdb/demos/jdlibs/conf_demo.jdb`
  does when it layers INI over TOML.
- `GET` walks maps only: an array of tables is returned whole at its
  key, and indexing into it is the caller's `[i]`.

## Tests and demo

- `tests/jdlibs/conf_selftest.jdb`
- `jdb/demos/jdlibs/conf_demo.jdb` (defaults, an operator's INI, secrets in .env and the command line, layered)
