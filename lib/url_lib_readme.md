# URL - URLs taken apart, built, encoded, resolved and changed

`lib/url.jdb` does what is otherwise done with string slicing: it splits a
URL into its parts and builds one from them, percent-encodes and decodes,
turns query strings into maps and back, resolves a link against the page
it was found on the way RFC 3986 prescribes, brings two spellings of one
URL to the same text, and changes one query parameter while leaving the
others as they were written.

A URL is a string; `PARSE` gives the map of its parts. Nothing is kept
between calls.

Stands in for: urllib.parse, furl.

## Quick start

```basic
IMPORT URL

DIM u = URL.PARSE("https://ann@example.com:8443/docs/guide/?lang=en#install")
PRINT u{"host"}; " "; u{"port"}; " "; u{"path"}; " "; u{"fragment"}

PRINT URL.RESOLVE$("https://example.com/docs/guide/", "../api/index.html?v=2")
' https://example.com/docs/api/index.html?v=2

DIM params AS MAP
params{"q"} = "rock & roll"
params{"tag"} = ["live", "1969"]
PRINT URL.JOINPATH$("https://api.example.com/v1", "search") + "?" + URL.QUERY$(params)
' https://api.example.com/v1/search?q=rock%20%26%20roll&tag=live&tag=1969

PRINT URL.SETPARAM$("https://example.com/list?page=1&sort=name", "page", "2")
' https://example.com/list?page=2&sort=name
```

## API

### Parts

| Call | What it does |
|------|--------------|
| `PARSE(url$)` | The parts as written: `scheme`, `user`, `password`, `host`, `port`, `path`, `query`, `fragment`, and the flags `has_authority`, `has_query`, `has_fragment`, which tell an empty part from a missing one. |
| `BUILD$(parts)` | The URL of a map of parts. A missing key counts as empty, a port may be a number, and a path without its leading slash gets one when there is a host. |
| `ISABSOLUTE(url$)` | Whether it starts with a scheme. |
| `ORIGIN$(url$)` | `scheme://host[:port]`, lowered and without a default port; `""` without a host. |

`PARSE` keeps every part as it was written: the host is not lowered and
the path is not decoded. An IPv6 host keeps its brackets.

### Encoding

| Call | What it does |
|------|--------------|
| `ENCODE$(text$, [keep$])` | Percent-encodes every byte outside the unreserved set of RFC 3986 (`A-Z a-z 0-9 - . _ ~`) and outside `keep$`, with upper case hex. |
| `DECODE$(text$)` | Every `%XX`, in either case, back to its byte. A `%` that starts no escape stays. |
| `FORMENCODE$(text$)` / `FORMDECODE$(text$)` | The encoding of HTML forms, where a space is `+`. |

A string is bytes, so a UTF-8 character becomes one escape per byte:
`Grüße` is `Gr%C3%BC%C3%9Fe`.

### Query strings

| Call | What it does |
|------|--------------|
| `QUERYMAP(query$)` | A query string as a map; a leading `?` is allowed. Keys and values are decoded with `+` read as a space. A repeated key gives an array of its values in order; a key without `=` has the value `""`. |
| `QUERY$(params)` | A map as a query string, keys in the map's order. An array repeats its key; numbers and booleans are written as text. |

### Parameters of a URL

| Call | What it does |
|------|--------------|
| `PARAM$(url$, key$, [fallback$])` | The first value of a parameter, or the fallback. |
| `PARAMS(url$, key$)` | Every value of it, in order. |
| `SETPARAM$(url$, key$, value$)` | One value for the parameter, in the place of its first occurrence or added at the end. |
| `DROPPARAM$(url$, key$)` | Every occurrence taken out, and the `?` when nothing is left. |
| `JOINPATH$(url$, segment$)` | One more path segment, encoded as one segment (a `/` inside it becomes `%2F`), joined with one slash, before the query. |

The parameter calls leave every other parameter exactly as it was
written, escapes and order included, and keep the fragment.

### Resolving and normalising

| Call | What it does |
|------|--------------|
| `RESOLVE$(base$, ref$)` | A reference resolved against a base URL by the algorithm of RFC 3986 section 5.2: every example of section 5.4, the abnormal ones included, gives the answer the standard lists. |
| `REMOVEDOTS$(path$)` | A path with its `.` and `..` segments taken out (section 5.2.4). |
| `CANONICAL$(url$)` | The form two spellings of one URL share: scheme and host lowered, a default port (80, 443, 21) dropped, dot segments removed, an empty path with a host made `/`, escapes of unreserved characters decoded and every other escape written with upper case hex. |

`RESOLVE$` is strict: a reference with a scheme is taken as it is, so
`http:g` stays `http:g`.

## Notes

- Nothing here touches the network; REQ does that and takes the URLs
  these calls build.
- A query written with `QUERY$` uses `%20` for a space, as REQ does.
  `QUERYMAP` reads both `%20` and `+`.
- Internationalised domain names are left as they are written; there is
  no punycode conversion.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/url_selftest.jdb`.
Demo: `jdb/demos/jdlibs/url_demo.jdb`.
