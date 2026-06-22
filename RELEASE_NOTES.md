# Release Notes

Convention: one section per released version, newest at the top. Pre-release / unreleased changes go under **Unreleased**.

---

## v1.0 Build 73 - 2026-06-22

First public 1.0 release of the v2 bytecode-VM rewrite.

### Highlights

- Bytecode compiler + virtual machine with APL-style array vectorization, Eigen-backed linear algebra (`SVD`/`QR`/`DET`/`EIG`) and `FFT`/`IFFT`.
- Native compilation to standalone Windows EXEs (`jdbasic -c`) via an embedded LLVM backend.
- SDL3 graphics + Dear ImGui, a sprite / tilemap / camera / particle game layer, and SDL_mixer audio.
- **MCP server** (`jdbasic --mcp` stdio, `--mcp-http 7321` HTTP) exposing the persistent VM to any MCP-aware client (Claude Code/Desktop, Cursor, Cline, Continue, Zed, Windsurf). Live `jdb_stop` / `jdb_resume` / `jdb_recompile` enable AI pair-coding on a running program. See [`doc/MCP.md`](doc/MCP.md).
- Local AI stack (feature-flag builds): llama.cpp LLMs, ONNX models, dense embeddings, RAG and a k-NN text classifier.
- In-REPL `HELP` and editor hover/signature data driven by the bundled `help.txt` (full builtin reference) and `--dump-symbols` / `--dump-help`.

### Distribution

Three Windows x64 bundles:

- **core** (`jdbasic-core-windows-x64`) - interpreter + MCP server, no LLVM (~4 MB).
- **mcp-native** (`jdbasic-mcp-native-windows-x64`) - adds the LLVM native-compile toolchain (~33 MB).
- **vibe-game-pack** (`jdbasic-vibe-game-pack-windows-x64`) - GFX/ImGui build with ready-to-run game demos (~7 MB).

All bundles ship the app-local Visual C++ runtime (so they run on a clean Windows with no redistributable installed), the full `help.txt` reference, `doc/languages.md`, and `THIRD_PARTY_LICENSES.txt`.

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
