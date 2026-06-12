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

`jdb_doc` resolves `doc/languages.md` relative to the binary's own directory first, then falls back to the working directory — so a redistributed bundle (which ships `doc/languages.md` next to the EXE) works without any extra config. Set `"cwd"` only if you also want `jdb_load` to resolve relative file paths against a specific folder.

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

## Client setup notes

### Claude Code / Claude Desktop

Drop the snippet from *Quickstart* into the project's `.mcp.json` (Claude Code) or into `claude_desktop_config.json` (Claude Desktop, OS-specific path under `Application Support` / `%APPDATA%`). Restart the client. Tools appear as `mcp__jdbasic__*`.

### ChatGPT Desktop (Mac / Windows)

ChatGPT Desktop ships native MCP stdio support. Settings → *Developer* → *MCP Servers* → add an entry with the same `command` / `args` / `cwd` shape as above. The browser-only ChatGPT (chat.openai.com) does **not** speak MCP stdio — for that you need the HTTP transport plus a public HTTPS tunnel (Cloudflare Tunnel / ngrok) and your own auth proxy. Treat that as remote code execution and gate it behind a bearer token at minimum.

### Cursor / Cline / Continue / Zed / Windsurf

All five accept the standard MCP server config object. Paths differ (`~/.cursor/mcp.json`, Cline's settings UI, Continue's `~/.continue/config.json`, …) but the JSON snippet is identical to the Claude one — same `command`, `args`, `cwd`.

---

## Tools

All tools share a single persistent VM instance — variables, `FUNC`s, and loaded modules live across calls within one client session.

| Tool | Purpose |
|---|---|
| `echo` | Connectivity smoke test. Returns the input. |
| `jdb_eval` | Execute jdBasic statements; captured stdout is returned and state persists. Optional `result` expression ships a second pure-JSON block; optional `timeout_ms` watchdog (default 30000, 0 = off) parks a runaway chunk without wedging the VM. |
| `jdb_check` | Lint without running. Faster than the `--lint` subprocess. |
| `jdb_load` | Load a `.jdb` file into the VM (so subsequent `jdb_eval` can call its functions). |
| `jdb_recompile` | Re-read a `.jdb` from disk and merge its `FUNC`/`SUB` into the live VM — live-coding while a script is STOPped. |
| `jdb_vars` | List currently-bound variables, each with its shape and a length-capped value preview (`max_chars`). |
| `jdb_funcs` | List user-defined `FUNC` / `SUB` / `ASYNC FUNC` with signatures. |
| `jdb_doc` | Substring lookup against `doc/languages.md`. Authoritative answer for "does jdBasic have function X". |
| `jdb_savews` / `jdb_loadws` | Persist / restore user globals + `FUNC`/`SUB` definitions to a `<name>.jsws` workspace file. |
| `jdb_reset` | Clear the VM to a clean slate (the `CLEARWS` equivalent); workspace files on disk are untouched. |
| `jdb_stop` / `jdb_status` / `jdb_resume` | Pause a running script, report VM state (`running` / `stopped` / `idle`), and continue after a `STOP`. The reader thread fast-paths `jdb_stop`/`jdb_status` so they answer even while another call is busy. |
| `jdb_run_native` | Compile a snippet via the LLVM backend, run the resulting binary, capture stdout. **Requires the Full build (`NATIVEC=1`).** |

### Optional builtin namespaces (build-flag gated)

`jdb_eval` exposes whatever the binary was built with — gate on `OS.FEATURE(name$)`:

- `SQLITE` — `SQL.*`: an embedded SQLite engine, statically linked (no DLL/install). Query a `.db` straight from the VM.
- `PYTHON` — `PYTHON$` / `PY.EVAL` / `PY.SET` / `PY.GET` / `PY.DIR$` / `PY.HELP$`: an embedded CPython interpreter with one persistent namespace, so you can reach numpy/scipy/etc. for what jdBasic can't do natively, handing arrays back and forth with zero subprocess cost. Values convert recursively (list↔array, dict↔map, numpy/`array.array`→native array). The **first** heavy import (e.g. `numpy`) in a cold process can exceed the default 30 s `timeout_ms` — raise it on that first call, then it stays warm.

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
- **Hung calls** — set `JDBASIC_MCP_LOG=1` and check stderr; long-running `jdb_eval` calls may simply be a slow user program (e.g. an infinite `DO LOOP`). The first `import numpy` (or another big package) in a `PYTHON` build can take tens of seconds on a cold process while the OS / antivirus scans its native modules — raise `timeout_ms` on that first call; later imports are instant.
