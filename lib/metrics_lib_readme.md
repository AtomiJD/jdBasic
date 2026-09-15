# METRICS - counters, gauges, histograms and a /metrics page

`lib/metrics.jdb` keeps counters, gauges and histograms with labels in one
registry and writes them in the Prometheus text exposition format 0.0.4, the
format Prometheus, Grafana Agent and VictoriaMetrics scrape. A pair of JDWEB
middleware functions counts every request and its latency per route pattern.

Stands in for: prometheus_client.

## Quick start

```basic
IMPORT METRICS

DIM jobs = METRICS.NEWCOUNTER("jobs_done", "Jobs finished.", ["queue"])
DIM waiting = METRICS.NEWGAUGE("queue_length", "Items waiting.", ["queue"])
DIM took = METRICS.NEWHISTOGRAM("job_seconds", "Time per job.")

DIM started = METRICS.STARTTIMER()
' ... the job ...
METRICS.OBSERVE_SINCE(took, started)
METRICS.INC(jobs, 1, ["mail"])
METRICS.SETVALUE(waiting, 12, ["mail"])

PRINT METRICS.EXPOSE$()
```

```
# HELP jobs_done_total Jobs finished.
# TYPE jobs_done_total counter
jobs_done_total{queue="mail"} 1.0
...
```

A JDWEB app with request metrics and a `/metrics` route:

```basic
IMPORT JDWEB, METRICS

FUNC CountBefore(request)
    RETURN METRICS.WEB_BEFORE(request)
ENDFUNC

FUNC CountAfter(request)
    RETURN METRICS.WEB_AFTER(request, JDWEB.CURRENT_STATUS())
ENDFUNC

FUNC Scrape(request)
    RETURN METRICS.PAGE(request)
ENDFUNC

METRICS.WEB_SETUP(["/users/:id", "/metrics"])
JDWEB.GET("/users/:id", ShowUser@)
JDWEB.GET("/metrics", Scrape@)
JDWEB.BEFORE(CountBefore@)
JDWEB.AFTER(CountAfter@)
JDWEB.SERVE(8080)
```

## API

A metric is a handle returned by `NEWCOUNTER`, `NEWGAUGE` or `NEWHISTOGRAM`.
Label names are fixed when the metric is made; every call that records passes
the label values in the same order, as an array of text or numbers.

| Call | What it does |
|------|--------------|
| `NEWCOUNTER(name$, doc$, [label_names])` | A counter. The exposition name ends in `_total`, added when the name does not end in it already. |
| `NEWGAUGE(name$, doc$, [label_names])` | A gauge. |
| `NEWHISTOGRAM(name$, doc$, [label_names], [bucket_list])` | A histogram with rising bucket bounds; by default the Prometheus client's `0.005` to `10` seconds. `+Inf` is always added. |
| `INC(metric, [amount], [label_values])` | Adds `amount` (1) to a counter or a gauge. A counter refuses a negative amount. |
| `DEC(metric, [amount], [label_values])` | Subtracts `amount` (1) from a gauge. |
| `SETVALUE(metric, reading, [label_values])` | Sets a gauge. |
| `OBSERVE(metric, reading, [label_values])` | Counts an observation in the first bucket whose bound it does not exceed, and adds it to the sum. |
| `STARTTIMER()` / `OBSERVE_SINCE(metric, started, [label_values])` | Observes the seconds since `STARTTIMER`. |
| `VALUE(metric, [label_values])` | The value of a counter or a gauge, the sum of a histogram; 0 for label values not used yet. |
| `OBSERVATIONS(metric, [label_values])` | The number of observations of a histogram. |
| `EXPOSE$()` | Every metric in the text format: `# HELP`, `# TYPE`, then the samples in the order their label values were first used. |
| `CONTENT_TYPE$()` | `text/plain; version=0.0.4; charset=utf-8`. |
| `PAGE(request)` | A response with the exposition and its content type, for a route. |
| `FORMATVALUE$(x)` | A number as the Prometheus client writes it. |
| `RESET()` | Forgets every metric. |

### JDWEB

| Call | What it does |
|------|--------------|
| `WEB_SETUP(patterns)` | Makes `http_requests_total{method,route,status}` and `http_request_duration_seconds{method,route}`. `route` is the first of the given JDWEB patterns the path matches (`/users/:id`), or `other`, so a path with ids in it does not make a series per id. |
| `WEB_BEFORE(request)` | A before middleware: notes the start, the method and the route, and lets the request go on. |
| `WEB_AFTER(request, status)` | An after middleware, called with `JDWEB.CURRENT_STATUS()`: counts the request and observes its latency. |

JDWEB calls middleware as functions of the app, so the app wraps the two
calls in `FUNC`s as in the quick start.

## Notes

- Names follow the Prometheus rules: a metric name matches
  `[a-zA-Z_:][a-zA-Z0-9_:]*`, a label name `[a-zA-Z_][a-zA-Z0-9_]*` and does
  not start with `__`; a histogram has no label `le`. Anything else, a name
  used twice, the wrong number of label values or buckets that do not rise
  raises an error.
- Label values escape backslash, double quote and newline; `# HELP` text
  escapes backslash and newline. Label names are written sorted, `le`
  among them, the way the Python client writes them.
- A metric without labels has its sample from the start, at 0.
- Values are written like prometheus_client does: `3.0`, `13.524000000000001`,
  `1e-05`, and a positive number with seven or more integer digits in
  exponent form (`1.00505e+06`).
- The registry lives in module globals. An `ASYNC FUNC` works on its own
  copy of the globals, so what it records never reaches the registry a
  scrape reads; record in the task that serves `/metrics`.
- No summaries and no `_created` samples.
- The self test compares an exposition byte for byte with the one
  prometheus_client 0.26 writes for the same operations
  (`PROMETHEUS_DISABLE_CREATED_SERIES=True`), and 26 numbers with its
  `floatToGoString`. `prometheus_client.parser` reads the exposition of the
  JDWEB middleware and of edge-case values.

## Tests and demo

- `tests/jdlibs/metrics_selftest.jdb`
- `jdb/demos/jdlibs/metrics_demo.jdb` (a small JDWEB app with request metrics, a job counter and a queue gauge, scraped through its own `/metrics`)
