# SCHED - cron expressions and a job loop

`lib/sched.jdb` answers when something should run and runs it: cron
expressions with five fields, the `@hourly` family, plain intervals and
daily times; the next and previous run times in a fixed offset; and a
job loop that runs what is due through RETRY, logs through LOGGER,
never starts a job while it still runs, can hold a lock file, and
follows a policy for runs it missed. Times are DT numbers: seconds since
1970-01-01 UTC.

Stands in for: croniter, schedule, the job loop part of APScheduler.

## Quick start

```basic
IMPORT SCHED, DT, LOGGER

PRINT DT.ISO$(SCHED.NEXTRUN("30 7 * * mon-fri", DT.NOW(), 2), 2)

FUNC NightlyReport(due_at)
    ' build and mail the report for DT.ISO$(due_at)
    RETURN 1
ENDFUNC

DIM lg = LOGGER.NEW("jobs")
LOGGER.TO_FILE(lg, "jobs.log")
SCHED.LOGTO(lg)
SCHED.JOB("report", "daily 06:00", NightlyReport@, {"offset": 2, "attempts": 3, "delay": 5000})
SCHED.JOB("ping", "every 5 minutes", CheckService@, {"policy": "skip"})
DIM runs = SCHED.SERVE(0)
```

## Expressions

A schedule is a string.

| Shape | Meaning |
|-------|---------|
| `m h dom mon dow` | Cron: minute 0-59, hour 0-23, day of month 1-31, month 1-12 or `JAN`-`DEC`, weekday 0-7 or `SUN`-`SAT` (0 and 7 are Sunday). |
| `*`, `5`, `1-5`, `1,15,30`, `*/15`, `10-50/20`, `0/20` | Every value, one, a range, a list, steps over all, steps over a range, steps from a start. |
| `L` in the day field | The last day of the month (`0 18 L * *`). |
| `@hourly` `@daily` `@midnight` `@weekly` `@monthly` `@yearly` `@annually` | The usual shorthands. |
| `daily HH:MM`, `weekdays HH:MM`, `weekends HH:MM`, `weekly DAY HH:MM` | Times of day, e.g. `weekly fri 16:00`. |
| `every <length>` | A fixed interval from seconds to weeks in DT.ADD's words (`every 15 minutes`, `every 90s`, `every 6 hours`), counted from midnight 1970-01-01 of the offset's wall clock, so `every 15 minutes` falls on the quarter hours. Months and years are refused. |

When both the day of month and the weekday are restricted, a day
matching either one runs (`0 0 13 * 5` is every 13th and every Friday),
as in cron and croniter; when one of them is `*`, both must match.

## Run times

`offset` is the wall clock the expression is read in, in hours east of
UTC (`2` for Berlin in summer, `-5.5`); there is no time zone database,
so a job across a daylight saving change keeps the offset it was given.

| Call | What it does |
|------|--------------|
| `NEXTRUN(when$, t, [offset])` | The first run time strictly after `t`. |
| `PREVRUN(when$, t, [offset])` | The last run time strictly before `t`. |
| `UPCOMING(when$, t, n, [offset])` | The next `n` run times. |
| `MATCHES(when$, t, [offset])` | TRUE when `t` falls in a run minute (a run second for an interval). |
| `VALID(when$)` / `PROBLEM$(when$)` | Whether the expression reads, and the reason when it does not (`SCHED: hour value 99 is outside 0-23`). |

`NEXTRUN` and `PREVRUN` throw for an expression that does not read and
for one that never matches within 28 years (`0 0 30 2 *`).

## Job loop

| Call | What it does |
|------|--------------|
| `JOB(name$, when$, fn, [opts])` | Adds a job, or replaces the one of that name. `fn` is called with the run time it is due for. |
| `RUNDUE(now)` | Runs every job due at the instant `now` and answers how many runs started. Drive it from an own loop or a test clock. |
| `SERVE(end_at)` | Runs jobs on the real clock until `end_at` (0 for no end) or `HALT`, sleeping until the next one is due. Answers the runs started. |
| `HALT()` | Ends `SERVE` after the runs in progress; a job may call it. |
| `JOBS()` / `DUE(name$)` | The job names and a job's next run time. |
| `STATE(name$)` | `runs`, `fails`, `skips`, `next`, `last_due`, `last_error`, `when`, `policy`. |
| `CANCEL(name$)` / `CANCELALL()` | Removes jobs. |
| `LOGTO(lg)` | Logs `job ran` (INFO), `job failed` with the error (ERROR) and skips (WARN) to a LOGGER, with the fields `job`, `due`, `attempts`, `error`. |

| Option | Default | What it does |
|--------|---------|--------------|
| `offset` | 0 | The wall clock of `when$`. |
| `policy` | `"once"` | What happens to runs that were missed while nothing called `RUNDUE` (a sleeping machine, a long job): `once` makes one run for the latest missed time, `all` runs every missed time oldest first (up to 1000), `skip` drops a run that is later than `grace`. |
| `grace` | 60 | Seconds a run under `skip` may be late. |
| `attempts`, `delay` | 1, 1000 | Passed to `RETRY.ATTEMPT`: tries per run and the first wait in milliseconds, doubling. A run fails when every attempt throws. |
| `lock`, `lock_ttl` | none, 3600 | A lock file holding the start time. A run does not start while the file is younger than `lock_ttl` seconds, which keeps two processes from running the same job; an older file counts as stale and is taken over. The file is removed after the run. |
| `start` | now | The first run is the first run time after it. |

A job runs to its end before the loop goes on, so it never runs twice at
once: a job that calls `RUNDUE` from inside is left out of that call,
and the starts it missed meanwhile follow its policy.

## Notes

- Next and previous run times were checked against croniter 6.2.4 for 35
  expressions from five starts in UTC and two in fixed offsets
  (`tests/jdlibs/fixtures/sched_croniter.tsv`): five runs after and three
  before each, 245 rows.
- The lock file is written after a check for it, not atomically: two
  processes starting within the same instant can both run.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/sched_selftest.jdb`. Demo: `jdb/demos/jdlibs/sched_demo.jdb`.
