# Building Web Apps in jdBasic - JDWEB + TMPL

A practical guide to building web servers in jdBasic, from a one-line "Hello
World" up to a complete app with a theme, cookie-session login and a SQLite
database. It uses three pieces:

| Piece | What it is | How you get it |
|---|---|---|
| **HTTP.SERVER** | the web server itself (routing, requests, responses) | built into jdBasic (the `HTTP` build flag) |
| **TMPL** (`tmpl.jdb`) | an HTML template engine (the "views") | `IMPORT TMPL` |
| **JDWEB** (`jdweb.jdb`) | a small framework: page chrome, theme CSS, nav, cookie-session auth | `IMPORT JDWEB` |

**Mental model:** `HTTP.SERVER` maps a URL to one of your handler functions ->
your handler builds an HTML string (with `TMPL`/`JDWEB`) or returns a map (which
becomes JSON) -> the server sends it back.

---

## Setup

**Build flags.** You need `HTTP`. For the JDWEB auth/database parts you also need
`SQLITE`:

```
build.bat HTTP GFX IMGUI NATIVEC SQLITE        # Windows (GFX/IMGUI/NATIVEC optional)
./build.sh HTTP SQLITE                          # Linux/macOS
```

**Files next to your app.** `TMPL` and `JDWEB` are jdBasic modules - `IMPORT`
resolves them relative to your script, so keep them in the same folder:

```
myapp/
  myapp.jdb
  tmpl.jdb            # copy from jdb/demos/web/
  jdweb.jdb           # copy from jdb/demos/web/  (only if you use JDWEB)
  jdweb_tpl/          # copy the whole folder      (JDWEB reads its HTML/CSS from here)
    layout.html  theme.html  nav.html  login.html  notfound.html  cookiebar.html
```

JDWEB renders through TMPL, so if you use JDWEB you need `tmpl.jdb` too.

**Run:** `jdBasic myapp.jdb` (then open the printed URL; `Ctrl+C` stops it).

---

## Level 0 - Hello World (just HTTP.SERVER)

```basic
' hello.jdb
FUNC HELLO(request)
    RETURN "<h1>Hello from jdBasic</h1>"
ENDFUNC

HTTP.SERVER.ON_GET "/", "HELLO"

IF HTTP.SERVER.START(8080, "127.0.0.1") THEN
    PRINT "Listening on http://127.0.0.1:8080"
    HTTP.SERVER.WAIT
ELSE
    PRINT "Could not start: "; ERRMSG$
ENDIF
HTTP.SERVER.STOP
```

What to notice:

- A **handler** is a `FUNC NAME(request)`. Returning a **string** sends it as the
  response body (HTML by default).
- `HTTP.SERVER.ON_GET "/path", "HANDLER"` registers it. The second argument is the
  handler's name as a **string** - it must match the function name.
- `HTTP.SERVER.START(port[, host$])` returns true on success. The default bind
  is **`127.0.0.1` (loopback only)** - pass `"0.0.0.0"` explicitly to expose the
  server to the LAN. (This default changed for security reasons; older examples
  that omit the host and expect LAN visibility are wrong.)
- **`HTTP.SERVER.WAIT` blocks the main thread** and runs the server. Use it - do
  **not** write a `DO ... SLEEP ... LOOP`: that runs jdBasic bytecode on the main
  thread at the same time as the handler pool and crashes the shared VM.

---

## Level 1 - Dynamic views with TMPL

`TMPL` turns a template + a **model** (a `MAP`) into a string. Holes are
dotted/bracket lookups into the model, and HTML is escaped by default.

```basic
IMPORT TMPL

DIM m AS MAP
m{"name"} = "<b>Kat</b>"
PRINT TMPL.RENDERSTR$("Hi {{ name }}", m)        ' -> Hi &lt;b&gt;Kat&lt;/b&gt;
PRINT TMPL.RENDERSTR$("Hi {{{ name }}}", m)      ' -> Hi <b>Kat</b>   (triple = raw)
```

### Syntax

```
{{ x }}                 escaped interpolation
{{{ x }}}               raw (no HTML escaping)
{{ user.name }}         nested map
{{ items[0].title }}    array index then key
{% if cond %} ... {% elseif cond %} ... {% else %} ... {% endif %}
{% for it in items %} ... {% endfor %}
{% include "partial.html" %}            inserts another file (resolved next to this one)
{% extends "layout.html" %}             this file fills the layout's blocks
{% block name %} ... {% endblock %}     an override point / default content
```

Conditions: a bare path (truthy - an empty array/string/`0`/missing key is
false), `path == "literal"`, `path != "literal"`, `not path`.

`for` has **no** `else` branch. For an "empty list" message, wrap the loop:
`{% if items %} ...{% for...%}...{% endfor %}... {% else %} nothing yet {% endif %}`.

### Filters (the `|` pipe, chainable)

```
{{ name | upper }}              UPPER / lower / trim / length
{{ missing | default:"n/a" }}   fallback when empty/missing
{{ created | date:"DD.MM.YYYY" }}
{{ price | money:"EUR" }}       -> 3,50 EUR
{{ name | upper | trim }}       chain them
```

Register your own filter (the function gets `(value, arg$)`):

```basic
FUNC SHOUT(v, arg$)
    RETURN UCASE$("" + v) + "!"
ENDFUNC
TMPL.ADDFILTER "shout", SHOUT@
' {{ name | shout }}  ->  KAT!
```

### Rendering a file

`TMPL.RENDER$(path$, model)` reads a file and renders it; it caches the parsed
template per path + modification time, so a file is parsed once and re-rendered
from cache after that.

```html
<!-- greet.html -->
<ul>
{% for u in users %}
  <li>{{ u.name }}{% if u.admin %} (admin){% endif %}</li>
{% endfor %}
</ul>
```

```basic
IMPORT TMPL
DIM model AS MAP
DIM users = []
users = APPEND(users, { "name": "Kat",  "admin": TRUE })
users = APPEND(users, { "name": "Piet", "admin": FALSE })
model{"users"} = users
PRINT TMPL.RENDER$("greet.html", model)
```

### Serving a template from a route

```basic
IMPORT TMPL

FUNC HOME(request)
    DIM model AS MAP
    model{"title"} = "Tasks"
    DIM items = []
    items = APPEND(items, { "text": "Write the guide", "done": TRUE })
    items = APPEND(items, { "text": "Ship it",          "done": FALSE })
    model{"items"} = items
    RETURN TMPL.RENDER$("home.html", model)
ENDFUNC

HTTP.SERVER.ON_GET "/", "HOME"
IF HTTP.SERVER.START(8080, "127.0.0.1") THEN HTTP.SERVER.WAIT
HTTP.SERVER.STOP
```

---

## Level 2 - Reading input and returning JSON

Returning a **MAP** from a handler sends it as JSON (`Content-Type:
application/json`). The `request` map gives you the input:

| Field | Type | What |
|---|---|---|
| `request{"PATH"}` | string | the request path, e.g. `"/api/hello"` - lets one handler serve several routes |
| `request{"METHOD"}` | string | `"GET"`, `"POST"`, ... |
| `request{"PARAMS"}` | map | query-string params: `?name=Kat` -> `{"name"}` = `"Kat"` |
| `request{"BODY"}` | string | the raw request body; `JSON.PARSE$` it for JSON posts |
| `request{"HEADERS"}` | map | request headers, **lowercase** keys: `{"cookie"}`, `{"x-api-key"}` |

```basic
' GET /api/hello?name=Kat   ->  {"greeting":"Hello, Kat"}
FUNC API_HELLO(request)
    DIM p = request{"PARAMS"}
    DIM name$ = "" + p{"name"}
    IF LEN(name$) = 0 THEN name$ = "world"
    RETURN { "greeting": "Hello, " + name$ }
ENDFUNC

' POST /api/echo   body: {"msg":"hi"}   ->  {"you_said":"hi"}
FUNC API_ECHO(request)
    DIM body = JSON.PARSE$(request{"BODY"})
    RETURN { "you_said": "" + body{"msg"} }
ENDFUNC

HTTP.SERVER.ON_GET  "/api/hello", "API_HELLO"
HTTP.SERVER.ON_POST "/api/echo",  "API_ECHO"
```

**Custom status / headers / redirects.** Return a *rich-response map* with
double-underscore keys:

```basic
FUNC GONE(request)
    RETURN { "__http_status": 410, "__http_content_type": "text/plain", _
             "__http_body": "Gone." }
ENDFUNC

FUNC GO_HOME(request)
    RETURN { "__http_status": 302, "__http_headers": { "Location": "/" }, "__http_body": "" }
ENDFUNC
```

---

## Level 3 - Real pages with JDWEB (theme, nav, layout)

JDWEB wraps your page body in a full HTML document with a shared theme (a
terminal "Hot Phosphor" look), a header/nav bar, and an optional cookie banner.
You give it a small **config** map (usually loaded from a JSON file).

```basic
IMPORT JDWEB
IMPORT TMPL

' resolve files relative to THIS script, so the app runs from any folder
gAppDir$ = PATH.DIRNAME$(OS.ARGS()[0])

DIM cfg AS MAP
cfg{"app"}     = "MyApp"
cfg{"sub"}     = "a jdBasic web app"
cfg{"cookies"} = FALSE
DIM nav = []
nav = APPEND(nav, { "path": "/",      "label": "Home" })
nav = APPEND(nav, { "path": "/about", "label": "About" })
cfg{"nav"} = nav

FUNC HOME(request)
    DIM body$ = TMPL.RENDERSTR$("<h2>Welcome, {{ who }}</h2>", { "who": "Kat" })
    RETURN JDWEB.PAGE$(cfg, "MyApp - Home", JDWEB.NAV_HTML$(cfg, "/"), body$)
ENDFUNC

FUNC ABOUT(request)
    DIM body$ = "<p>Built with jdBasic, JDWEB and TMPL.</p>"
    RETURN JDWEB.PAGE$(cfg, "MyApp - About", JDWEB.NAV_HTML$(cfg, "/about"), body$)
ENDFUNC

HTTP.SERVER.ON_GET "/",      "HOME"
HTTP.SERVER.ON_GET "/about", "ABOUT"
IF HTTP.SERVER.START(8080, "127.0.0.1") THEN HTTP.SERVER.WAIT
HTTP.SERVER.STOP
```

Key calls:

- **`JDWEB.PAGE$(cfg, title$, navHtml$, content$)`** - the full HTML document.
  Pass `""` for `navHtml$` on chrome-less pages (e.g. login).
- **`JDWEB.NAV_HTML$(cfg, active$)`** - the header + nav; `active$` is the path of
  the current page so its link is highlighted.
- The theme CSS lives in `jdweb_tpl/theme.html` - edit that file to restyle every
  page at once.

`cfg` is just a map; most apps load it from JSON so the same code serves several
apps:

```basic
gCfg = JSON.PARSE$(TXTREADER$(PATH.JOIN$(gAppDir$, "myapp.json")))
```

```json
{ "app": "MyApp", "sub": "a jdBasic web app", "db": "myapp.db",
  "host": "127.0.0.1", "port": 8080, "secure": false, "cookies": true,
  "meta_desc": "One-line site description for search engines.",
  "nav": [ {"path":"/","label":"Home"}, {"path":"/about","label":"About"} ] }
```

**SEO:** when `cfg{"meta_desc"}` is set, `PAGE$` emits a `<meta name='description'>`
plus OpenGraph tags (`og:title`, `og:description`, `og:site_name`). For a
page-specific description, hand `PAGE$` a shallow copy of the config with
`meta_desc` overridden - see `cfg_with_desc` in jdeRG for the 6-line helper.

---

## Level 3.5 - Static pages from disk: one handler, N pages

Once an app grows a handful of mostly-static pages (imprint, privacy, help,
articles), a `FUNC` per page stops scaling. The pattern below - proven in
production by jdeRG (profi-rg.de) - discovers pages on disk at startup and
serves them all through **one** shared handler.

Each page file is a body fragment whose first line is a front-matter comment
of `key="value"` pairs:

```html
<!-- page route="/impressum" nav="" title="MyApp - Imprint" -->
<div class="page">
  <h1>Imprint</h1>
  ...
</div>
```

At startup: scan the folder with `DIR$`, parse the front-matter, cache the
body **eagerly**, and register every discovered route on the same handler:

```basic
DIM gPages AS MAP    ' route -> { title, nav, content, ... }

' front-matter parser: SPLIT on the quote char pairs key= / value
FUNC page_meta(src$) AS OBJECT
    DIM meta AS MAP
    IF NOT STARTSWITH(src$, "<!-- page") THEN RETURN meta
    close_at = INSTR(src$, "-->")
    IF close_at < 0 THEN RETURN meta
    parts = SPLIT(MID$(src$, 9, close_at - 9), CHR$(34))
    FOR i = 0 TO LEN(parts) - 2 STEP 2
        k$ = TRIM$(parts[i])
        IF ENDSWITH(k$, "=") THEN meta{LEFT$(k$, LEN(k$) - 1)} = parts[i + 1]
    NEXT i
    meta{"__body"} = TRIM$(MID$(src$, close_at + 3))
    RETURN meta
ENDFUNC

SUB pages_scan(dir$)
    FOR EACH f$ IN DIR$(PATH.JOIN$(dir$, "*.html"))
        meta = page_meta(TXTREADER$(PATH.JOIN$(dir$, f$)))
        IF TYPEOF(meta{"route"}) <> "NONE" THEN gPages{"" + meta{"route"}} = meta
    NEXT f$
ENDSUB

FUNC HandleStaticPage(request)
    p = gPages{"" + request{"PATH"}}
    IF TYPEOF(p) = "NONE" THEN RETURN JDWEB.NOT_FOUND$(gCfg)
    RETURN JDWEB.PAGE$(gCfg, "" + p{"title"}, JDWEB.NAV_HTML$(gCfg, "" + p{"nav"}), "" + p{"__body"})
ENDFUNC

pages_scan(PATH.JOIN$(gAppDir$, "pages"))
FOR EACH page_route$ IN MAP.KEYS(gPages)
    HTTP.SERVER.ON_GET page_route$, "HANDLESTATICPAGE"
NEXT page_route$
```

Adding a page is now: drop a file in `pages/`, restart. No wildcard routes are
needed (and none exist) - every discovered route is registered explicitly, so
unknown paths still hit the themed 404 and there is no path-traversal surface.

Why **eager** (everything read and cached before `HTTP.SERVER.START`): after
`START` the main thread must sit in `HTTP.SERVER.WAIT` and the maps should be
read-only shared state. Loading up front keeps every handler a pure read - no
disk I/O, no cache-fill writes, no surprises under load.

The worked production version of this pattern (per-page meta descriptions, a
guest-only flag, sitemap + article index derived from the discovered pages,
and a `page_cache: false` dev flag that re-reads files per request) lives in
jdeRG: <https://github.com/AtomiJD/jderg> (`jderg.jdb`, section
"static pages", plus `tpl/pages/`).

---

## Level 4 - A complete app: login + database + a templated list

This ties everything together: a SQLite table, cookie-session login from JDWEB,
a guard that redirects anonymous visitors to `/login`, and a list page rendered
with TMPL. (A browser-side `fetch` could refresh the list as JSON; here we keep
it server-rendered for clarity.)

```basic
' notes.jdb  - run with: jdBasic notes.jdb   (needs SQLITE build)
IMPORT JDWEB
IMPORT TMPL

gAppDir$ = PATH.DIRNAME$(OS.ARGS()[0])
gCfg = JSON.PARSE$(TXTREADER$(PATH.JOIN$(gAppDir$, "notes.json")))
gApp$ = "" + gCfg{"app"}

gDb = SQL.OPEN(gCfg{"db"})
JDWEB.AUTH_INIT(gDb)                 ' creates the users + sessions tables
SQL.EXEC(gDb, "CREATE TABLE IF NOT EXISTS notes (" + _
    "id INTEGER PRIMARY KEY AUTOINCREMENT, body TEXT NOT NULL, " + _
    "created_at TEXT DEFAULT CURRENT_TIMESTAMP)")

' escape single quotes for SQL string literals (complete for SQLite)
FUNC ESC$(s$)
    RETURN REPLACE$(s$, "'", "''")
ENDFUNC

' --- auth route wrappers: the server calls handlers by name, so we delegate ---
FUNC HandleLogin(request)     : RETURN JDWEB.AUTH_LOGIN(gDb, request, FALSE) : ENDFUNC
FUNC HandleLogout(request)    : RETURN JDWEB.AUTH_LOGOUT(gDb, request)       : ENDFUNC
FUNC HandleMe(request)        : RETURN JDWEB.AUTH_ME(gDb, request)           : ENDFUNC
FUNC HandleLoginPage(request) : RETURN JDWEB.LOGIN_PAGE$(gCfg)               : ENDFUNC
FUNC Handle404(request)       : RETURN JDWEB.NOT_FOUND$(gCfg)                : ENDFUNC

' --- the notes page (guarded) ---
FUNC HomePage(request)
    IF LEN(JDWEB.AUTH_USER$(gDb, request)) = 0 THEN RETURN JDWEB.REDIRECT_TO("/login")
    DIM rows = SQL.QUERY(gDb, "SELECT id, body, created_at FROM notes ORDER BY id DESC")
    DIM model AS MAP
    model{"notes"} = rows
    DIM body$ = TMPL.RENDER$(PATH.JOIN$(gAppDir$, "notes_body.html"), model)
    RETURN JDWEB.PAGE$(gCfg, gApp$, JDWEB.NAV_HTML$(gCfg, "/"), body$)
ENDFUNC

' --- the JSON API the form posts to ---
FUNC AddNote(request)
    IF LEN(JDWEB.AUTH_USER$(gDb, request)) = 0 THEN RETURN JDWEB.UNAUTH()
    DIM b = JSON.PARSE$(request{"BODY"})
    DIM text$ = TRIM$("" + b{"body"})
    IF LEN(text$) = 0 THEN RETURN { "error": "empty" }
    SQL.EXEC(gDb, "INSERT INTO notes (body) VALUES ('" + ESC$(text$) + "')")
    RETURN { "ok": TRUE }
ENDFUNC

HTTP.SERVER.ON_GET  "/",           "HomePage"
HTTP.SERVER.ON_POST "/api/notes",  "AddNote"
HTTP.SERVER.ON_GET  "/login",      "HandleLoginPage"
HTTP.SERVER.ON_POST "/api/login",  "HandleLogin"
HTTP.SERVER.ON_POST "/api/logout", "HandleLogout"
HTTP.SERVER.ON_GET  "/api/me",     "HandleMe"
HTTP.SERVER.ON_NOTFOUND "Handle404"

DIM warm$ = JDWEB.THEME$()          ' warm the template cache before going concurrent
IF HTTP.SERVER.START(INT(gCfg{"port"}), "" + gCfg{"host"}) THEN
    PRINT gApp$; " on http://"; gCfg{"host"}; ":"; gCfg{"port"}
    HTTP.SERVER.WAIT
ENDIF
HTTP.SERVER.STOP
```

```html
<!-- notes_body.html -->
<form class='add' id='noteForm'>
  <input class='grow' name='body' placeholder='New note' autocomplete='off'>
  <button type='submit'>Add</button>
</form>
<div class='wrap'>
{% if notes %}
{% for n in notes %}
  <div class='urow'><span>{{ n.body }}</span><span class='when'>{{ n.created_at }}</span></div>
{% endfor %}
{% else %}
  <div class='empty'>No notes yet.</div>
{% endif %}
</div>
<script>
document.getElementById('noteForm').addEventListener('submit',async(ev)=>{
  ev.preventDefault();const f=ev.target;const body=f.body.value.trim();if(!body)return;
  await fetch('/api/notes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({body})});
  location.reload();
});
</script>
```

The auth model (provided by JDWEB): the **first** sign-in on an empty board
creates the owner and sets their password; afterwards only known user names sign
in, each claiming their password on first login. Sessions are cookies
(`jdwsession`, HttpOnly, SameSite=Lax, salted SHA-256 password). `AUTH_USER$`
returns the logged-in name or `""` - guard every protected handler with it.

### JDWEB API at a glance

| Call | Purpose |
|---|---|
| `JDWEB.PAGE$(cfg, title$, nav$, content$)` | full HTML document |
| `JDWEB.NAV_HTML$(cfg, active$)` | header + nav bar |
| `JDWEB.THEME$()` | the stylesheet (also good for cache-warming) |
| `JDWEB.LOGIN_PAGE$(cfg)` / `JDWEB.NOT_FOUND$(cfg)` | the login page / a themed 404 |
| `JDWEB.AUTH_INIT(db)` | create users + sessions tables |
| `JDWEB.AUTH_USER$(db, request)` | logged-in user name, or `""` |
| `JDWEB.AUTH_LOGIN/AUTH_LOGOUT/AUTH_ME(db, request[, secure])` | login/logout/whoami endpoints |
| `JDWEB.UNAUTH()` | 401 JSON response |
| `JDWEB.REDIRECT_TO(loc$)` | 302 redirect |

---

## Deployment

- **Ship the templates.** A deploy must include `tmpl.jdb`, `jdweb.jdb` and the
  whole `jdweb_tpl/` folder next to your app script (JDWEB resolves them via
  `PATH.DIRNAME$(OS.ARGS()[0])`). Miss them and every page 500s with
  "file not found".
- **Know the threading model.** httplib spawns a thread per accepted request,
  but every handler runs under one VM mutex - handlers are **serialised** on the
  shared VM, so handler-vs-handler races cannot happen. What CAN race is the
  **main thread**: after `HTTP.SERVER.START` it must not execute bytecode
  concurrently, which is exactly why it parks in `HTTP.SERVER.WAIT`. Practical
  rule: do all loading (templates, static pages, config) *before* `START` -
  e.g. one `JDWEB.THEME$()` warms the theme cache - and treat globals as
  read-only afterwards.
- **Bind local, proxy for TLS.** Run the app on `127.0.0.1:<port>` and put nginx
  in front for HTTPS, security headers, a Content-Security-Policy and login rate
  limiting. (See the jdTrakr deploy notes for a worked example.)
- **One binary, no toolchain.** No `npm`, no build step for the views - the
  templates are plain files you edit and the engine re-reads them.

---

## Gotchas (the ones that bite)

- **Handler name = string.** `HTTP.SERVER.ON_GET "/x", "MYHANDLER"` must match the
  `FUNC MYHANDLER` name exactly.
- **`HTTP.SERVER.WAIT`, never a `SLEEP` loop** (see Level 0).
- **Escape user input in SQL** with `ESC$` (doubles `'`; complete for SQLite),
  and cast numbers with `STR$(INT(...))`. Never concatenate raw user strings.
  `SQL.QUERY`/`SQL.EXEC` have no bind parameters; for more than a couple of
  statements, write a tiny `?`-placeholder formatter that quotes each value
  (see `FMT$`/`Q$` in jdeRG's `ergdb.jdb` - about 30 lines).
- **`{{ }}` escapes, `{{{ }}}` does not.** Use the triple braces only for HTML you
  trust (it is the XSS escape hatch).
- **Inline `<script>` under a strict CSP must be hashed.** If you deploy behind a
  hash-based Content-Security-Policy, changing a served inline script means you
  must update its `sha256` in the CSP, or the browser silently blocks it.
- **Header keys are lowercase** in `request{"HEADERS"}` (`"cookie"`,
  `"x-api-key"`). The request path is `request{"PATH"}` (uppercase key, like
  `METHOD`/`BODY`/`PARAMS`).
- **No wildcard routes.** Registration is exact-match only. To serve a family
  of URLs, register each path explicitly (a `FOR EACH` over discovered routes,
  see Level 3.5) and dispatch inside the handler on `request{"PATH"}`.
- **Reserved names** still apply in handlers: don't name variables `LINE`, `VAL`,
  `LEN`, `MAP`, `E`, `PI`, etc.

---

## See also

- `jdb/demos/web/tmpl.jdb` - the template engine (+ `tmpl_test.jdb`, 28 checks)
- `jdb/demos/web/jdweb.jdb` - the framework module
- `jdb/demos/web/jdtrakr.jdb` - a full app (kanban board) built on both
- `jdb/demos/web/tmpl_demo.jdb`, `tmpl_server.jdb` - smaller worked examples
- <https://github.com/AtomiJD/jderg> - jdeRG "E-Rechnung Studio"
  (profi-rg.de): the largest production app on this stack - accounts + tiers,
  CSRF double-submit, Stripe billing, transactional mail, static-page
  discovery (Level 3.5), all in jdBasic
