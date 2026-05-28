# jdBasic + Godot Embedding (spike)

**Branch:** `godot_spinoff`
**Started:** 2026-05-28
**Status:** E0 done - standalone embed C-ABI proven. E1 in progress.

This folder is the working area for the GDExtension spike described in
[release/plan_godot_embedding.md](../../release/plan_godot_embedding.md).
The original plan was held until post-launch (2026-06-04); we started
early on a feature branch so launch focus on `main` is undisturbed.

---

## What works today

`src/jdb_embed_api.h` + `src/jdb_embed_api.cpp` add 6 `extern "C"` exports
to `jdbrt.dll`:

```c
JdbEmbed*  jdb_embed_init(void);
void       jdb_embed_shutdown(JdbEmbed*);
char*      jdb_embed_eval(JdbEmbed*, const char* code);
char*      jdb_embed_load(JdbEmbed*, const char* path);
const char* jdb_embed_last_error(JdbEmbed*);
void        jdb_embed_free(char*);
```

Sanity-tested with `tests/test_embed.c` (build via `tests/build_test_embed.bat`)
- runs in <50 ms, shows persistent state across eval calls, map mutation
matches the live-tweak pattern from the launch video.

---

## Etappen

| Etappe | What | Status |
|---|---|---|
| **E0** | jdb_embed C-ABI + standalone C smoke test | done 2026-05-28 |
| **E1** | GDExtension wrapper `jdb_godot.dll` with `JDBasicVM` class + REPL panel | done 2026-05-28 |
| **E2** | Rotating-cube demo, `on_process(delta)` runs in jdBasic, Slow/Fast/Reverse buttons mutate `rot_speed` live | done 2026-05-28 |
| **E3** | Variant marshalling (`Array`, `Dictionary`, `PackedFloat64Array`); kill the PRINT-and-parse round-trip in `cube.gd` | next |
| **E4** | Embed API gets `stop` / `resume` / `recompile` + a worker thread, so the Stellar-Drift live-loop works against Godot scripts | |
| **E5** | `HEADLESS` build flag on jdbrt - no SDL / no ImGui / no OpenGL, ~10 MB DLL | |
| **E6** | APL procedural-content demo (PackedFloat64Array straight from `IOTA + sin`-style vector ops) | |
| **E7** | Editor plugin: REPL panel that talks to all `JDBasicVM`s in the running scene | |

---

## Installation needed before E1

Atomi installs:

1. **Godot 4.x stable** (download zip, extract to `D:\usr\dev\Godot\`)
   - https://godotengine.org/download/windows/
   - Pick "Standard build" (NOT the .NET / C# variant)
   - Size: ~80 MB
2. **SCons** for the godot-cpp build:
   ```
   pip install scons
   ```
3. **godot-cpp** is added as a git submodule from this repo:
   ```
   cd D:\usr\dev\cc
   git submodule add https://github.com/godotengine/godot-cpp embed/godot/godot-cpp
   cd embed/godot/godot-cpp
   git checkout 4.3-stable   # or matching Godot tag
   ```
   First compile of godot-cpp itself takes ~5-10 min, subsequent builds
   are instant.

Already present:
- MSVC 2022 + Windows SDK
- Python 3.14
- jdBasic build chain (`build.bat` / `build_rt.bat`)

---

## How to rebuild after a jdb_embed_api.cpp change

```
cd D:\usr\dev\cc
build_rt.bat GFX IMGUI HTTP             # produces build\jdbrt.dll
embed\godot\tests\build_test_embed.bat  # smoke test
embed\godot\tests\test_embed.exe        # verify
```

The "BUILD FAILED" line at the end of `build_rt.bat` after a successful
build is a known cosmetic bug in the .bat's IF-block parser when the
output line contains unescaped parentheses. Confirm via `BUILD OK` two
lines above, or check `build\jdbrt.dll` modification time.

---

## Repository hygiene

- `embed/godot/godot-cpp/` will be a git submodule, not checked-in source.
- `embed/godot/bin/` will hold compiled `jdb_godot.dll` - .gitignore'd
  via the existing `*.dll` rule.
- `tests/test_embed.exe` + copied DLLs - .gitignored via `*.exe` / `*.dll`.
- The standalone `tests/test_embed.c` source and `build_test_embed.bat`
  ARE tracked.

---

## `GODOT` compiler directive convention

**Rule:** any code that would only ever exist for the Godot embedding case
is guarded with `#ifdef GODOT` (defined via `/DGODOT` in the build script).
Even though such code naturally lives under `embed/godot/`, the guard is
mandatory so that:

1. A future `jdbrt.dll` build that accidentally pulls a godot-specific .cpp
   into its source list fails fast instead of silently linking against
   godot-cpp symbols.
2. Shared headers (e.g. eventual conversion helpers) can declare Godot
   types behind `#ifdef GODOT` and stay safe to include from anywhere.
3. `grep GODOT src/` reveals the entire Godot integration surface in one
   shot - useful when porting to another host engine (Unity, Bevy, etc.).

**Already-generic on purpose** - these are *not* guarded:

- `src/jdb_embed_api.h` / `src/jdb_embed_api.cpp` - the C-ABI is meant
  to be host-agnostic. A future Unity C# wrapper or a Bevy crate would
  call exactly the same exports.
- `embed/godot/tests/test_embed.c` - the smoke test must run without
  any Godot dependency, that is the whole point.

**Will be guarded** (E1+):

- `embed/godot/src/jdb_godot.cpp` - GDExtension entry point, wraps
  godot-cpp.
- `embed/godot/src/jdbasic_vm.h` / `.cpp` - the `JDBasicVM` Godot Object
  class.
- Any cross-cutting helper added to `src/` that uses godot-cpp types.

---

## Architecture sketch

```
+------------------------------------------------------+
|              Godot 4.x Editor / Runtime              |
|                                                      |
|   GDScript                       (planned, E1+)      |
|       |                                              |
|       | ClassDB::bind_method                         |
|       v                                              |
|   jdb_godot.dll  (GDExtension, built with godot-cpp) |
|       |                                              |
|       | jdb_embed_eval(vm, "code") -> char*          |
|       v                                              |
|   jdbrt.dll      (jdBasic VM, built today)           |
|       |                                              |
|       | (in-process, same address space)             |
|       v                                              |
|   persistent VM state - Map, FUNCs, DIMs all live    |
+------------------------------------------------------+
```

**Today's proof point (E0):** the bottom three layers - `jdbrt.dll` with
the embed API, the persistent VM, and state survival - are real and
working. The `tests/test_embed.exe` is a stand-in for the upper "Godot"
layer; replacing it with a real GDExtension is E1.

---

## Open design questions

These are recorded for E2-E5 but explicitly NOT blocking E1:

- **Variant marshalling** - today `eval` returns the captured PRINT output
  as a string. For richer host integration we need the handle-based path
  laid out in the original plan (`jdb_value_handle` + typed accessors).
- **Yielding** - for 16 ms Godot frames, a long-running jdBasic FUNC
  blocks the main thread. Two options: cooperative `YIELD` opcode in
  jdBasic, or run the VM on its own thread + channel.
- **HEADLESS jdbrt** - today `jdbrt.dll` pulls in SDL3 + ImGui + OpenGL.
  Embedding works as long as host code never calls `SCREEN` / `GL.*`.
  E5 cuts those out and ships a ~10 MB minimal DLL.
- **Asset resolver** - `IMPORT` and file-reading natives look at the
  filesystem. Godot's `res://` and `user://` need an embedder-supplied
  hook so jdBasic scripts can reach packaged assets.
