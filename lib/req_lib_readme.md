# REQ - HTTP sessions

`lib/req.jdb` puts everything around an HTTP call on top of the `HTTP.*`
builtins: a session with a base URL, default headers and auth, a cookie
jar, query strings with proper percent encoding, JSON and form bodies,
multipart upload, retries with backoff, and a response map that knows
how to parse itself.

Stands in for: requests.

## Quick start

```basic
IMPORT REQ

DIM api = REQ.NEW("https://api.example.com")
REQ.BEARER(api, token$)
REQ.TIMEOUT(api, 120)
REQ.RETRIES(api, 3, 500)

DIM r = REQ.GET(api, "/users", {"page": 2, "q": "jd basic"})
IF r{"ok"} THEN PRINT REQ.JSON(r){"total"}

DIM created = REQ.POST(api, "/users", {"name": "Ann"})
DIM logged_in = REQ.FORM(api, "/login", {"user": "ann", "pass": "secret"})
DIM up = REQ.UPLOAD(api, "/files", {"note": "scan"}, {"file": "scan.png"})
```

## The response

Every call returns a map:

| Key | Meaning |
|-----|---------|
| `status` | The HTTP status, `0` for a transport failure. |
| `ok` | `TRUE` for a 2xx status. |
| `body` | The body as a string. |
| `headers` | The response headers, names in lowercase. |
| `error` | The reason when `status` is 0. |
| `url` | The URL that was called. |

`REQ.JSON(r)` parses the body and raises when it is not JSON,
`REQ.TEXT$(r)` hands the body back, `REQ.RAISE_FOR_STATUS(r)` raises
with the status and the start of the body unless `ok`.

## API

| Call | What it does |
|------|--------------|
| `NEW(base_url$)` | A session. Paths given to the verbs are appended; a full URL is used as it is. |
| `HEADER(s, name$, value$)` | A default header. |
| `BEARER(s, token$)` / `BASIC(s, user$, pass$)` | The `Authorization` header. |
| `COOKIE(s, name$, value$)` | A cookie in the jar. `Set-Cookie` on a response fills the jar too. |
| `TIMEOUT(s, seconds)` | Connect, read and write timeout; 30 by default. |
| `RETRIES(s, attempts, base_ms)` | `attempts` counts the first try; the wait doubles after each one. |
| `RETRY_ON(s, statuses)` | The status list that triggers a retry; `[429, 500, 502, 503, 504]` by default. A transport failure always does. |
| `GET(s, path$, [query])` / `DEL(s, path$, [query])` | `query` is a map that becomes the query string. |
| `POST(s, path$, [body], [content_type$])` / `PUT` / `PATCH` | A map or array body goes out as JSON, a string as it is. |
| `FORM(s, path$, fields)` | `application/x-www-form-urlencoded`. |
| `UPLOAD(s, path$, fields, files)` | `multipart/form-data`; `files` maps a part name to a path, or to `{"filename", "content", "type"}`. |
| `SEND(s, method$, path$, [body$], [content_type$], [query])` | The generic call the verbs use. |
| `ENCODE$(text$)` | Percent encoding of every byte outside the unreserved set. |
| `QUERY$(map)` | A query string without the leading `?`. |
| `MULTIPART(fields, files)` | The multipart body and content type, without sending. |
| `JSON(r)` / `TEXT$(r)` / `RAISE_FOR_STATUS(r)` | On a response. |

## Notes

- The `HTTP.*` builtins keep one global header, cookie and timeout
  state. Every REQ call installs the session's state before the request
  and clears it afterwards, so two sessions do not leak into each other.
- The default timeout of the builtins is ten seconds. REQ sets its own
  per session; a long call wants `REQ.TIMEOUT(s, 120)` or more.
- A session is a plain map, so `s{"cookies"}` and `s{"headers"}` can be
  inspected or saved.

## Tests and demo

- `tests/jdlibs/req_selftest.jdb` (drives an `HTTP.SERVER` in the same process)
- `jdb/demos/jdlibs/req_demo.jdb` (a small JSON service and the client that uses it)
