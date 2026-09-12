# RETRY - call it again, with a growing wait

`lib/retry.jdb` runs a call that sometimes fails until it works. The wait
doubles after every failure and can carry a random share so many callers
do not all come back at the same moment. A predicate decides which
errors are worth another attempt, a callback sees each one, and a
ceiling holds the whole run inside a deadline.

Stands in for: tenacity, backoff, urllib3's Retry.

## Quick start

```basic
IMPORT RETRY

DIM answer = RETRY.RUN(Fetch@, url$, {"attempts": 4, "delay": 200})

DIM report = RETRY.ATTEMPT(Fetch@, url$, {"attempts": 3})
IF report{"ok"} THEN PRINT report{"value"} ELSE PRINT report{"error"}
```

## The two ways to run

| Call | What it does |
|------|--------------|
| `RUN(fn, [arg], [opts])` | The value the function answers. When every attempt fails the last error is raised again. |
| `ATTEMPT(fn, [arg], [opts])` | The same run as a report that never raises. |

The report is a map: `ok`, `value`, `error`, `attempts`, `waits` (the
waits that were actually taken) and `elapsed` in milliseconds.

## Options

| Key | Default | What it does |
|-----|---------|--------------|
| `attempts` | 3 | How many times the call may be made. Fewer than one is one. |
| `delay` | 100 | The first wait, in milliseconds. |
| `factor` | 2 | What the wait is multiplied by after each failure. 1 keeps it flat. |
| `jitter` | 0 | The share of a wait that is random, so 0.4 spreads it by up to two fifths either way. |
| `max_ms` | 0 | A ceiling on the waits of the whole run. The last wait is cut to what is left, and the run stops when under a millisecond remains. |
| `retry_if` | every error | A predicate on the message: TRUE means try again. |
| `on_retry` | nothing | Called before each wait with `{attempt, delay, error}`. |

## Planning and judging

| Call | What it does |
|------|--------------|
| `DELAYS([opts])` | The waits a run would take, without taking them. |
| `TRANSIENT(message$)` | TRUE when the message names passing trouble rather than a wrong request. |

`DELAYS` takes the same `attempts`, `delay`, `factor` and `max_ms`, so a
plan can be shown or checked before anything sleeps.

`TRANSIENT` recognises a timeout, a reset or refused connection, a
service that is temporarily away, too many requests, and the 429, 500,
502, 503 and 504 statuses. A wrong request, something that is not there
and a refused key are not worth another attempt.

```basic
DIM report = RETRY.ATTEMPT(Upload@, path$, { _
    "attempts": 5, "delay": 200, "max_ms": 4000, "jitter": 0.3, _
    "retry_if": RETRY.TRANSIENT@, "on_retry": Note@})
```

## Notes

- The waits are in milliseconds throughout, as `SLEEP` counts them.
- `jitter` never makes a wait negative.
- A predicate that says no ends the run at once, so a wrong request
  costs one call and no waiting at all.
- The callback's answer is ignored; it is there to log or to count.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/retry_selftest.jdb`. Demo: `jdb/demos/jdlibs/retry_demo.jdb`.
