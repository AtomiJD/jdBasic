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

### Sessions

| Call | What it does |
|------|--------------|
| `SESSION(request)` | The visitor's session map. A new visitor, or one whose session expired, gets an empty one and the response sets the `jdwsid` cookie; what the handler stores in the map is there on the visitor's next request. Each read keeps the session alive. The id is 32 random bytes from `SECRET.TOKEN$`. |
| `SESSION_ROTATE(request)` | Moves the session to a new id with what it holds and forgets the old id. Call it at sign-in, so an id someone learned before (session fixation) is worthless after. |
| `SESSION_END(request)` | Forgets the visitor's session and clears the cookie. |
| `SESSION_CONFIG(opts)` | The settings, below. An unknown key raises. |
| `SESSION_CLEANUP()` | Removes expired sessions from memory and the database store and returns how many. DISPATCH runs it on its own at most every five minutes. |
| `SESSION_CLOCK(shift_seconds)` | Moves the session clock, for tests of expiry. |
| `FLASH(request, message$, [kind$])` | A message (`kind$` defaults to `info`) kept in the session until it is read, typically across a redirect. |
| `FLASHES(request)` | The waiting messages as an array of `{kind, text}` maps, oldest first; reading removes them. |
| `CSRF_TOKEN$(request)` | The session's CSRF token, made on first use. Forms send it in a `csrf_token` field, scripts in an `X-CSRF-Token` header. |
| `CSRF_PROTECT([enabled])` | From here on POST, PUT, PATCH and DELETE without the right token are answered with 403. `CSRF_PROTECT(FALSE)` turns the check off. |
| `CSRF_EXEMPT(prefix$)` | A path prefix that needs no token, such as an API that checks a key in a header. |

| Setting | Default | Meaning |
|---------|---------|---------|
| `ttl` | 86400 | Seconds a session lives after its last use; 0 for no limit. |
| `lifetime` | 0 | Seconds since it was made (or rotated), whatever the use; 0 for no limit. |
| `cookie` | `jdwsid` | The cookie name. |
| `secure`, `samesite`, `domain`, `path` | FALSE, `Lax`, none, `/` | The session cookie's attributes. |
| `db` | 0 | A SQLite handle: sessions are written to table `jdweb_sessions` after each request and read back after a restart. Setting it drops what memory holds. |
| `auth_ttl` | 2592000 | Seconds a sign-in of `AUTH_LOGIN` lives after its last use (30 days); 0 for no limit. |
| `auth_rounds` | 600000 | PBKDF2 rounds of a password `AUTH_LOGIN` stores; a stored hash with fewer is renewed at the next sign-in. |

### Cookies and files

| Call | What it does |
|------|--------------|
| `COOKIE$(name$, value$, [opts])` | A `Set-Cookie` value. The value is percent-encoded where a cookie cannot carry a byte; `opts` may set `max_age` (0 deletes), `path` (`/`), `domain`, `secure` (FALSE), `httponly` (TRUE) and `samesite` (`Lax`, `""` leaves it out). `SameSite=None` always gets `Secure`. |
| `SET_COOKIE(response, name$, value$, [opts])` | A copy of the response that sets the cookie COOKIE$ builds. |
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
| `AUTH_LOGIN(db, request, secure)` / `AUTH_LOGOUT(db, request)` / `AUTH_ME(db, request)` | The sign-in, sign-out and who-am-I endpoints: the first sign-in on an empty table creates the owner, each user's first sign-in sets the password, the `jdwsession` cookie carries a 32-byte random token. Passwords are kept as `SECRET.HASH$` (PBKDF2, `auth_rounds`); a row of the older salted SHA-256 scheme, or one with fewer rounds, still signs in and is hashed again at that sign-in. |
| `AUTH_USER$(db, request)` | The signed-in user's name, or `""`. A sign-in unused for longer than the `auth_ttl` setting (30 days) is removed; each use keeps it alive. |
| `AUTH_CLEANUP(db)` | Removes the sign-ins unused for longer than `auth_ttl` and returns how many. |
| `HASH_PW$(salt$, pw$)` / `COOKIE_VAL$(request, name$)` | The password hash, and a cookie of the request, percent-decoded. |

## Notes

- Routes answer through `HTTP.SERVER.ON_NOTFOUND`, so paths registered with
  `HTTP.SERVER.ON_GET` and `ON_POST` keep working beside them; jdTrakr is
  built that way and runs on this module unchanged.
- The server hands one request at a time to the program, so the module
  keeps the route parameters and session changes of the current request in
  its own state.
- A deploy needs `jdweb.jdb`, `tmpl.jdb`, `secret.jdb` and the `jdweb_tpl/`
  folder next to the app; the module imports TMPL and SECRET.
- Sessions, flash messages and CSRF tokens live in the session map under
  keys of their own (`__flash`, `__csrf`); a handler should not use keys
  starting with `__`. A database store writes the map as JSON, so what a
  handler keeps in a session there has to be JSON: text, numbers, flags,
  lists and maps.
- Compiled with `-c`, the page, login and response helpers work; serving
  routes from a compiled program does not yet, so run a JDWEB app
  interpreted.

Self test: `tests/jdlibs/jdweb_selftest.jdb`.
Demo: `jdb/demos/jdlibs/jdweb_demo.jdb`. A full app on it:
`jdb/demos/web/jdtrakr.jdb`, with the deploy runbook in
`jdb/demos/web/deploy/DEPLOY.md`.
