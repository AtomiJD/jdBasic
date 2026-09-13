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
| `console.jdb` | tabulate, rich, tqdm | tables from arrays or maps in four border styles, styled text that degrades to plain, rules, panels, key and value blocks, progress bars, spinners | [console_lib_readme.md](console_lib_readme.md) |
| `dt.jdb` | dateutil, arrow | dates as epoch numbers with their own calendar: ISO, RFC 2822 and German parsing, fixed offsets, calendar arithmetic, rounding, ranges, relative text in English and German | [dt_lib_readme.md](dt_lib_readme.md) |
| `md.jdb` | markdown | the CommonMark subset to HTML: headings, lists, fences, quotes, tables, links, images, escaping with a raw switch | [md_lib_readme.md](md_lib_readme.md) |
| `df.jdb` | pandas | data frames as named columns: CSV in and out, computed columns, filters, sorting, group by with aggregates, joins, pivots, summaries, console tables | [df_lib_readme.md](df_lib_readme.md) |
| `yaml.jdb` | PyYAML | the block subset of YAML: mappings, sequences, implicit types, block scalars, documents, flow collections, a dotted path, and writing it back | [yaml_lib_readme.md](yaml_lib_readme.md) |
| `htmldom.jdb` | beautifulsoup | HTML as it is served into a tree: unclosed tags, implicit closes, raw text, entities; CSS selectors, text and markup of a node | [htmldom_lib_readme.md](htmldom_lib_readme.md) |
| `stats.jdb` | scipy.stats | the sample forms, shape, correlation and regression, t, chi square and rank sum tests, the normal, t and chi square distributions | [stats_lib_readme.md](stats_lib_readme.md) |
| `cache.jdb` | functools.lru_cache, cachetools | a computed read that is answered once: a size limit that throws out the entry read longest ago, a lifetime per entry, keys built from parts, counters, and a file on disk | [cache_lib_readme.md](cache_lib_readme.md) |
| `retry.jdb` | tenacity, backoff | a call made again with a growing wait: a random share, a ceiling on the run, a predicate for which errors are worth another try, and a callback for each one | [retry_lib_readme.md](retry_lib_readme.md) |
| `graph.jdb` | networkx | nodes and links: traversal, components, cycles, topological order, Dijkstra and A star, all the ways between two nodes, and Graphviz output | [graph_lib_readme.md](graph_lib_readme.md) |
| `fake.jdb` | Faker | made up names, addresses, companies, account numbers, dates and text in English and German, from a seeded generator that answers the same on every machine, plus a table built from a spec | [fake_lib_readme.md](fake_lib_readme.md) |
| `xml.jdb` | lxml, ElementTree, xmltodict | XML into a tree with line and column on every fault: namespaces, a subset of XPath, a map view, documents built from nothing and written back | [xml_lib_readme.md](xml_lib_readme.md) |

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

Three demos put several modules to work on one task:

| Demo | What it builds |
|------|----------------|
| `sales_dashboard.jdb` | a quarter of orders loaded with DF, dated with DT and reported with CONSOLE: figures, revenue by month and region, top customers, a weekday bar chart, overdue invoices, the weekly trend |
| `log_digest.jdb` | a day of server log lines in three timestamp formats, parsed by DT and digested by DF into hourly, level, endpoint and error-window tables |
| `report_site.jdb` | a three page HTML site: DF frames become Markdown tables, DT writes the changelog dates, MD renders each page and the demo adds navigation and a table of contents |

`service_report.jdb` does the same for CONF, LOGGER, XLSX and JWT.

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
