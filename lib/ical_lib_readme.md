# ICAL - calendars and recurrence rules

`lib/ical.jdb` reads and writes iCalendar (`.ics`) files: the components
VCALENDAR, VEVENT, VTODO, VALARM and VTIMEZONE with their properties and
parameters, line folding and text escaping, date-times in UTC, in a named
zone or in a zone the file defines. Recurrence rules expand into the
occurrences of a range, less EXDATE and moved occurrences. Calendars
written by ICAL fold at 75 octets, end lines with CRLF and carry the
VTIMEZONE of every zone they use, as Outlook and Google expect.

Stands in for: icalendar, recurring_ical_events, dateutil.rrule.

## Quick start

```basic
IMPORT ICAL, DT, DF

DIM cal = ICAL.PARSEFILE("calendar.ics")
DIM week = ICAL.EXPAND(cal, DT.PARSE("2026-10-19"), DT.PARSE("2026-10-26"))
DF.SHOW(week)                                     ' start, end, summary, uid, event

DIM out = ICAL.NEW()
DIM start = DT.PARSE("2026-10-19T10:00:00+02:00")
DIM ev = ICAL.ADDEVENT(out, "Team meeting", start, start + 3600, "Europe/Berlin")
ICAL.SETPROP(out, ev, "RRULE", "FREQ=WEEKLY;BYDAY=MO,TH;COUNT=10")
ICAL.SETTEXT(out, ev, "LOCATION", "Room 3, second floor")
DIM alarm = ICAL.ADDALARM(out, ev, 15)
ICAL.WRITEFILE(out, "team.ics")
```

Instants are DT values, seconds since 1970 in UTC.

## Reading

| Call | What it does |
|------|--------------|
| `PARSE(text$)` / `PARSEFILE(path$)` | Reads a calendar; answers its handle. CRLF, LF and a byte order mark are all fine. Text without a VCALENDAR throws. |
| `EVENTS(cal)` / `TODOS(cal)` | The VEVENTs or VTODOs of the calendar, in file order. |
| `COMPONENTS(cal, [kind$])` | Every component right inside the calendar, or those of a kind. |
| `CHILDREN(cal, comp, [kind$])` | The components inside one: an event's VALARMs. |
| `KIND$(cal, comp)` | `VEVENT`, `VTODO`, ... The calendar handle itself is a component too, so `PROP$(cal, cal, "X-WR-CALNAME")` reads calendar properties. |
| `PROP$(cal, comp, name$, [fallback$])` | A property's value, text unescaped (`\n`, `\,`, `\;`, `\\`). |
| `RAW$(cal, comp, name$)` | The value as written. |
| `PARAM$(cal, comp, name$, param$, [fallback$])` | A parameter, quotes removed: `PARAM$(cal, ev, "DTSTART", "TZID")`. |
| `VALUES(cal, comp, name$)` | Every value of a property that repeats, such as ATTENDEE or EXDATE. |
| `HAS(cal, comp, name$)` | Whether the property is there. |
| `STARTAT(cal, comp)` / `ENDAT(cal, comp)` | DTSTART, and DTEND, else DUE, else DTSTART plus DURATION; an all-day start ends a day later. |
| `TIMEOF(cal, comp, name$)` | Any date or date-time property as an instant (DTSTAMP, CREATED, RECURRENCE-ID). |
| `ALLDAY(cal, comp)` | Whether the component starts on a date rather than at a time. |

A date-time with `Z` is UTC; one with a TZID is read through the file's
VTIMEZONE of that name, else through the named zones below; a floating
time and a DATE count as UTC, so an all-day event starts at 00:00 UTC of
its date.

## Occurrences

| Call | What it does |
|------|--------------|
| `OCCURRENCES(cal, comp, from, to)` | The starts of a component's occurrences that overlap `[from, to)`: its RRULE and RDATE, without its EXDATEs and without the occurrences another component with the same UID moves through RECURRENCE-ID. |
| `EXPAND(cal, from, to)` | Every event occurrence overlapping the range as a DF frame with the columns `start`, `end`, `summary`, `uid` and `event` (the component), ordered by start. A moved occurrence appears with its own time and summary. |
| `RRULE(rule$, start, from, to, [zone$])` | The starts of a rule by itself in `[from, to)`; `zone$` names the wall clock that repeats. |

Rules repeat the wall clock of their zone, so a 10:00 meeting in Berlin
stays at 10:00 when the clocks change. Supported: `FREQ` DAILY, WEEKLY,
MONTHLY and YEARLY with `INTERVAL`, `COUNT`, `UNTIL` (date or date-time),
`BYDAY` (with ordinals such as `3TU` and `-1SU` in monthly and yearly
rules), `BYMONTHDAY` (negative from the month's end), `BYMONTH`,
`BYSETPOS` (Outlook's "last weekday of the month") and `WKST`. DTSTART is
always the first occurrence. Not supported: `SECONDLY` to `HOURLY`,
`BYYEARDAY`, `BYWEEKNO`, `BYHOUR` and the like, EXRULE, and a
year-level ordinal (`FREQ=YEARLY;BYDAY=20MO` without BYMONTH).

## Zones

| Call | What it does |
|------|--------------|
| `ZONEOFFSET(zone$, t)` | The UTC offset of a named zone at an instant, in hours, as DT takes it: `DT.FMT$(t, "%H:%M", ICAL.ZONEOFFSET("Europe/Berlin", t))`. Throws for an unknown zone. |
| `ZONES()` | The names known without a VTIMEZONE. |

About 90 names: the European zones (UTC, UK, Ireland and Portugal; central
Europe; eastern Europe with Helsinki and Athens; Moscow and Istanbul
without summer time), the US zones with Phoenix and Honolulu, Halifax,
Dubai, Kolkata, Shanghai, Singapore, Tokyo and Brisbane, each under its
IANA name and its Windows name as Outlook writes it (`W. Europe Standard
Time`, `Eastern Standard Time`). Summer time follows today's rules: the
EU changes at 01:00 UTC on the last Sundays of March and October, the US
at 02:00 local on the second Sunday of March and the first of November.
A file's own VTIMEZONE always takes precedence over the list.

## Writing

| Call | What it does |
|------|--------------|
| `NEW([prodid$])` | A calendar with VERSION, PRODID and CALSCALE. |
| `ADDEVENT(cal, summary$, start, finish, [zone$])` | An event with UID and DTSTAMP, in the wall clock of `zone$` (its VTIMEZONE is added) or in UTC. |
| `ADDALLDAY(cal, summary$, day_start, [days])` | An all-day event over `days` days from the UTC date of `day_start`. |
| `ADDTODO(cal, summary$, due, [zone$])` | A VTODO with DUE and STATUS:NEEDS-ACTION. |
| `ADDALARM(cal, comp, minutes_before, [text$])` | A DISPLAY alarm. |
| `SETPROP(cal, comp, name$, value$, [params$])` | Sets a property as written, replacing the first of that name: `SETPROP(cal, ev, "RRULE", "FREQ=WEEKLY;COUNT=4")`. |
| `ADDPROP(cal, comp, name$, value$, [params$])` | Adds one more: `ADDPROP(cal, ev, "EXDATE", ICAL.DATETIME$(t, "Europe/Berlin"), "TZID=Europe/Berlin")`. |
| `SETTEXT(cal, comp, name$, text$)` | Sets a text property, escaped. |
| `DATETIME$(t, [zone$])` | `20261019T080000Z`, or the wall clock of a zone without the Z. |
| `TEXT$(cal)` / `WRITEFILE(cal, path$)` | The calendar as .ics: CRLF, folded at 75 octets without cutting a character, VTIMEZONEs before the events. |

A parsed calendar writes back with every property it had, unknown ones
(`X-MICROSOFT-CDO-*`, `X-ALT-DESC`) included.

## Notes

- Checked against a Google and an Outlook export (`tests/jdlibs/fixtures/ical_google.ics`,
  `ical_outlook.ics`): all 56 occurrences of four months agree with Python's
  icalendar and recurring_ical_events, the rules with dateutil.rrule, and
  1302 offsets of 22 zones around their changes in 2025 to 2027 with
  zoneinfo. A calendar written by ICAL reads back in icalendar with the
  same occurrences.
- In the gap when clocks go forward a wall-clock time that does not exist
  is read with the summer offset; in the hour that repeats in autumn the
  first one is taken.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/ical_selftest.jdb`. Demo: `jdb/demos/jdlibs/ical_demo.jdb`.
