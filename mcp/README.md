# jdBasic MCP server

A [Model Context Protocol](https://modelcontextprotocol.io) server **written in jdBasic itself** — dogfooding the language as a real tool runtime for AI assistants.

## What it exposes

Ten tools, served as JSON-RPC 2.0 over HTTP POST `/mcp` on `127.0.0.1:7321`:

| Tool                | Purpose |
|---------------------|---------|
| `jdb_eval`          | Execute jdBasic source on the persistent VM. Captures `PRINT` output. Variables defined in one call survive into the next. Returns `isError` on parse/runtime failure. |
| `jdb_check`         | Lex + parse only — validate a snippet without executing it. Safe to call before `jdb_eval` to avoid contaminating state with half-executed code. |
| `jdb_load`          | Read a jdBasic source file (path relative to the server's CWD, usually the project root) and `EXECUTE` it on the persistent VM. Avoids round-tripping bytes through tool args. |
| `jdb_vars`          | List all global variables currently defined on the persistent VM. Server internals (variables defined by `server.jdb` itself) are filtered out — you only see what your own snippets created. |
| `jdb_funcs`         | List user-defined `FUNC` / `SUB` / `ASYNC FUNC` with their parameter signatures. Server-internal helpers are filtered out. |
| `jdb_save_state`    | Snapshot every user-global under a string label. Multiple snapshots can coexist for the server's lifetime. |
| `jdb_restore_state` | Restore a previously saved snapshot. Restore-by-overwrite — variables added after the save are left in place. |
| `jdb_run_native`    | Run a command line in a child process and return combined stdout+stderr plus exit code. Useful for testing native-compiled `.exe`s without going through the persistent VM. No timeout; long-running processes block. |
| `jdb_doc`           | Fuzzy-search `doc/languages.md` for symbols (`MAP.EXISTS`, `CODEC.UUID$`) or topics (`random`, `json`). Returns up to 8 matching bullet entries / sections. |
| `echo`              | Connectivity smoke test — returns the message you sent. |

## Run it

```bash
build/jdBasic.exe mcp/server.jdb
```

The server binds to **127.0.0.1 only** (loopback). To expose it on the LAN, edit `mcp/server.jdb` and pass `"0.0.0.0"` as the second argument to `HTTP.SERVER.START`.

## Hook it into Claude Code

`.mcp.json` at the repo root already declares the server:

```json
{
  "mcpServers": {
    "jdbasic": {
      "type": "http",
      "url": "http://127.0.0.1:7321/mcp"
    }
  }
}
```

When you open the repo in Claude Code, it'll prompt to trust the MCP server. Once approved, the tools become available as `mcp__jdbasic__jdb_eval`, `mcp__jdbasic__jdb_check`, `mcp__jdbasic__jdb_load`, `mcp__jdbasic__jdb_vars`, `mcp__jdbasic__jdb_funcs`, `mcp__jdbasic__jdb_save_state`, `mcp__jdbasic__jdb_restore_state`, `mcp__jdbasic__jdb_run_native`, `mcp__jdbasic__jdb_doc`, and `mcp__jdbasic__echo`. Start the server first (`build/jdBasic.exe mcp/server.jdb` in another terminal), then ask Claude to use them.

## Smoke test from the command line

```bash
# 1. initialize — captures Mcp-Session-Id from the response headers
SID=$(curl -s -i -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"curl","version":"1"}}}' \
  | grep -i "mcp-session-id:" | sed 's/.*: //; s/[\r\n]//g')

# 2. tools/list — needs the session id
curl -s -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'

# 3. tools/call — evaluate code
curl -s -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"jdb_eval","arguments":{"code":"PRINT FAC(20)"}}}'

# 4. inspect persistent state
curl -s -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"jdb_vars","arguments":{}}}'
```

Without the `Mcp-Session-Id` header every method other than `initialize` returns HTTP 404 (forces the client to re-handshake).

## Building blocks (jdBasic features used)

| Feature                                  | Where                                                                |
|------------------------------------------|----------------------------------------------------------------------|
| `HTTP.SERVER.START` / `ON_POST`          | `src/http.cpp` — built-in HTTP server                                |
| Rich response (`{STATUS, HEADERS, BODY}`)| `src/http.cpp apply_rich_response` — lets handlers control HTTP status + headers |
| `OUTPUT.CAPTURE_BEGIN` / `_END$` / `_PEEK$` | `src/vm.cpp` — captures `PRINT` for `jdb_eval` / `jdb_load` output |
| `EXECUTE` (re-entrant code execution)    | Persistent VM, runs user snippets on the same VM as the server       |
| `TRY` around `EXECUTE`                   | Surfaces parse / runtime errors as MCP `isError` tool results        |
| `VARS()` / `FUNCS()` natives in script mode | `src/main.cpp setup_dynamic_code` — moved out of console-only       |
| `JSON.PARSE$` / auto-stringify on map return | Parses incoming JSON-RPC, encodes responses                       |
| `TYPE` / `MAP` / `ARRAY` literals        | Tool descriptors and request handling                                |
| `CODEC.UUID$`                            | Generates fresh `Mcp-Session-Id` per `initialize`                    |

The server is ~700 lines of jdBasic.

## Streamable-HTTP transport

The server implements MCP's "Streamable HTTP" transport, which is what Claude Code's `type: "http"` config expects:

- On `initialize`, the server generates a random `Mcp-Session-Id` and returns it as a response header.
- Clients echo it back as a request header on every later call. Unknown / missing session id ⇒ HTTP 404 (forces the client to re-initialize).
- Notifications (`method = "notifications/*"`) get HTTP 202 Accepted with an empty body, per JSON-RPC.

Every incoming request and any handler exception is logged to the server's stderr — keep the server's terminal visible while debugging.

## Known limitations

- **Sessions never expire on the server side.** A client that drops without sending DELETE leaves an entry in the in-memory `SESSIONS` map until the server restarts. Harmless for the single-client Claude Code use case but unbounded in principle.
- **Single VM, no isolation.** Every `jdb_eval` / `jdb_load` call runs on the same persistent VM as the server itself. There is no `jdb_spawn` yet, so a long-running snippet blocks the next request behind the VM mutex.
- **`jdb_run_native` has no timeout.** A hung child process keeps the tool call open forever.

## File layout

```
mcp/
├── README.md     ← this file
├── server.jdb    ← the server, ~700 lines of jdBasic
└── jdbasic_history.txt  ← REPL-style history written when the server stops
.mcp.json         ← Claude Code config (repo root)
```
