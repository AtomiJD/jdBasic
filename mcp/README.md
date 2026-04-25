# jdBasic MCP server

A [Model Context Protocol](https://modelcontextprotocol.io) server **written in jdBasic itself** — dogfooding the language as a real tool runtime for AI assistants.

## What it exposes

Currently three tools, served as JSON-RPC 2.0 over HTTP POST `/mcp` on `127.0.0.1:7321`:

| Tool       | Purpose                                                          |
|------------|------------------------------------------------------------------|
| `jdb_eval` | Execute jdBasic code on a persistent VM. Captures `PRINT` output. Variables defined in one call survive into the next. |
| `jdb_vars` | List all global variables on the persistent VM (filters out language internals starting with `__`). |
| `echo`     | Connectivity smoke test — returns the message you sent.          |

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

When you open the repo in Claude Code, it'll prompt to trust the MCP server. Once approved, the three tools become available as `mcp__jdbasic__jdb_eval`, `mcp__jdbasic__jdb_vars`, `mcp__jdbasic__echo`. Start the server first (`build/jdBasic.exe mcp/server.jdb` in another terminal), then ask Claude to use them.

## Smoke test from the command line

```bash
# tools/list
curl -s -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'

# evaluate
curl -s -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"jdb_eval","arguments":{"code":"PRINT FAC(20)"}}}'

# inspect persistent state
curl -s -X POST http://127.0.0.1:7321/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"jdb_vars","arguments":{}}}'
```

## Building blocks (jdBasic features used)

| Feature                               | Where                                       |
|---------------------------------------|---------------------------------------------|
| `HTTP.SERVER.START` / `ON_POST`       | `src/http.cpp` — built-in HTTP server       |
| `OUTPUT.CAPTURE_BEGIN/END$/PEEK$`     | `src/vm.cpp` — added to capture PRINT for `jdb_eval` |
| `EXECUTE` (re-entrant code execution) | persistent VM, runs user snippets on the same VM as the server |
| `VARS` native (in script mode)        | `src/main.cpp setup_dynamic_code` — moved out of console-only |
| `JSON.PARSE$` / auto-stringify on map return | parses incoming JSON-RPC, encodes responses |
| `TYPE` / `MAP` / `ARRAY` literals     | tool descriptors and request handling       |

The whole server is ~150 lines of jdBasic.

## Streamable-HTTP transport

The server implements MCP's "Streamable HTTP" transport, which is what Claude Code's `type: "http"` config expects:

- On `initialize`, the server generates a random `Mcp-Session-Id` and returns it as a response header.
- Clients echo it back as a request header on every later call. Unknown / missing session id ⇒ HTTP 404 (forces the client to reinitialize).
- Notifications (`method = "notifications/*"`) get HTTP 202 Accepted with an empty body, per JSON-RPC.

Every incoming request and any handler exception is logged to the server's stderr — keep the server's terminal visible while debugging.

## Known limitations

- **`TRY/CATCH` around `EXECUTE`** triggers a VM bytecode-corruption bug (repro in `tests/test_execute_in_try.jdb`). Workaround: `jdb_eval` does not wrap `EXECUTE` in `TRY`. Errors propagate up to the HTTP server's outer catch and come back as HTTP 500 plain-text — the server stays alive, but the response isn't a structured `isError` MCP block.
- **No request-level mutex.** Concurrent MCP requests would race on the shared VM. Today Claude Code issues calls serially per server, so this is fine, but a `jdb_spawn` background tool (Phase 3+) will need locking.
- **`jdb_vars` includes server internals** (`TOOLS`, `MCP_PORT`, etc.) alongside user globals. A future enhancement could mark "system" globals at server start and filter them out.
- **Sessions never expire on the server side.** A client that drops without sending DELETE leaves an entry in the in-memory `SESSIONS` map until the server restarts. That's harmless for the single-client Claude Code use case but unbounded in principle.

## File layout

```
mcp/
├── README.md     ← this file
└── server.jdb    ← the server, ~150 lines of jdBasic
.mcp.json         ← Claude Code config (repo root)
```
