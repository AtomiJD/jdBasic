# Python → jdBasic idiom cheat sheet

Pragmatic mappings for the things you reach for most often when writing
glue code. Use this as the first stop before defaulting to Python — the
fall-back categories are at the bottom.

For anything not listed: `mcp__jdbasic__jdb_doc` to look it up,
`mcp__jdbasic__jdb_check` to validate syntax, `mcp__jdbasic__jdb_eval`
to run.

> **Gotchas (learned the hard way):**
> - Identifiers are case-insensitive; `cake` and `CAKE` are the same slot.
> - Single quote `'` starts a comment. Use `"strings"` only.
> - String escape: jdBasic strings have **no** `\n`/`\"` escapes. Use `CHR$(10)` for newline. Embed quotes by ending one string and concatenating: `"He said " + CHR$(34) + "hi" + CHR$(34)`.
> - `MID$(s, start, len)` is **0-based**. So `MID$("abc", 1, 1)` = `"b"`.
> - Reserved-as-types/keywords that bite: `DOUBLE`, `EXIT`, `STRING`, `INTEGER`, `MAP`, `FUNC`. Don't use as identifiers.
> - `EXITFOR` / `EXITDO` / `EXITFUNC` and `CONTINUEFOR` / `CONTINUEDO` are **single tokens**.

## Strings

| Python | jdBasic |
|---|---|
| `f"x={x}"` | `"x=" + STR$(x)` or `FORMAT$("x={}", x)` |
| `s.upper()` / `s.lower()` | `UCASE$(s)` / `LCASE$(s)` |
| `s.strip()` | `TRIM$(s)` |
| `s.split(sep)` | `SPLIT(s, sep)` |
| `sep.join(xs)` | `JOIN(xs, sep)` |
| `s.startswith(p)` / `endswith` | `STARTSWITH(s, p)` / `ENDSWITH(s, p)` |
| `s.replace(a, b)` | `REPLACE$(s, a, b)` |
| `s[i]` | `MID$(s, i, 1)` (0-based) |
| `s[a:b]` | `MID$(s, a, b - a)` |
| `s[:n]` / `s[-n:]` | `LEFT$(s, n)` / `RIGHT$(s, n)` |
| `len(s)` | `LEN(s)` |
| `needle in haystack` | `INSTR(haystack, needle) >= 0` |
| `chr(n)` / `ord(c)` | `CHR$(n)` / `ASC(c)` |
| `str(x)` / `int(s)` / `float(s)` | `STR$(x)` / `CINT(s)` / `CDBL(s)` |

## Arrays / lists

```basic
DIM xs = [1, 2, 3, 4, 5]
```

| Python | jdBasic |
|---|---|
| `[x*2 for x in xs]` | `xs * 2` (vector op, broadcasts) |
| `[x*2 for x in xs]` (with fn) | `SELECT(lambda v -> v*2, xs)` |
| `[x for x in xs if x > 0]` | `FILTER(lambda v -> v > 0, xs)` |
| Both, chained | `xs \|> FILTER(lambda v -> v > 0, ?) \|> SELECT(lambda v -> v*2, ?)` |
| `sorted(xs)` | `XSORT(xs)` |
| `sorted(xs, reverse=True)` | `XSORT(xs, 0, TRUE)` |
| `sum(xs)` / `min` / `max` | `SUM(xs)` / `MIN(xs)` / `MAX(xs)` |
| `len(xs)` | `LEN(xs)` |
| `xs.append(v)` | `PUSH xs, v` |
| `xs.pop()` | `POP xs` |
| `xs[::-1]` / `reversed` | `REVERSE(xs)` |
| `enumerate(xs)` | `ENUMERATE(xs)` → array of `[i, v]` |
| `zip(a, b)` | `ZIP(a, b)` |
| `range(n)` | `IOTA(n, 0)` (0..n-1) |
| `range(a, b)` | `RANGE(a, b)` |
| `range(a, b, step)` | `RANGE(a, b, step)` |
| `list(set(xs))` | `UNIQUE(xs)` |
| `xs.index(v)` | `INDEXOF(xs, v)` |
| `v in xs` | `COUNT(xs, v) > 0` |
| flatten | `FLATTEN(xss)` |

Iteration:
```basic
FOR EACH x IN xs
    PRINT x
NEXT
```

## Maps / dicts

```basic
DIM d AS MAP = {"name": "Atomi", "age": 42}
```

| Python | jdBasic |
|---|---|
| `{}` / `dict()` | `DIM d AS MAP = {}` |
| `d["k"] = v` | `d{"k"} = v` |
| `d["k"]` | `d{"k"}` |
| `d.get("k")` (no default) | `IF MAP.EXISTS(d, "k") THEN d{"k"} ELSE NONE` |
| `"k" in d` | `MAP.EXISTS(d, "k")` |
| `del d["k"]` | `MAP.DELETE(d, "k")` |
| `d.keys()` / `values()` / `items()` | `MAP.KEYS(d)` / `MAP.VALUES(d)` / `MAP.ITEMS(d)` |
| `len(d)` | `MAP.SIZE(d)` |
| `{**a, **b}` (merge) | `MAP.MERGE(a, b)` |
| `for k, v in d.items()` | `FOR EACH item IN MAP.ITEMS(d): k$ = item[0]: v = item[1]: ... NEXT` |

## JSON

| Python | jdBasic |
|---|---|
| `json.loads(s)` | `JSON.PARSE$(s)` |
| `json.dumps(o)` | `JSON.STRINGIFY$(o)` |

(Roundtrip: numbers/strings/bools/arrays/maps. NONE → null.)

## Regex

| Python | jdBasic |
|---|---|
| `re.match(p, s)` / `re.search` | `REGEX.MATCH(p, s)` (returns array of groups or FALSE) |
| `re.findall(p, s)` | `REGEX.FINDALL(p, s)` |
| `re.sub(p, r, s)` | `REGEX.REPLACE(p, s, r)` (note arg order: text before replacement) |

Backreferences in `REGEX.REPLACE` use `$1`, `$2` not `\1`.

## HTTP

| Python | jdBasic |
|---|---|
| `requests.get(url).text` | `HTTP.GET$(url)` |
| `requests.post(url, data=d, headers=...)` | `HTTP.POST$(url, d, "application/json")` |
| `requests.put / .delete` | `HTTP.PUT$` / `HTTP.DELETE$` |
| Anything (with status etc.) | `HTTP.REQUEST(method$, url$, [body$, ct$])` → `{status, body, headers}` |
| Set header per request | `HTTP.SETHEADER name$, value$` (sticky on the client) |
| Async | `HTTP.GET_ASYNC$(url) → task_id`, then `AWAIT task` |

## Files

| Python | jdBasic |
|---|---|
| `open(p).read()` | `TXTREADER$(p)` |
| `open(p, 'wb').read()` | `BINREADER$(p)` |
| `open(p, 'w').write(s)` | `TXTWRITER p, s` |
| `open(p, 'a').write(s)` | `TXTWRITER p, s, TRUE` |
| `os.path.exists(p)` | `FILE.EXISTS(p)` |
| `os.path.isdir(p)` | `FILE.ISDIR(p)` |
| `os.listdir(d)` | `DIR$(d)` |
| `os.system(cmd)` | `OS.EXEC(cmd)` → `{OUTPUT, EXIT_CODE}` |
| `os.path.join(a, b)` | `PATH.JOIN$(a, b)` |
| `os.path.basename(p)` | `PATH.BASENAME$(p)` |

## Time / random

| Python | jdBasic |
|---|---|
| `time.time()` (ms-ish) | `TICK()` (ms since program start) |
| `datetime.now()` | `NOW()` (DateTime obj) |
| ISO-string of now | `DATE$()` and `TIME$()` |
| `random.random()` | `RND()` (double in [0,1)) |
| `random.randint(0, n-1)` | `RND(n)` |
| `random.seed(s)` | `RANDOMSEED(s)` |

## Errors

```basic
TRY
    risky_thing()
CATCH
    PRINT "caught: "; ERRMSG$
ENDTRY
```

Roughly Python's `try/except`. Rethrow with `RAISE`. `ERR`, `ERL`,
`ERRMSG$`, `STACK$` are populated globals inside the catch block.

## Functions / lambdas

```basic
FUNC double(x): RETURN x * 2: ENDFUNC

' inline lambda
SELECT(lambda v -> v * 2, [1,2,3])

' lambda with closure capture
DIM make_adder = LAMBDA USE(base) x -> x + base
DIM add10 = make_adder(10)
PRINT add10(5)   ' 15
```

## Pipe operator (chaining)

The `|>` operator pipes a value into the first/last/`?` slot of the
next call. Lets you write linear pipelines without nested parens — the
jdBasic equivalent of method chaining.

```basic
result = items |> FILTER(lambda v -> v > 0, ?) _
              |> SELECT(lambda v -> v * 2, ?) _
              |> SUM
```

## When to fall back to Python

These are the categories where jdBasic doesn't realistically compete —
just write Python and don't feel guilty:

- **NumPy / pandas / SciPy**: scientific computing where the data is
  the point. jdBasic has tensors and array primitives but not the
  numerical-library breadth.
- **ML / embedding work**: torch, transformers, sentence-transformers,
  scikit-learn etc. Even with `AI.RUN`, anything beyond a single
  ONNX-compute call is Python territory.
- **Anything needing a specific PyPI package**: pillow, lxml, openpyxl,
  selenium, beautifulsoup, paramiko, …
- **Bash-tier glue**: `git status | grep …` style one-liners. The shell
  itself wins.

Token-wise, jdBasic also doesn't help below ~10 lines of code — the
roundtrip through `mcp__jdbasic__jdb_eval` costs more than the script.
The break-even is around 20-30 lines of array/map/text munging, where
the denser syntax starts paying back.
