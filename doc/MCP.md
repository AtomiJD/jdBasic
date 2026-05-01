# jdBasic MCP Server

The jdBasic binary doubles as a [Model Context Protocol](https://modelcontextprotocol.io/) server, exposing the persistent jdBasic VM to any MCP-aware client (Claude Code/Desktop, Cursor, Cline, Continue, Zed, Windsurf, custom agents via the official SDKs, …).

It gives an LLM a fast, local, deterministic sandbox for vectorised array work, APL-style data pipelines, and — optionally — native compilation of jdBasic programs.

---

## Quickstart

### 1. Install jdBasic

Either build from source or grab a pre-built binary from the [releases page](https://github.com/AtomiJD/jdBasic/releases). The "Core" asset is enough for the MCP server.

```bash
# Linux / macOS
./build.sh MCPSERVER=1

# Windows (Developer Command Prompt for VS 2022)
build.bat MCPSERVER HTTP
```

### 2. Wire it into your MCP client

Add an entry to your client's MCP config (`.mcp.json`, `claude_desktop_config.json`, `mcp.json`, etc.):

```json
{
  "mcpServers": {
    "jdbasic": {
      "command": "/absolute/path/to/jdbasic",
      "args": ["--mcp"]
    }
  }
}
```

Optional environment:

- `JDBASIC_MCP_LOG=1` — write JSON-RPC traffic to stderr for debugging.

### 3. Try it

In your client, ask the agent to call `jdb_eval` with `code: "PRINT SUM(IOTA(20))"`. You should see `210`.

---

## Transports

`jdbasic --mcp` speaks **stdio** JSON-RPC and works with every MCP client.

A second transport, an **HTTP** server, is available for remote / containerised setups:

```json
{
  "mcpServers": {
    "jdbasic": { "type": "http", "url": "http://127.0.0.1:7321/mcp" }
  }
}
```

Run `jdbasic --mcp-http 7321` in a long-lived terminal or systemd unit. **Bind to localhost only** unless you put authentication in front — there is no built-in auth and the tools can execute arbitrary code (see *Security* below).

---

## Tools

All tools share a single persistent VM instance — variables, `FUNC`s, and loaded modules live across calls within one client session.

| Tool | Purpose |
|---|---|
| `echo` | Connectivity smoke test. Returns the input. |
| `jdb_eval` | Execute jdBasic statements. Captured stdout is returned. State persists. |
| `jdb_check` | Lint without running. Faster than the `--lint` subprocess. |
| `jdb_load` | Load a `.jdb` file into the VM (so subsequent `jdb_eval` can call its functions). |
| `jdb_vars` | List currently-bound variables and their tags / shapes. |
| `jdb_funcs` | List user-defined `FUNC` / `SUB` / `ASYNC FUNC` with signatures. |
| `jdb_doc` | Substring lookup against `doc/languages.md`. Authoritative answer for "does jdBasic have function X". |
| `jdb_run_native` | Compile a snippet via the LLVM backend, run the resulting binary, capture stdout. **Requires the Full build (`NATIVEC=1`).** |

### Why the persistent VM matters

Most code-execution MCP servers spawn a fresh interpreter per call. jdBasic does not — `jdb_eval` reuses the same VM, so:

- Heavy data stays in memory between turns (no re-loading a CSV every call).
- Function definitions accumulate naturally — define once, call many times.
- The agent can iterate on a hot dataset without paying startup cost on every step.

### Why `jdb_doc` matters

Models hallucinate language-specific builtins constantly. `jdb_doc` gives the agent a way to look up the real name in one tool call (e.g. asking for `MAP` returns the entry for `SELECT`), which catches mistakes before they hit `jdb_eval`.

---

## Distribution

### Release tracks

The MCP server build does **not** require LLVM. Native compilation does. Ship two assets:

| Asset | Build flags | Size | Dependencies |
|---|---|---|---|
| `jdbasic-core-<os>-<arch>` | `MCPSERVER=1 HTTP=1` | ~8 MB | OpenSSL (system) |
| `jdbasic-full-<os>-<arch>` | `MCPSERVER=1 HTTP=1 GFX=1 IMGUI=1 NATIVEC=1` | ~25 MB + libs | OpenSSL, SDL3, LLVM-18, `libjdbrt.so` |

Most MCP users only need Core. Full is for users who want `jdb_run_native` or the GFX/IMGUI built-ins inside `jdb_eval`.

### LLVM specifically

The Full build links **dynamically** against `libLLVM-18.so` (~100 MB system library). Three options:

1. **System install** (recommended): Linux `apt install llvm-18`, macOS `brew install llvm@18`, Windows official LLVM 18 installer. Smallest download, easy update path.
2. **Sidecar bundle**: ship `libLLVM-18.so` in the release tarball next to `jdbasic`. Adds ~100 MB but zero-config for the user.
3. **Static link**: possible but produces a 200+ MB binary. Not recommended.

Generated native `.exe`s never link LLVM — they only need `libjdbrt.so` shipped alongside them.

### Where to list it

- GitHub Releases — primary distribution.
- [`modelcontextprotocol/servers`](https://github.com/modelcontextprotocol/servers) — the canonical community list. Open a PR with a one-line entry.
- [Smithery](https://smithery.ai), [Glama](https://glama.ai/mcp/servers) — discoverability via MCP registries.

---

## Security

`jdb_eval` and `jdb_run_native` execute arbitrary code on the host. The VM exposes:

- Filesystem read/write (`OPEN`, `KILL`, `MKDIR`, …)
- Process spawn (`OS.EXEC`, `SHELL`)
- Network I/O (`HTTP.GET`, sockets)
- Native FFI (`DECLARE FUNC`)

This is intentional — it is a developer tool. **Do not expose the HTTP transport to the public internet**, and treat the stdio server as you would treat a local shell. Run it under your own user, not as root.

---

## Troubleshooting

- **Client shows "tool not found"** — confirm `jdbasic --version` lists `MCP` in its features. If not, the binary was built without `MCPSERVER=1`.
- **`jdb_run_native` errors with "native backend not built in"** — you have the Core build. Switch to Full.
- **Hung calls** — set `JDBASIC_MCP_LOG=1` and check stderr; long-running `jdb_eval` calls may simply be a slow user program (e.g. an infinite `DO LOOP`).
