# Release Notes

Convention: one section per released version, newest at the top. Pre-release / unreleased changes go under **Unreleased**.

---

## Unreleased

### Added

- **MCP server transport** (`jdbasic --mcp`, stdio JSON-RPC). Exposes the persistent VM to MCP clients (Claude Code/Desktop, Cursor, Cline, …). See [`doc/MCP.md`](doc/MCP.md).
- Tools: `echo`, `jdb_eval`, `jdb_check`, `jdb_load`, `jdb_vars`, `jdb_funcs`, `jdb_doc`, `jdb_run_native`.
- HTTP transport for the same protocol (`http://127.0.0.1:7321/mcp`) for remote / containerised use.
- Build flag `MCPSERVER=1` (off by default) on **both Linux (`build.sh`) and Windows (`build.bat`)** - keeps the Core binary lean for users who do not need the server.
- Windows portability for the MCP stdio server: binary-mode stdin/stdout, unbuffered stderr logging, `_popen` / `_pclose` for `jdb_run_native`.

### Changed

- _(none yet)_

### Fixed

- _(none yet)_

### Distribution

Two release tracks introduced:

- **Core** (`jdbasic-core-<os>-<arch>`) - interpreter + MCP server, no LLVM, ~8 MB.
- **Full** (`jdbasic-full-<os>-<arch>`) - adds LLVM-based native compilation, GFX/IMGUI built-ins. Requires LLVM-18 at runtime (or sidecar `libLLVM-18.so`).

---

## v1.0 Build 2 - 2026-04-15

(Existing release; populate retroactively from `git log` if desired.)

---

## Template for new entries

```
## vX.Y Build N - YYYY-MM-DD

### Added
- ...

### Changed
- ...

### Fixed
- ...

### Breaking
- ...

### Distribution
- ...
```
