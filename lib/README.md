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
| `url.jdb` | urllib.parse, furl | URLs split into parts and built back, percent and form encoding, query strings as maps, one parameter changed in place, RFC 3986 resolution and a canonical form | [url_lib_readme.md](url_lib_readme.md) |
| `mail.jdb` | smtplib, email | e-mail messages with text, HTML, inline images and attachments, RFC 2047 headers and quoted-printable, written as .eml, sent over SMTP or SMTPS through curl, and read back into headers, bodies and attachments | [mail_lib_readme.md](mail_lib_readme.md) |
| `numfmt.jdb` | babel.numbers, num2words, python-stdnum | numbers, money and percentages in six locales from the CLDR data, read back, commercial rounding, numbers and amounts in words in German and English, IBAN, VAT id and German tax numbers | [numfmt_lib_readme.md](numfmt_lib_readme.md) |
| `pdfgen.jdb` | fpdf2, reportlab | PDF files without another program: page sizes, the 14 standard fonts with their glyph widths, text, cells and wrapped paragraphs with page breaks, lines, boxes, JPEG images, tables with a repeated header, footers with page numbers | [pdfgen_lib_readme.md](pdfgen_lib_readme.md) |
| `tmpl.jdb` | Jinja2 | HTML templates: escaped and raw holes, paths into the model, conditions, loops, includes, layouts with blocks, filters and custom filters, a cache per file | [tmpl_lib_readme.md](tmpl_lib_readme.md) |
| `xml.jdb` | lxml, ElementTree, xmltodict | XML into a tree with line and column on every fault: namespaces, a subset of XPath, a map view, documents built from nothing and written back | [xml_lib_readme.md](xml_lib_readme.md) |
| `fuzzy.jdb` | rapidfuzz, jellyfish, cologne_phonetics | near matches: Levenshtein, OSA, Damerau and Indel distances, Jaro and Jaro-Winkler, ratio, partial, token and weighted ratios, the best matches from a list, Koelner Phonetik and Soundex, all on characters rather than bytes | [fuzzy_lib_readme.md](fuzzy_lib_readme.md) |
| `textx.jdb` | textwrap, python-slugify, Unidecode, humanize | paragraphs wrapped, shortened, dedented and indented, cuts that count characters, slugs and ASCII transliteration, file sizes, grouped numbers, large numbers in words, ordinals and durations in English and German | [textx_lib_readme.md](textx_lib_readme.md) |
| `textdiff.jdb` | difflib, patch | the matching runs, edit steps and similarity of lines, words or characters, unified diffs written and applied back, word and character diffs inline, snapshots for tests | [textdiff_lib_readme.md](textdiff_lib_readme.md) |
| `jdweb.jdb` | Flask, the Werkzeug test client | web apps on HTTP.SERVER: routes with path parameters for any method, middleware before and after, in-memory sessions, static files, JSON and error responses, a test client for the running app, and the themed page chrome and cookie login jdTrakr runs on | [jdweb_lib_readme.md](jdweb_lib_readme.md) |
| `docx.jdb` | python-docx, docxtpl | Word documents written with headings, bold and italic runs, lists, tables, page breaks, a header and a footer; templates filled where Word split the placeholders across runs; paragraphs and tables read back | [docx_lib_readme.md](docx_lib_readme.md) |
| `ml.jdb` | scikit-learn (preprocessing, linear_model, neighbors, cluster, tree, metrics) | a seeded train and test split, standard and min-max scaling, one-hot encoding; linear, ridge and logistic regression, k nearest neighbours, k-means and a decision tree behind one FIT, PREDICT and SCORE; accuracy, precision, recall, F1, confusion matrix, R squared, MSE; models as JSON; checked against scikit-learn on iris and diabetes | [ml_lib_readme.md](ml_lib_readme.md) |
| `parsec.jdb` | pyparsing, lark | small languages without a hand-written lexer: combinators (literals, character classes, regular expressions, sequences, choices, repetition, look-ahead, separated lists) and PEG grammars compiled into them, trees of maps with line and column, actions computing values bottom-up, errors naming the column and what was expected; arithmetic, JSON and INI grammars as examples | [parsec_lib_readme.md](parsec_lib_readme.md) |
| `svg.jdb` | svgwrite, pygal | SVG without a window: drawings with shapes, paths, text, groups, transforms and gradients; line, bar, stacked bar, scatter and pie charts with axes, ticks, a legend and colours for light and dark pages, straight from DF frames; sparklines | [svg_lib_readme.md](svg_lib_readme.md) |
| `ical.jdb` | icalendar, recurring_ical_events, dateutil.rrule | .ics calendars read and written: events, to-dos, alarms and time zones, folding and escaping; recurrence rules expanded into occurrences with EXDATE and moved occurrences; named IANA and Windows zones with summer time; files Outlook and Google import | [ical_lib_readme.md](ical_lib_readme.md) |
| `oauth.jdb` | authlib, requests-oauthlib | OAuth 2 tokens: client credentials, the authorization code flow with PKCE and a local redirect, the device code flow, refresh tokens; REQ sessions that refresh expired or refused tokens; tokens kept in a CACHE; Google and Microsoft presets | [oauth_lib_readme.md](oauth_lib_readme.md) |
| `db.jdb` | SQLAlchemy Core, dataset, Alembic migrations | SQLite without hand-written SQL strings: a query builder whose values always become quoted literals, rows as maps with NULL told from 0, inserts, bulk inserts, upserts, updates and deletes from maps, tables from a column map or an example row, transactions, numbered migrations in a version table; hostile text stays a value | [db_lib_readme.md](db_lib_readme.md) |
| `sched.jdb` | croniter, schedule, APScheduler job loop | jobs on a clock: cron expressions with ranges, steps, lists, names and L, the @hourly family, intervals and daily times; next and previous run times in a fixed offset, checked against croniter; a job loop with RETRY and LOGGER, a lock file and a policy for missed runs | [sched_lib_readme.md](sched_lib_readme.md) |
| `proptest.jdb` | hypothesis | property based tests on TESTKIT: generators for numbers, text, FAKE data, picks, lists, oneof and maps; runs from a seed that replays; failing inputs shrunk on their choice sequence to a minimal case; FORALL as one assertion | [proptest_lib_readme.md](proptest_lib_readme.md) |
| `pkg.jdb` | pip, requirements.txt, pip-tools | modules installed into `~/.jdbasic/lib` from an index, a folder, a git repository or a ZIP archive, with what they require chosen by semver ranges (`^`, `~`, comparisons, `\|\|`); list, remove and update; a project's `jdbasic.json` and `jdbasic.lock`; `jdb/tools/jdpkg.jdb` on the command line | [pkg_lib_readme.md](pkg_lib_readme.md) |
| `money.jdb` | decimal for money, py-moneyed, dinero.js | amounts as whole minor units with their ISO 4217 code: exact reading of German and English amounts, rates, division, currency conversion and cash rounding in seven rounding modes, allocation by ratios and installments that add up to the cent, locale formatting through NUMFMT exact up to 2^53 - 1 minor units; checked against Python decimal | [money_lib_readme.md](money_lib_readme.md) |
| `valid.jdb` | python-stdnum, pycountry, iso4217 | IBAN against the SWIFT registry with national checks, BIC, VAT ids of every EU member state and XI, EAN, UPC, GTIN, ISBN with conversion, Luhn and MOD 97-10; the ISO 4217 currency table with minor units and the ISO 3166-1 country table; formats by name for SCHEMA | [valid_lib_readme.md](valid_lib_readme.md) |

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
