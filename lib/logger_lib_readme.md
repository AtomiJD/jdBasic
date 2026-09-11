# LOGGER - levels, sinks and rotation

`lib/logger.jdb` writes what a script or a service does to the console,
to a text file and to a JSON lines file, with a level threshold, a
context map merged into every record, and size-based rotation. It is
named LOGGER because `LOG` is the natural logarithm.

Stands in for: logging.

## Quick start

```basic
IMPORT LOGGER

DIM lg = LOGGER.NEW("packer")
LOGGER.LEVEL(lg, "INFO")
LOGGER.TO_CONSOLE(lg)
LOGGER.TO_FILE(lg, "packer.log", 1000000, 3)
LOGGER.TO_JSONL(lg, "packer.jsonl")
LOGGER.CONTEXT(lg, {"host": "cortex", "run": 42})

LOGGER.INFO(lg, "build started", {"target": "arm64"})
LOGGER.WARN(lg, "slow disk")
LOGGER.ERROR(lg, "link failed", {"exit": 2})
LOGGER.DEBUG(lg, "not shown below INFO")
```

A text line looks like this:

```
2026-09-11 15:04:50 INFO  [packer] build started host=cortex run=42 target=arm64
```

and the JSON lines sink writes one object per line with `time`,
`epoch`, `level`, `logger`, `message` and every context and record
field flattened in.

## API

| Call | What it does |
|------|--------------|
| `NEW(name$)` | A logger. |
| `LEVEL(lg, level$)` | The threshold: `DEBUG`, `INFO`, `WARN` or `ERROR`. Records below it are dropped before any sink sees them. |
| `CONTEXT(lg, fields)` | Merged into every record. |
| `TO_CONSOLE(lg, [colour])` | Coloured lines on stdout; `FALSE` for plain. |
| `TO_FILE(lg, path$, [max_bytes], [keep])` | One text line per record. |
| `TO_JSONL(lg, path$, [max_bytes], [keep])` | One JSON object per record. |
| `DEBUG` / `INFO` / `WARN` / `ERROR(lg, message$, [fields])` | A record at that level. |
| `LOG(lg, level$, message$, [fields])` | The same with the level as text. |
| `RECORD(lg, level$, message$, [fields])` | The record map a sink receives. |
| `LINE$(rec)` / `JSON$(rec)` | The two forms of a record. |

## Rotation

A file sink with `max_bytes` rotates when the next line would push the
file past the limit: the file becomes `name.1`, `name.1` becomes
`name.2` and so on up to `keep`, and the oldest is dropped. Files are
moved by read, write and delete, since the core has no rename builtin.

## A sink of your own

`RECORD` builds the same map the built-in sinks receive, so a custom
sink is a SUB that takes it:

```basic
DIM problems = []
SUB Remember(rec)
    IF rec{"level"} = "WARN" OR rec{"level"} = "ERROR" THEN PUSH(problems, rec)
ENDSUB
Remember(LOGGER.RECORD(lg, "WARN", "job slow", {"job": "upload"}))
```

## Tests and demo

- `tests/jdlibs/logger_selftest.jdb` (rotation across three files, the JSON sink parsed back)
- `jdb/demos/jdlibs/logger_demo.jdb` (four sinks, a rotation you can watch, counts per level)
