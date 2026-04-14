# Building jdBasic

This document describes how to build jdBasic from source on Windows. Linux and macOS notes are at the bottom.

jdBasic uses a modular **feature-flag** build system: you pick the features you want and only those subsystems are compiled and linked. The minimal build is just the bytecode VM and the BASIC standard library — no GUI, no network, no AI.

---

## 1. Prerequisites

### Required (always)

| Tool | Notes |
|------|-------|
| **Visual Studio 2022** (Community is fine) | C++ workload + Windows 10/11 SDK |
| **Windows 10/11 SDK** | bundled with VS 2022 installer |

The build script uses `cl.exe` directly via `build.bat`, no `.sln` is needed.

### Optional (only if you select the matching feature flag)

| Feature flag | Dependency | Where to get it |
|--------------|------------|-----------------|
| `HTTP`  | OpenSSL 3.x for Windows | https://slproweb.com/products/Win32OpenSSL.html (install to `C:\Program Files\OpenSSL-Win64`) |
| `GFX`   | SDL3, SDL3_ttf, SDL3_image, SDL2_mixer | see below |
| `IMGUI` | Dear ImGui (vendored) | see below |
| `LLM`   | llama.cpp prebuilt binaries | https://github.com/ggerganov/llama.cpp/releases |
| `ONNX`  | ONNX Runtime 1.24.x | https://github.com/microsoft/onnxruntime/releases |
| `COM`   | (uses Windows COM, no extra deps) | — |
| `SERIAL`| (uses Win32 serial API, no extra deps) | — |

---

## 2. Third-party libraries (not in git)

The `libs/` directory is **gitignored** because some of these dependencies are huge (llama.cpp ≈ 1.2 GB with CUDA backends, ONNX Runtime ≈ 380 MB). You need to download and unpack them manually before building features that require them.

The build script expects this exact layout:

```
libs/
├── SDL3-3.2.16/             ← from https://github.com/libsdl-org/SDL/releases
│   ├── include/
│   └── lib/x64/{SDL3.dll, SDL3.lib}
├── SDL3_ttf-3.2.2/          ← from https://github.com/libsdl-org/SDL_ttf/releases
│   ├── include/
│   └── lib/x64/{SDL3_ttf.dll, SDL3_ttf.lib}
├── SDL3_image-3.2.4/        ← from https://github.com/libsdl-org/SDL_image/releases
│   ├── include/
│   └── lib/x64/{SDL3_image.dll, SDL3_image.lib}
├── SDL2_mixer-2.8.1/        ← from https://github.com/libsdl-org/SDL_mixer/releases
│   ├── include/
│   └── lib/x64/{SDL2_mixer.dll, SDL2_mixer.lib}
├── imgui/                   ← clone https://github.com/ocornut/imgui (master branch)
│   ├── imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp
│   └── backends/{imgui_impl_sdl3.cpp, imgui_impl_sdlrenderer3.cpp}
├── llama/                   ← from https://github.com/ggerganov/llama.cpp/releases
│   ├── llama.h, llama.lib, llama.dll
│   ├── ggml*.h, ggml*.lib, ggml*.dll
│   └── (optional) cudart64_12.dll, cublas64_12.dll, cublasLt64_12.dll, ggml-cuda.dll
└── onnxruntime/             ← from https://github.com/microsoft/onnxruntime/releases
    ├── include/
    └── lib/{onnxruntime.lib, onnxruntime.dll, onnxruntime_providers_shared.dll}
```

> **Tip:** if you only want a quick GUI build without LLM/ONNX, you can ignore the `llama/` and `onnxruntime/` folders entirely.

---

## 3. Building

The entry point is `build.bat`. It takes a list of feature flags as arguments. Anything you don't list is skipped — its source files won't be compiled and its libraries won't be linked.

### Quick examples

```bat
REM Minimal — just the language and console
build.bat

REM Headless network/scripting build
build.bat HTTP COM SERIAL

REM Full graphical build (no AI)
build.bat GFX IMGUI

REM Everything (the full kitchen-sink build)
build.bat COM HTTP SERIAL GFX IMGUI LLM ONNX

REM Release build with version stamp
build.bat COM HTTP SERIAL GFX IMGUI LLM ONNX RELEASE
```

### Feature flags

| Flag | Adds | Compiles |
|------|------|----------|
| `COM`    | Windows COM automation (`CreateObject`, `.invoke`, ...) | `src/com.cpp` |
| `HTTP`   | HTTP/HTTPS client | `src/http.cpp` (links OpenSSL) |
| `SERIAL` | Serial port I/O (`SERIAL.OPEN`, ...) | `src/serial.cpp` |
| `GFX`    | SDL3 graphics & audio | `src/graphics.cpp` (links SDL3 + mixer) |
| `IMGUI`  | Dear ImGui in-game GUI | ImGui sources + SDL3 backend |
| `LLM`    | llama.cpp inference (`LLM.LOAD`, `LLM.CHAT`, ...) | links `llama.lib`, `ggml*.lib` |
| `ONNX`   | ONNX Runtime (`ONNX.LOAD`, `ONNX.PREDICT`, ...) | links `onnxruntime.lib` |
| `RELEASE`| Stamps build number + date into the binary | (no source impact) |

After a successful build the script writes:

```
build\jdBasic.exe          ← the interpreter
build\*.dll                ← all runtime DLLs needed by the selected features
```

The DLLs that match the selected features are auto-copied next to the EXE.

### Adjusting paths

If your Visual Studio or Windows SDK lives somewhere unusual, edit the top of `build.bat`:

```bat
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set OPENSSL=C:\Program Files\OpenSSL-Win64
```

---

## 4. Running the regression tests

After every build, please run the regression suite:

```bat
build\jdBasic.exe tests\comprehensive_test.jdb
build\jdBasic.exe tests\crash_test.jdb
```

You should see:

```
 Sections: 18
 PASS:     184
 FAIL:     0
 ALL TESTS PASSED!
```

```
 PASS: 11  FAIL: 0
 ALL CRASH TESTS PASSED!
```

---

## 5. Packaging a redistributable

`dist.bat` builds with the chosen feature set and assembles a clean `dist\jdBasic\` directory containing only the EXE and the DLLs needed by that feature set.

```bat
REM Usage: dist.bat [FEATURES...] [NOCUDA] [NOBUILD] [CLEAN]

REM Slim graphical build, no LLM
dist.bat GFX IMGUI CLEAN

REM Full build but skip the huge CUDA DLLs
dist.bat COM HTTP SERIAL GFX IMGUI LLM ONNX NOCUDA CLEAN

REM Repackage from existing build\ without recompiling
dist.bat GFX IMGUI HTTP NOBUILD
```

Switches:

- `NOCUDA` — skip the CUDA runtime DLLs even when `LLM` is selected (for machines without NVIDIA GPUs)
- `NOBUILD` — don't recompile, just repackage from `build\`
- `CLEAN` — wipe `dist\jdBasic\` before assembling

### Target machine requirements

The packaged `dist\jdBasic\` only needs **one** thing on the target machine:

- **Visual C++ 2015–2022 Redistributable (x64)**: <https://aka.ms/vs/17/release/vc_redist.x64.exe>

Everything else (SDL, OpenSSL, ONNX, llama, ggml, ...) is bundled as a DLL next to the EXE.

---

## 6. Linux & macOS

The current `build.bat` is Windows-only. A CMake-based build for Linux/macOS is being reworked — see `CMakeLists.txt` (work in progress) and the original v1 instructions on the [`legacy-v1`](https://github.com/AtomiJD/jdBasic/tree/legacy-v1) branch in the meantime.

PRs that bring back parity with the v1 Linux/macOS build are very welcome.

---

## 7. Troubleshooting

**`LNK1104: cannot open file 'build\jdBasic.exe'`**
A previous instance of `jdBasic.exe` is still running and has the file locked. Close it and rebuild.

**`SDL3.dll not found` at startup**
Either the `GFX` flag wasn't selected when you built, or the DLLs weren't copied to `build\`. Re-run `build.bat GFX ...` — the script copies the DLLs automatically.

**`libssl-3-x64.dll not found`**
Install OpenSSL 3 to `C:\Program Files\OpenSSL-Win64\` or edit `OPENSSL=` at the top of `build.bat`. If you're not using HTTPS, just omit the `HTTP` flag.

**LLM build fails / unresolved external `llama_*`**
Download a matching prebuilt release of llama.cpp from <https://github.com/ggerganov/llama.cpp/releases> and unpack the `.lib`, `.dll`, and headers into `libs/llama/`.

**ONNX build fails / `onnxruntime.lib` not found**
Download ONNX Runtime 1.24.x from <https://github.com/microsoft/onnxruntime/releases>, unpack so that `libs/onnxruntime/include/` and `libs/onnxruntime/lib/` exist.
