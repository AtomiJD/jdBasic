---
name: jdbbuild
description: Build jdBasic.exe + jdbrt.dll with feature flags. Default is `HTTP GFX IMGUI NATIVEC MCPSERVER` (mandatory base - MCPSERVER is needed so the mcp-runtime build can serve the MCP tools). Pass extra flags to extend (`COM SERIAL LLM ONNX SQLITE PYTHON`). Auto-kills stale jdBasic.exe on LNK1104 lock. Verify with --version.
---

# Build jdBasic

Working dir: `/d/usr/dev/cc`.

## Choose the flag set

- **Default base (always include):** `HTTP GFX IMGUI NATIVEC MCPSERVER`
  - Dropping HTTP silently skips the HTTP slice in `native_test.jdb` / `comprehensive_test.jdb` so regressions go unnoticed
  - Dropping IMGUI strips SCREEN/RECT/SPRITE from the runtime DLL
  - Dropping NATIVEC means `-c` won't compile
  - Dropping MCPSERVER means the resulting `jdBasic.exe` can't serve as the MCP backend (`--mcp` mode); the `mcp-runtime/` build needs it
- **Full feature** (AI.LOAD/AI.RUN, COM, Serial, LLM): add `COM SERIAL LLM ONNX SQLITE PYTHON`
  - PYTHON embeds CPython (PYTHON$ / PY.*); needs dev headers + `python3xx.dll` (set `JDB_PYTHON_HOME` or use the per-user `pythoncore` package). The build copies `python314.dll` next to the exe.

If **Atomi** passed extra flags after `/jdbbuild`, append them to the base. If he passed `full`, use the full feature set. No flags = base.

## Step 1 - Build the EXE and the DLL

```bash
./build.bat <FLAGS> 2>&1 | tail -10
./build_rt.bat <FLAGS> 2>&1 | tail -3
```

Both must end with `BUILD OK`. The two scripts use `/MP32` parallel - about 14 s for the EXE, 11 s for the DLL on the 32-thread machine.

## Step 2 - On LNK1104, kill and retry (no asking)

If `build.bat` fails with `LNK1104: cannot open file 'build\jdBasic.exe'`, a stale `jdBasic.exe` holds the lock:

```bash
taskkill //F //IM jdBasic.exe
```

Then re-run Step 1. Atomi has standing permission for this - don't ask first.

## Step 3 - Verify

```bash
./build/jdBasic.exe --version
```

Output should list the features you compiled in. Default base = `Features: HTTP, GFX, ImGui, MCP`. Full feature adds `COM, Serial, ONNX, LLM`.

## Notes

- `jdbasic -c file.jdb` auto-copies `jdbrt.dll` next to the produced `.exe` (since 2026-04-23). The old DLL-copy dance is only needed when an EXE is moved to a fresh dir later.
- **`build_rt.bat` is required** after editing `src/graphics.cpp`, `src/gui.cpp`, `src/vm_bridge.cpp`, `src/tiledmap.cpp`, or `src/imgui/*` - `build.bat` only rebuilds the EXE; native `.exes` load the DLL at runtime, so DLL-side fixes are otherwise invisible.
- For `.jdb`-only changes, **don't build at all** - jdBasic interprets at runtime.
