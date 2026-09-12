# DT - dates as numbers, with a calendar

`lib/dt.jdb` parses the date formats that turn up in files and APIs,
does calendar arithmetic, rounds to units, builds ranges and writes
relative text, all on plain numbers: seconds since 1970-01-01 UTC,
negative before it, fractional below the second. The calendar is
proleptic Gregorian, so 1969 and 1900 are ordinary dates.

Stands in for: dateutil, arrow.

## Quick start

```basic
IMPORT DT

DIM t = DT.PARSE("2026-09-12T15:00:00+02:00")      ' 1789218000
PRINT DT.ISO$(t, 2)                                ' 2026-09-12T15:00:00+02:00
PRINT DT.FMT$(DT.ADD(t, "+1 month -2 days"), "%d.%m.%Y")
PRINT DT.HUMANIZE$(t, DT.NOW(), "de")              ' vor 3 Stunden
DIM mondays = DT.RANGE(DT.STARTOF(t, "week"), DT.ADD(t, "8 weeks"), "1 week")
```

## Offsets

Every function takes an optional fixed offset in hours (`2` for UTC+2,
`-5.5` for UTC-5:30) that names the wall clock to read or write; without
it the clock is UTC. `DT.LOCAL_OFFSET()` answers the machine's current
offset. There is no time zone database.

## API

| Call | What it does |
|------|--------------|
| `PARSE(text$, [offset])` | ISO 8601 (`2026-09-12`, `2026-09-12T15:00:00.25Z`, `+02:00`, `+0200`), RFC 2822 (`Sat, 12 Sep 2026 15:00:00 +0200`, `GMT`), German `12.09.2026 15:00`, a plain epoch number. `NONE` for anything else, including a day that does not exist. A time without an offset is read in `offset`. |
| `MAKE(yy, mm, dd, [hh, mi, ss], [offset])` | The instant for a wall-clock date and time. |
| `PARTS(t, [offset])` | `year month day hour minute second fraction weekday yday`; weekday 0 is Sunday. |
| `FMT$(t, fmt$, [offset])` | strftime style: `%Y %y %m %d %e %H %I %M %S %p %j %a %A %b %B %z %s %F %T %%`. |
| `ISO$(t, [offset])` / `RFC$(t, [offset])` | `2026-09-12T15:04:05+02:00` (`Z` at offset zero) and `Sat, 12 Sep 2026 15:04:05 +0200`. |
| `WEEKDAY$(t, [lang$], [offset])` / `MONTHNAME$(...)` | Names in `en` or `de`. |
| `ADD(t, spec$, [offset])` | `"+1 month -2 days 3h"`, `"2w"`, `"1.5 hours"`. Units: `s sec second(s)`, `min minute(s)`, `h hour(s)`, `d day(s)`, `w week(s)`, `mo month(s)`, `y year(s)`. Months and years move the wall clock of `offset` and clamp the day; the rest are exact seconds. |
| `DIFF(a, b, unit$, [offset])` | From `a` to `b`: seconds, minutes, hours, days, weeks as an exact number; months and years as whole calendar units, negative when `b` lies before `a`. |
| `STARTOF(t, unit$, [offset])` / `ENDOF(...)` | `minute hour day week month quarter year`; weeks start on Monday; `ENDOF` is the last whole second. |
| `ISOWEEK(t, [offset])` | `{"year", "week"}` of the ISO week. |
| `RANGE(a, b, [step$], [offset])` | Every instant from `a` to `b` inclusive stepping by a spec such as `"1 day"`; counts down when `b` lies before `a`. |
| `HUMANIZE$(t, [ref], [lang$])` | `just now`, `3 hours ago`, `in 2 days`; German with `"de"`: `vor 3 Stunden`, `in 2 Tagen`. `ref` defaults to now. |
| `IS_LEAP(yy)` / `DAYS_IN(yy, mm)` | The calendar facts. |
| `NOW()` / `LOCAL_OFFSET()` | The current instant and the machine's offset in hours. |
| `TODATE(t)` / `FROMDATE(d)` | To and from the core `DateTime` (from 1970 on) for builtins such as `FORMAT_DATE` or `DATERANGE`. |

## Notes

- A DT value compares and subtracts like any number: `b - a` is seconds.
- `DT.RANGE` with a month step keeps the day it reaches: from January
  31st the next steps are February 28th, March 28th and so on, the way
  `ADD` clamps.
- The core `DateTime` starts at 1970; before that use DT alone.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/dt_selftest.jdb`. Demos: `jdb/demos/jdlibs/dt_demo.jdb`,
and with DF and CONSOLE in `sales_dashboard.jdb`, `log_digest.jdb` and
`report_site.jdb`.
