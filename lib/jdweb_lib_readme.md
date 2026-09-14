# JDWEB - web apps on HTTP.SERVER

`lib/jdweb.jdb` is the web framework of jdBasic. It adds to `HTTP.SERVER`
what a web app needs: routes with path parameters for any method,
middleware before and after every handler, in-memory sessions, a folder of
static files, JSON and error responses, and a test client that calls the
running app from the same program. It also carries the page chrome of the
terminal "Hot Phosphor" theme and the cookie login that jdTrakr runs on.

Stands in for: Flask, the Werkzeug test client.

## Quick start

```basic
IMPORT JDWEB

FUNC ShowNote(request)
    DIM id$ = JDWEB.PARAM$(request, "id")               ' /notes/:id
    RETURN {"id": id$}                                  ' a map goes out as JSON
ENDFUNC

FUNC AddNote(request)
    DIM note = JSON.PARSE$(request{"BODY"})
    RETURN JDWEB.REPLY_JSON(note, 201)
ENDFUNC

FUNC NeedKey(request)
    DIM hs = request{"HEADERS"}
    IF MAP.EXISTS(hs, "x-api-key") THEN RETURN JDWEB.PASS()
    RETURN JDWEB.REPLY_JSON({"error": "no key"}, 401)
ENDFUNC

FUNC Stamp(request)
    RETURN {"X-App": "notes"}                           ' headers for every answer
ENDFUNC

JDWEB.GET("/notes/:id", ShowNote@)
JDWEB.POST("/notes", AddNote@)
JDWEB.BEFORE(NeedKey@)
JDWEB.AFTER(Stamp@)
JDWEB.ASSETS("/static", "static")
JDWEB.SERVE(8080)
```

Testing the same app without a browser:

```basic
IF JDWEB.LISTEN(9090) THEN
    DIM r = JDWEB.FETCH(9090, "GET", "/notes/7")
    PRINT r{"status"}, r{"body"}                        ' 200  {"id":"7"}
    HTTP.SERVER.STOP()
ENDIF
```

## API

### Routes

A handler takes the request map (`METHOD`, `PATH`, `BODY`, `PARAMS`, and
`HEADERS` with lower-case names) and returns a string (sent as HTML), a map
or an array (sent as JSON), or a response from REPLY or REPLY_JSON.

| Call | What it does |
|------|--------------|
| `ROUTE(method$, pattern$, handler)` | A handler for a method and a path pattern. `:name` takes one segment, `*name` the rest of the path; a trailing slash on the path still matches. The first matching route answers. |
| `GET` / `POST` / `PUT` / `DELETE` / `PATCH(pattern$, handler)` | ROUTE for that method; GET also answers HEAD. |
| `ANY(pattern$, handler)` | A handler for every method. |
| `PARAM$(request, name$)` | A route parameter, or when there is none of that name a query parameter; `""` when neither exists. Route parameters are percent-decoded. |

A path that matches a route for other methods only is answered with 405
and an `Allow` header naming them.

### Middleware and errors

| Call | What it does |
|------|--------------|
| `BEFORE(handler)` | Runs before the routes, in the order added. It returns `PASS()` to go on, or a response to answer at once. |
| `PASS()` | What a BEFORE function returns to let the request go on. |
| `AFTER(handler)` | Runs on every response, also on 404, 405 and the answers of BEFORE functions. It returns a map of headers to add, or a response with `__http_status` to send instead. |
| `CURRENT_STATUS()` | The status of the response an AFTER function is called for. |
| `ON_MISSING(handler)` | Answers paths that no route and no asset folder serve; its answer goes out with status 404 unless it sets its own. Without it the answer is `Not Found`. |
| `ON_ERROR(handler)` | Answers a route that throws; its answer goes out with status 500 unless it sets its own. Without it the answer is `Internal Server Error`. |
| `ERROR_MESSAGE$()` | The message of the error ON_ERROR is called for. |

### Responses

| Call | What it does |
|------|--------------|
| `REPLY(body$, [status], [content_type$])` | A response (200, `text/html; charset=utf-8`). |
| `REPLY_JSON(value, [status])` | A value sent as JSON (200). |
| `HEADER(response, name$, value$)` | A copy of the response with one more header. Another `Set-Cookie` is added beside the ones already there (the value becomes an array and the server sends one line each), so a handler's own cookie and the session cookie both arrive; any other name replaces its value. |
| `REDIRECT_TO(loc$)` | A 302 to another path. |
| `UNAUTH()` | A 401 with a JSON error. |

### Sessions and files

| Call | What it does |
|------|--------------|
| `SESSION(request)` | The visitor's session map. A new visitor gets an empty one and the response sets the `jdwsid` cookie (HttpOnly, SameSite=Lax); what the handler stores in the map is there on the visitor's next request. Sessions live in memory and end with the program. |
| `SESSION_END(request)` | Forgets the visitor's session and clears the cookie. |
| `ASSETS(prefix$, dir$)` | Serves the files of a folder under a path prefix to GET and HEAD: `/static/css/site.css` from `dir$/css/site.css`, a folder by its `index.html`, with the content type of the extension. A path that climbs out of the folder is refused with 403. |

### Serving and testing

| Call | What it does |
|------|--------------|
| `SERVE(port, [host$])` | Starts the app and serves until a handler stops the server (`127.0.0.1` by default). |
| `LISTEN(port, [host$])` | Starts the app and returns TRUE, for a program that goes on, such as a test. |
| `MOUNT()` | Only puts the routes on the server (through `HTTP.SERVER.ON_NOTFOUND`), for a program that starts the server itself. |
| `DISPATCH(request)` | The answer to one request map, as MOUNT uses it. |
| `FETCH(port, method$, path$, [body$], [headers])` | A request to the app running in this process: a map with `status`, `body` and `headers` (lower-case names). `headers` is a map; a `Content-Type` among them sets the body's type (JSON by default). |
| `COOKIE_OF$(response, name$)` | The value a FETCH response sets for a cookie, or `""`; it looks through every `Set-Cookie` the response carries. |

### Pages and login

| Call | What it does |
|------|--------------|
| `TEMPLATES(dir$)` | The folder the page templates are read from; by default `jdweb_tpl/` next to the app script. |
| `PAGE$(cfg, title$, nav$, content$)` | A whole HTML document in the theme; `cfg` names the app and may turn on the cookie banner. |
| `NAV_HTML$(cfg, active$)` / `THEME$()` / `COOKIE_BANNER$()` | The header with its navigation, the stylesheet, the banner. |
| `LOGIN_PAGE$(cfg)` / `NOT_FOUND$(cfg)` | The themed sign-in page and 404 page. |
| `AUTH_INIT(db)` | Creates the `users` and `sessions` tables in a SQLite database. |
| `AUTH_LOGIN(db, request, secure)` / `AUTH_LOGOUT(db, request)` / `AUTH_ME(db, request)` | The sign-in, sign-out and who-am-I endpoints: the first sign-in on an empty table creates the owner, each user's first sign-in sets the password (salted SHA-256), the `jdwsession` cookie carries the session. |
| `AUTH_USER$(db, request)` | The signed-in user's name, or `""`. |
| `HASH_PW$(salt$, pw$)` / `COOKIE_VAL$(request, name$)` | The password hash and a cookie of the request. |

## Notes

- Routes answer through `HTTP.SERVER.ON_NOTFOUND`, so paths registered with
  `HTTP.SERVER.ON_GET` and `ON_POST` keep working beside them; jdTrakr is
  built that way and runs on this module unchanged.
- The server hands one request at a time to the program, so the module
  keeps the route parameters and session changes of the current request in
  its own state.
- A deploy needs `jdweb.jdb`, `tmpl.jdb` and the `jdweb_tpl/` folder next to
  the app; the module imports only TMPL.
- Compiled with `-c`, the page, login and response helpers work; serving
  routes from a compiled program does not yet, so run a JDWEB app
  interpreted.

Self test: `tests/jdlibs/jdweb_selftest.jdb`.
Demo: `jdb/demos/jdlibs/jdweb_demo.jdb`. A full app on it:
`jdb/demos/web/jdtrakr.jdb`, with the deploy runbook in
`jdb/demos/web/deploy/DEPLOY.md`.
