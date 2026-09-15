# POOL - worker pools, fan-out and fan-in

`lib/pool.jdb` spreads jobs over ASYNC worker tasks and collects the
answers in input order. Each job ends in one of four states: ok, error,
timeout or cancelled. The module also has WAITALL and WAITANY over task
handles, a token-bucket rate limiter and a fan-in over several channels.
It is plain jdBasic on top of `ASYNC FUNC`, `AWAIT`, `THREAD.ISDONE` and
the `CHAN.*` natives. It runs the same interpreted and compiled with
`-c`.

The Python counterparts are `concurrent.futures.ThreadPoolExecutor.map`,
`multiprocessing.Pool` and `asyncio.gather` / `asyncio.wait`, plus a
token bucket such as `aiolimiter`.

```basic
IMPORT POOL

FUNC Square(x)
    IF x < 0 THEN THROW "negative item"
    RETURN x * x
ENDFUNC

ASYNC FUNC SquareWorker(jobs, results, items)
    DO
        DIM idx = POOL.TAKE_JOB(jobs, results)
        IF idx < 0 THEN EXITDO
        TRY
            DIM answer = Square(items[idx])
            POOL.DONE(results, idx, answer)
        CATCH
            POOL.FAILED(results, idx, ERRMSG$)
        ENDTRY
    LOOP
    RETURN 0
ENDFUNC

DIM items = [3, 1, -4, 1, 5]
DIM p = POOL.CREATE(LEN(items))
DIM w = 0
FOR w = 1 TO 4
    POOL.SPAWN(p, SquareWorker(POOL.JOBS(p), POOL.RESULTS(p), items))
NEXT w
PRINT POOL.GATHER(p, 500, FALSE)   ' 4
PRINT POOL.VALUES(p)               ' [9, 1, 0, 1, 25]
PRINT POOL.STATES(p)               ' [ok, ok, error, ok, ok]
PRINT POOL.REASON$(p, 2)           ' negative item
POOL.SHUTDOWN(p)
```

## Why the worker is your own ASYNC FUNC

An `ASYNC FUNC` runs on its own OS thread with a fresh copy of the
program's globals and functions. Only channels are shared. This has
three consequences for the pool:

- **Globals are copies.** A worker sees the globals as they were when it
  was started. Its writes never reach the main program, and later changes
  in the main program never reach the worker. Everything a worker needs
  goes in as a parameter (`items` above), and everything it produces
  comes back through the results channel. The pool state (job states,
  values, reasons) lives in the main program's copy of the module.
  `GATHER`, `STATE$`, `VALUES` and the other readers are only meaningful
  there.
- **No funcrefs inside ASYNC.** A funcref passed to an ASYNC FUNC cannot
  be called on the worker thread in either backend. The interpreter
  answers "Undefined function". The compiler rejects it or produces wrong
  results. So there is no `POOL.MAP(fn@, items)`. You write one named
  `ASYNC FUNC` worker, which may call any named FUNC of the program, and
  POOL handles the queue, the bookkeeping and the order.
- **Only scalars cross into module helpers.** Inside a worker the POOL
  helpers take channels, numbers and strings: `TAKE_JOB`, `DONE`,
  `DONE_TEXT`, `FAILED`, `ACQUIRE`. Jobs travel as job numbers. The data
  stays in the arrays you hand to the worker.

## Pool

| Call | Answers |
| --- | --- |
| `POOL.CREATE(n_items)` | a pool handle. Job numbers 0 to n-1 are queued and the job channel is closed. |
| `POOL.JOBS(p)` / `POOL.RESULTS(p)` | the two channels to pass to each worker |
| `POOL.SPAWN(p, task)` | registers a started worker task |
| `POOL.TASKS(p)` | the worker tasks of the pool |
| `POOL.GATHER(p, [timeout_ms], [fail_fast])` | collects until every job is resolved, then answers the ok count |
| `POOL.CANCEL(p)` | drains the queue, marks those jobs cancelled and answers how many |
| `POOL.SHUTDOWN(p)` | joins every worker, including one still busy with a timed-out job, and answers how many |
| `POOL.STATE$(p, i)` / `POOL.STATES(p)` | `pending`, `running`, `ok`, `error`, `timeout` or `cancelled` |
| `POOL.VALUE(p, i)` / `POOL.VALUES(p)` | numeric results in input order (0 where a job did not come back ok) |
| `POOL.VALUE_TEXT$(p, i)` | a text result |
| `POOL.REASON$(p, i)` | the error message, `timed out after N ms` or `cancelled` |
| `POOL.TOTAL(p, state$)` | how many jobs are in one state |

Inside a worker:

| Call | Does |
| --- | --- |
| `POOL.TAKE_JOB(jobs, results)` | the next job number, or -1 when the queue is empty and closed. Reports the job as started. |
| `POOL.CLAIM_JOB(jobs, results)` | the same, but the job only counts as running and its timeout has not started yet |
| `POOL.STARTED(results, idx)` | starts the timeout of a claimed job now |
| `POOL.DONE(results, idx, value)` | a numeric result |
| `POOL.DONE_TEXT(results, idx, text$)` | a text result |
| `POOL.FAILED(results, idx, message$)` | an error for this job only |

### Errors, fail fast and cancellation

An error belongs to its job: the other jobs keep running, and `GATHER`
with `fail_fast = FALSE` collects them all. With `fail_fast = TRUE` the
first error drains the queue. Jobs that were not started become
`cancelled`, and jobs already running still report. `CANCEL(p)` does the
same from the outside. A worker ends once the queue is empty and closed,
so cancellation needs no extra signal.

### Timeouts

The timeout of a job starts when a worker takes it (`TAKE_JOB`). Waiting
in the queue does not count. A worker that has to wait before the real
work, for a rate-limit token for example, takes the job with `CLAIM_JOB`
and calls `STARTED` when the work begins. The demo does this. A job that is still running after
`timeout_ms` becomes `timeout`, and its answer is dropped if it comes
later. A thread cannot be killed, so the worker keeps working on the
slow job. `GATHER` still returns on time. `SHUTDOWN` waits for that
worker, so no task is left behind. With a timeout set, `GATHER` looks at
every running job each time it wakes up, so keep pools with timeouts in
the thousands of jobs, not the millions.

## Waiting on tasks

| Call | Answers |
| --- | --- |
| `POOL.WAITALL(tasks, [timeout_ms])` | TRUE once every task is done, FALSE when the timeout passes first |
| `POOL.WAITANY(tasks, [timeout_ms])` | the index of the first task found done, -1 on timeout or for no tasks |

Both poll `THREAD.ISDONE` every 2 ms. A timeout of 0 (the default) waits
as long as it takes.

## Rate limiter

```basic
DIM lim = POOL.LIMITER(20, 4)        ' 20 tokens a second, bucket of 4
DIM tokens = POOL.TOKENS(lim)        ' hand this channel to workers
IF POOL.ACQUIRE(tokens, 1000) THEN ...
PRINT POOL.LIMITER_STOP(lim)         ' tokens issued
```

The bucket is a channel with the burst as its capacity. The bucket's
own ASYNC task refills it every `1000 / per_second` ms and starts it
full. Because it is a channel, `ACQUIRE` works the same in the main
program and inside any number of workers, which share one budget.
`LIMITER_STOP` closes the channel and joins the task. A later `ACQUIRE`
drains what was left in the bucket, then answers FALSE.
`POOL.LIMITER_TASK(lim)` gives the task handle.

## Fan-in

```basic
DIM origin = POOL.FANIN([left, right], 2000)
DO WHILE origin >= 0
    PRINT origin, POOL.FANIN_VALUE()
    origin = POOL.FANIN([left, right], 2000)
LOOP
```

`FANIN(sources, [timeout_ms])` waits on all channels with `CHAN.SELECT`
and takes one value. It answers the index of the channel the value came
from, -1 on timeout, or -2 once every channel is closed and drained. The
value is read with `FANIN_VALUE()` for numbers and `FANIN_TEXT$()` for
strings. Both are kept in the module, so read them before the next
`FANIN` on the same thread. Each channel keeps its own order.

## Traps

- **Numbers come back as doubles.** Values that went through a channel
  are FLOAT64 in the compiled program, so job numbers need `CLNG` before
  they are used as an integer. `TAKE_JOB` already answers an integer.
- **Compute first, then report.** In a compiled program,
  `POOL.DONE(results, idx, Work(x))` still calls DONE when `Work` throws,
  and it sends 0. Write `DIM answer = Work(x)` first and then call DONE
  inside the TRY.
- **Scalars through module helpers.** In a compiled program, a
  channel-received array handed to a module FUNC can crash, and a
  received string put straight into an array cell or returned from a
  module FUNC can arrive empty. POOL works around this internally, and
  your worker code should follow the same message shapes.
- **One results channel per pool.** It is sized so that workers never
  block on it, even after `GATHER` has returned.
