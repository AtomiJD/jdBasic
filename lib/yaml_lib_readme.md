# YAML - the subset configuration files are written in

`lib/yaml.jdb` reads and writes the part of YAML that turns up in
pipelines, compose files and application settings: block mappings and
sequences nested by indentation, scalars with their implicit types,
quoted strings, block scalars, comments, document markers and one line
flow collections. A document becomes plain maps, arrays, strings,
numbers, truth values and `NONE`.

Stands in for: PyYAML (`safe_load` and `safe_dump`).

## Quick start

```basic
IMPORT YAML

DIM cfg = YAML.PARSEFILE("docker-compose.yml")
PRINT cfg{"services"}{"web"}{"image"}
PRINT YAML.GET(cfg, "services.web.ports.0")
PRINT YAML.GET(cfg, "services.web.deploy.replicas", 1)

cfg{"version"} = "3.9"
TXTWRITER "compose.yml", YAML.DUMP$(cfg)
```

## API

| Call | What it does |
|------|--------------|
| `PARSE(text$)` | The first document of the text. |
| `PARSE_ALL(text$)` | Every document, as an array. |
| `PARSEFILE(path$)` | The first document of a file. |
| `GET(root, path$, [fallback])` | The value at a dotted path, or the fallback when a step is missing. A number selects an element of a sequence: `"jobs.build.steps.0.run"`. |
| `DUMP$(value, [opts])` | The value as YAML text with a trailing newline. `indent` (2) sets the step, `document` (`FALSE`) writes a leading `---`. |

## What is read

**Structure**: block mappings, block sequences, and any nesting of the
two. A sequence may sit at its key's own indentation or be indented
under it. An item that opens a mapping carries the rest of that mapping
on the following lines: the usual

```yaml
steps:
  - name: Build
    run: build.bat
```

**Scalars**: `42`, `-1.5`, `1.5e3`, `0x1f` become numbers; `true`,
`false`, `yes`, `no`, `on`, `off` become truth values in any case;
`null`, `~` and an empty value become `NONE`; everything else is a
string. `'single'` quotes are literal with `''` for one quote,
`"double"` quotes take `\n \t \r \" \\ \/ \0` and `\uXXXX`. Quoting
keeps a value a string: `port: "8080"`.

**Block scalars**: `|` keeps the newlines, `>` folds each paragraph into
one line, `-` strips the last newline, `+` keeps the trailing ones, and
a digit sets the indentation explicitly. A blank line inside the block
is kept, a line indented deeper keeps that indentation.

**The rest**: `#` starts a comment where it opens a line or follows a
space, so a URL fragment survives; `---` separates documents and `...`
ends one; `[a, b]` and `{k: v}` on one line are read, including nesting
and quoted items.

## What is not read

Anchors (`&name`), aliases (`*name`), tags (`!!str`) and merge keys
(`<<`) raise an error naming the line rather than being half
understood. Tabs are not indentation in YAML and are not accepted as
such here either.

## What is written

`DUMP$` writes block style with two spaces per level. A string is
quoted when it would otherwise read as something else: a number, a
truth value, `null`, an empty string, a leading indicator character, a
`: ` inside it, or a port-like `8080:8080`. A string with newlines
becomes a `|` block. An empty map is `{}` and an empty sequence `[]`. A
mapping inside a sequence shares the dash line with its first key.

Reading a document and writing it again gives the same tree back; the
self test checks that on both a pipeline and a compose file.

## The fixtures

`tests/jdlibs/fixtures/workflow.yml` is a GitHub Actions workflow and
`tests/jdlibs/fixtures/compose.yml` a docker compose file; the self
test reads both and checks the values down to the fifth level.

Self test: `tests/jdlibs/yaml_selftest.jdb`. Demo: `jdb/demos/jdlibs/yaml_demo.jdb`.
