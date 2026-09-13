# OAUTH - OAuth 2 client flows on REQ

`lib/oauth.jdb` gets and keeps the tokens that Google, Microsoft Graph
and most other APIs want: client credentials, the authorization code
flow with PKCE and a local redirect on `HTTP.SERVER`, the device code
flow, and refresh tokens. Tokens go on REQ sessions and are refreshed on
the way when they have run out or the API refuses them, and they can be
kept in a CACHE that is saved to a file.

Stands in for: authlib's OAuth 2 client, requests-oauthlib.

## Quick start

```basic
IMPORT OAUTH

' a desktop or command line app: the browser opens, the user signs in
DIM app = OAUTH.GOOGLE(client_id$, client_secret$)
DIM token$ = OAUTH.LOGIN(app, "openid email https://www.googleapis.com/auth/calendar.readonly", 8765)

DIM api = OAUTH.SESSION(app, "https://www.googleapis.com")
DIM events = OAUTH.GET(app, api, "/calendar/v3/calendars/primary/events", {"maxResults": 10})
PRINT events{"status"}

' a service without a user
DIM graph = OAUTH.MICROSOFT(app_id$, app_secret$, tenant_id$)
DIM service$ = OAUTH.CLIENTCREDENTIALS(graph, "https://graph.microsoft.com/.default")
```

## Clients

| Call | What it does |
|------|--------------|
| `NEW(client_id$, client_secret$, auth_url$, token_url$, [device_url$])` | A client of an authorization server. A public client (a desktop app with PKCE) has the secret `""`. |
| `GOOGLE(client_id$, [client_secret$])` | Google's endpoints; the code flow asks for a refresh token (`access_type=offline&prompt=consent`). |
| `MICROSOFT(client_id$, [client_secret$], [tenant$])` | The Microsoft identity platform (Entra ID, Graph) for `common` (default), `organizations`, `consumers` or a tenant id. |
| `SETOPT(client, key$, value)` | `scope`, `redirect_uri`, `auth_method` (`post` puts the secret in the form, `basic` in an Authorization header), `skew` (seconds before the real end a token counts as run out, 30), `extra_auth` (more query text for the sign-in address), `opener` (a function that opens an address for LOGIN). |

A client is a map; after a flow it holds `access_token`,
`refresh_token`, `token_type`, `id_token`, `token_scope` and
`expires_at` (seconds since 1970).

## Flows

| Call | What it does |
|------|--------------|
| `CLIENTCREDENTIALS(client, [scope$])` | A token for the client itself; answers the access token. |
| `LOGIN(client, scope$, port, [path$], [start_server])` | The whole code flow for an app on the user's machine: a redirect handler on `http://127.0.0.1:port/callback`, the browser opened at the sign-in address, the code traded once it arrives; answers the access token. Register that redirect address with the provider. |
| `AUTHURL$(client, [scope$], [redirect_uri$])` | The sign-in address with a fresh `state` and PKCE verifier kept in the client, for a web app that redirects itself. |
| `REDIRECTED(client, address$)` | Finishes from the address the browser came back to: checks the state, names an `error` the provider sent, trades the code; answers the access token. |
| `EXCHANGE(client, code$)` | Trades a code for tokens directly. |
| `MOUNT([path$])` / `CALLBACK(request)` | The redirect handler on a server that is already set up; `LOGIN(..., start_server = 0)` then uses it without starting or stopping the server. |
| `DEVICESTART(client, [scope$])` | Starts the device flow for a device without a browser; answers `user_code`, `verification_uri`, `verification_uri_complete`, `interval` and `expires_in` to show the user. |
| `DEVICEPOLL(client, [max_seconds])` | Asks until the user has approved, keeping the interval and slowing down when told to; answers the access token. |
| `CHALLENGE$(verifier$)` | The S256 challenge of a PKCE verifier. |

PKCE is always on: the verifier is 64 random hex characters, the
challenge its SHA-256 in base64url, as RFC 7636 describes.

## Tokens

| Call | What it does |
|------|--------------|
| `TOKEN$(client)` | A valid access token, refreshed first when it has run out. |
| `REFRESH(client)` | A new token from the refresh token; a client credentials client asks again. A refresh token the server rotates is kept. |
| `EXPIRED(client)` / `EXPIRE(client)` | Whether it has run out (within the skew), and marking it so. |
| `STORE(client, cache, key$)` | Keeps the tokens in a CACHE under a key: tokens there are taken now, every new token is put there. |
| `CACHEOF(client)` | That cache with the latest tokens, for `CACHE.SAVE`. |

```basic
DIM app = OAUTH.MICROSOFT(app_id$)
OAUTH.STORE(app, CACHE.LOAD("tokens.json"), "me@contoso.com")
IF OAUTH.EXPIRED(app) ORELSE app{"access_token"} = "" THEN DIM t$ = OAUTH.LOGIN(app, "User.Read offline_access", 8765)
CACHE.SAVE(OAUTH.CACHEOF(app), "tokens.json")
```

## Calling APIs

| Call | What it does |
|------|--------------|
| `SESSION(client, base_url$)` | A REQ session for the API; any other REQ setting (headers, retries, timeout) works on it. |
| `GET(client, s, path$, [query])` | A REQ call with the client's token as Bearer. |
| `POST(client, s, path$, [body], [content_type$])` | A map or array body goes out as JSON. |
| `SEND(client, s, method$, path$, [body$], [content_type$], [query])` | Any method. |

Before each call a token that has run out is refreshed; when the API
still answers 401, the token is refreshed once more and the call
repeated. The answer is REQ's response map.

## Errors

A failed flow throws `OAUTH: ` and what went wrong: the error and
description the token endpoint sent (`OAUTH: invalid_grant (Bad
Request)`), `authorization failed: access_denied` from a redirect, `the
state in the redirect does not match`, `no refresh token`, `no token
yet, run a flow first`, `the device code expired`, or the transport
error REQ reported.

## Notes

- `LOGIN` waits in `HTTP.SERVER.WAIT` until the redirect arrives, so no
  jdBasic code runs beside the handler, as HTTP.SERVER requires.
- The browser opens through `url.dll` on Windows, `open` on macOS and
  `xdg-open` on Linux; the address is also printed. `SETOPT(client,
  "opener", fn@)` replaces that.
- The acceptance runs against a fake authorization server in the same
  process: the code flow with PKCE and its redirect handler, LOGIN with
  a second jdBasic process as the browser, client credentials with both
  kinds of client authentication, rotating refresh tokens, a session
  that refreshes an expired and a revoked token, the device flow through
  `authorization_pending`, and tokens loaded from a saved cache.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/oauth_selftest.jdb`. Demo: `jdb/demos/jdlibs/oauth_demo.jdb`.
