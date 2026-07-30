# Building jdBasic

This document covers building jdBasic from source on **Windows**, **Linux**, and **macOS**.

jdBasic uses a modular feature-flag build system: you pick the features you want and only those subsystems are compiled and linked. The minimal build is just the bytecode VM and the BASIC standard library: no GUI, no network, no AI.

---

## 1. Build flags (all platforms)

| Flag | Adds | Source it pulls in |
|------|------|--------------------|
| `HTTP`      | HTTP/HTTPS client (`HTTP.GET`, `HTTP.POST`, ...)             | `src/http.cpp` (links OpenSSL 3.x) |
| `GFX`       | SDL3 graphics, audio, image, ttf, mixer                      | `src/graphics.cpp` |
| `IMGUI`     | Dear ImGui in-game GUI overlay                               | `libs/imgui/` sources + SDL3 backend |
| `OPENGL`    | `GL.WINDOW` + shaders + VBO/VAO + `MAT4.*` stack             | `src/opengl.cpp` (requires `GFX`) |
| `FTXUI`     | Terminal UI primitives via FTXUI                             | `src/repl_ftxui.cpp` (links FTXUI static libs) |
| `TUI`       | Script-facing `TUI.*` namespace                              | implies `FTXUI` |
| `LLM`       | Local llama.cpp inference (`AI.LOAD_LLM`, `AI.CHAT_STREAM`)  | links `libllama`, `libggml*`. Auto-pulls CUDA libs if `libggml-cuda.a` exists in `libs/llama/` (Linux) |
| `ONNX`      | ONNX Runtime (`AI.LOAD`, `AI.RUN`)                           | links `onnxruntime`. Env `JDB_ONNX_PROVIDER={cuda,tensorrt}` switches execution provider at session creation |
| `NATIVEC`   | LLVM-based native compiler: `jdBasic -c file.jdb -> file.exe`| `src/llvm_codegen.cpp` (links LLVM-C) |
| `MCPSERVER` | `jdBasic --mcp` mode (stdio MCP server, persistent VM)       | `src/mcp_stdio.cpp` |
| `COM`       | Windows COM automation (`CreateObject(...)`)                 | `src/com.cpp` (Windows only) |
| `SERIAL`    | Serial port I/O                                              | `src/serial.cpp` |
| `RELEASE`   | Stamps a build number + date into the binary                 | bumps `build_number.txt` |

Flag rules:

- `OPENGL` requires `GFX`; bare `OPENGL` aborts.
- `TUI` implies `FTXUI`; passing just `TUI` will auto-include the FTXUI sources.
- The native runtime DLL (`jdbrt.dll` / `libjdbrt.so`) must be built with the **same flags** as the interpreter, otherwise generated EXEs miss builtins.

---

## 2. Windows build

### Prerequisites

| Tool | Notes |
|------|-------|
| **Visual Studio 2022** (Community is fine) | C++ workload + Windows 10/11 SDK |
| **Windows 10/11 SDK** | bundled with the VS 2022 installer |
| **OpenSSL 3.x for Windows** (only if `HTTP`) | install to `C:\Program Files\OpenSSL-Win64\` |
| **Python 3.x** | used by `jdb/tv/` helpers and a few demo scripts (not by the build itself) |

`build.bat` invokes `cl.exe` directly via the VS 2022 environment; no `.sln` is needed.

### libs/ layout (Windows)

`libs/` is gitignored. Download and unpack manually:

```
libs/
├── SDL3-3.4.8/                ← from https://github.com/libsdl-org/SDL/releases
│   ├── include/
│   └── lib/x64/{SDL3.dll, SDL3.lib}
├── SDL3_ttf-3.2.2/            ← from https://github.com/libsdl-org/SDL_ttf/releases
├── SDL3_image-3.4.4/          ← from https://github.com/libsdl-org/SDL_image/releases
├── SDL3_mixer-3.2.2/          ← from https://github.com/libsdl-org/SDL_mixer/releases
├── imgui/                     ← clone https://github.com/ocornut/imgui (master)
│   ├── imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp
│   └── backends/{imgui_impl_sdl3.cpp, imgui_impl_sdlrenderer3.cpp}
├── ftxui/                     ← clone https://github.com/ArthurSonzogni/FTXUI (v6.x)
│   └── build/                 ← cmake-built static libs (see below)
├── llama/                     ← from https://github.com/ggml-org/llama.cpp/releases
│   ├── llama.h, llama.lib, llama.dll
│   ├── ggml*.h, ggml*.lib, ggml*.dll
│   └── (optional CUDA) cudart64_12.dll, cublas64_12.dll, ggml-cuda.dll
├── onnxruntime/               ← from https://github.com/microsoft/onnxruntime/releases
│   ├── include/
│   └── lib/{onnxruntime.lib, onnxruntime.dll, onnxruntime_providers_shared.dll}
│   ← for CUDA: download the "gpu" variant; bundles _providers_cuda.dll + _providers_tensorrt.dll
└── LLVM/                      ← LLVM 18.x Windows installer (point install at libs/LLVM/)
    ├── include/llvm-c/
    ├── lib/LLVM-C.lib
    └── bin/LLVM-C.dll
```

If `libs/ftxui/build/libftxui-component.lib` is missing when you pass `FTXUI`, `build.bat` will try to build it automatically via cmake.

### Build commands

```bat
REM Minimal - just the language and console
build.bat

REM Headless network / scripting
build.bat HTTP COM SERIAL

REM Full graphical (no AI)
build.bat GFX IMGUI

REM Full graphical + OpenGL
build.bat GFX IMGUI OPENGL

REM Native compiler enabled (-c)
build.bat NATIVEC

REM MCP-server mode enabled (jdBasic.exe --mcp)
build.bat MCPSERVER

REM Common kitchen-sink dev build (matches the pre-commit gate)
build.bat HTTP GFX IMGUI OPENGL NATIVEC MCPSERVER

REM Everything plus AI
build.bat HTTP GFX IMGUI OPENGL FTXUI TUI LLM ONNX NATIVEC MCPSERVER COM SERIAL

REM Release build with version stamp
build.bat HTTP GFX IMGUI OPENGL NATIVEC MCPSERVER RELEASE
```

After a successful build the script writes:

```
build\jdBasic.exe       ← the interpreter
build\*.dll             ← runtime DLLs needed by the selected features
build\jdb_runtime.obj   ← VM-builtin shim (NATIVEC only)
build\LLVM-C.dll        ← LLVM C-API runtime (NATIVEC only)
```

### Runtime DLL - `build_rt.bat`

`build.bat NATIVEC` only builds the interpreter and the `-c` machinery. The runtime DLL that generated EXEs link against is built by a separate script:

```bat
build_rt.bat HTTP GFX IMGUI OPENGL NATIVEC MCPSERVER
```

**Always pass the same feature flags** to `build_rt.bat` as you passed to `build.bat`. A bare `build_rt.bat` produces a `jdbrt.dll` without `SCREEN`/`RECT`/`SPRITE`/etc., and any generated EXE that uses graphics will fail at runtime.

Rebuild the DLL after any change under `src/graphics.cpp`, `src/gui.cpp`, `src/jdb_runtime.cpp`, `src/sprites.cpp`, `src/tiledmap.cpp`, `src/opengl.cpp`, or `src/imgui/*`.

### Using the native compiler

```bat
build\jdBasic.exe -c myprogram.jdb     REM emits myprogram.exe next to it
myprogram.exe
```

Since 2026-04-23 the `-c` step auto-copies `jdbrt.dll` next to the produced EXE, so you only have to copy the DLL manually if you move the EXE somewhere else.

### Adjusting paths

If your Visual Studio or Windows SDK lives somewhere unusual, edit the top of `build.bat`:

```bat
set MSVC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKV=10.0.26100.0
set OPENSSL=C:\Program Files\OpenSSL-Win64
```

---

## 3. Linux build

The Linux build uses `build.sh` (bash, GNU Make / Ninja for the libs).

### Prerequisites

```bash
sudo apt install -y build-essential cmake ninja-build pkg-config git \
                    libssl-dev llvm-18 llvm-18-dev \
                    libharfbuzz-dev libfreetype-dev libpng-dev libjpeg-dev \
                    libtiff-dev libwebp-dev libfreetype-dev
```

The script picks `llvm-config-18` via `LLVM_CONFIG=llvm-config-18` (or whatever you point it at).

### libs/ layout (Linux)

`build_libs.sh` clones and cmake-builds the SDL3 stack into `libs/`:

```bash
# Clone the SDL3 family at the matching release tags.
for repo in SDL SDL_image SDL_ttf SDL_mixer; do
    git clone https://github.com/libsdl-org/$repo.git libs/$(echo $repo | sed 's/SDL$/SDL3/;s/SDL_/SDL3_/')
done
( cd libs/SDL3        && git checkout release-3.4.8 )
( cd libs/SDL3_image  && git checkout release-3.4.4 )
( cd libs/SDL3_ttf    && git checkout release-3.2.2 )
( cd libs/SDL3_mixer  && git checkout release-3.2.2 )

# Build them all
./build_libs.sh
```

For FTXUI:

```bash
git clone https://github.com/ArthurSonzogni/FTXUI.git libs/ftxui
( cd libs/ftxui && mkdir -p build && cd build && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DFTXUI_BUILD_EXAMPLES=OFF -DFTXUI_BUILD_TESTS=OFF -DFTXUI_BUILD_DOCS=OFF .. && \
    cmake --build . -j )
```

For llama.cpp (CPU-only):

```bash
git clone https://github.com/ggml-org/llama.cpp.git libs/llama_src
./build_libs.sh   # also stages llama_src/ into libs/llama/
```

For ONNX Runtime: download the prebuilt tarball matching your CPU / GPU choice:

```bash
# CPU-only (smaller, simpler)
wget https://github.com/microsoft/onnxruntime/releases/download/v1.20.0/onnxruntime-linux-x64-1.20.0.tgz
# GPU (provides CUDA + TensorRT execution providers)
wget https://github.com/microsoft/onnxruntime/releases/download/v1.20.0/onnxruntime-linux-x64-gpu-1.20.0.tgz
tar xzf onnxruntime-linux-x64*-1.20.0.tgz -C libs/
mv libs/onnxruntime-linux-x64-* libs/onnxruntime
```

The GPU build needs `libcudnn9-cuda-12` installed system-wide (see the NVIDIA section below).

### Build commands

```bash
# Minimal
./build.sh

# Pre-commit-gate equivalent (interpreter + native)
LLVM_CONFIG=llvm-config-18 HTTP=1 GFX=1 IMGUI=1 NATIVEC=1 MCPSERVER=1 ./build.sh

# Full feature set
LLVM_CONFIG=llvm-config-18 HTTP=1 GFX=1 IMGUI=1 NATIVEC=1 MCPSERVER=1 \
            ONNX=1 LLM=1 FTXUI=1 TUI=1 \
            ./build.sh
```

The script produces:

```
build/jdbasic        ← the interpreter (lowercase on Linux)
build/libjdbrt.so    ← runtime DLL for -c-generated executables
```

### Run-convention

The runtime libs live in `build/` and the ONNX `.so` lives under `libs/onnxruntime/lib/`. Both need to be on the loader path:

```bash
export LD_LIBRARY_PATH=build:libs/onnxruntime/lib
./build/jdbasic file.jdb
```

For native EXEs the same `LD_LIBRARY_PATH` applies. The auto-DLL-copy from Windows is replaced by the loader path mechanism on Linux.

### NVIDIA-render variant (CUDA-llama + ONNX-GPU)

When you want a real GPU build (llama.cpp CUDA, ONNX CUDA EP), add cuDNN and rebuild llama.cpp with CUDA.

System packages (one-time):

```bash
sudo apt install -y nvidia-cuda-toolkit         # CUDA 12.0 + nvcc
# NVIDIA's apt repo for cuDNN
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt update && sudo apt install -y libcudnn9-cuda-12
```

Re-cmake llama.cpp with CUDA (Ada / RTX 4000-series = `89`; Ampere = `86`; Blackwell = `120`):

```bash
cd libs/llama_src
rm -rf build && mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF \
      -DLLAMA_BUILD_SERVER=OFF -DLLAMA_CURL=OFF -DGGML_OPENMP=OFF \
      -DGGML_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=89 ..
cmake --build . -j

# Re-stage into libs/llama/
cp -f ../include/llama.h ../../llama/
cp -f ../ggml/include/*.h ../../llama/
find . -name "*.a" -exec cp -f {} ../../llama/ \;
```

Then rebuild jdBasic. `build.sh` auto-detects `libs/llama/libggml-cuda.a` and links `cudart`/`cublas`/`cublasLt`/`cuda`:

```bash
LLVM_CONFIG=llvm-config-18 HTTP=1 GFX=1 IMGUI=1 NATIVEC=1 MCPSERVER=1 \
            ONNX=1 LLM=1 FTXUI=1 TUI=1 \
            ./build.sh
```

For ONNX, swap the libs/onnxruntime to the GPU tarball (above). Then route models through the CUDA EP via the env var:

```bash
JDB_ONNX_PROVIDER=cuda LD_LIBRARY_PATH=build:libs/onnxruntime/lib ./build/jdbasic file.jdb
# or TensorRT EP
JDB_ONNX_PROVIDER=tensorrt ... ./build/jdbasic file.jdb
```

Reference numbers from a real run on an RTX 4070 Ti SUPER:

- llama.cpp Phi-3-mini-4k Q4 (3B, all 33 layers on GPU): **197 tok/s**
- ONNX MATMUL 512x512: CPU-EP 174x speedup vs jdBasic native, CUDA-EP 221x

---

## 4. macOS build

The macOS build works on Apple Silicon (tested on an M1 mini): `./build.sh` with brew-installed dependencies (`llvm@18`, `sdl3`, `sdl3_image`, `sdl3_ttf`, `sdl3_mixer`, `cmake`, `ninja`), same lib-layout as the Linux side. All gate suites pass on arm64. There are no prebuilt macOS binaries yet - build from source.

---

## 5. Running the regression tests

After every build, run the pre-commit gate:

```bash
# Interpreter pass (4 gate suites)
./build/jdBasic.exe tests/gate/comprehensive_test.jdb     # Windows
./build/jdbasic     tests/gate/comprehensive_test.jdb     # Linux/macOS
./build/jdBasic.exe tests/gate/native_test.jdb
./build/jdBasic.exe tests/gate/test_apl_complete.jdb
./build/jdBasic.exe tests/gate/test_apl_pipelines.jdb

# Native pass (compile + run each; note the STRICT twin of native_test)
./build/jdBasic.exe -c tests/gate/comprehensive_test.jdb  && ./tests/gate/comprehensive_test.exe
./build/jdBasic.exe -c tests/gate/native_test.strict.jdb  && ./tests/gate/native_test.strict.exe
./build/jdBasic.exe -c tests/gate/test_apl_complete.jdb   && ./tests/gate/test_apl_complete.exe
./build/jdBasic.exe -c tests/gate/test_apl_pipelines.jdb  && ./tests/gate/test_apl_pipelines.exe

# GUI smoke (Windows pre-commit gate)
./build/jdBasic.exe -c fluppi/rpg_demo.jdb    && ( cd fluppi  && timeout 5 ./rpg_demo.exe )
./build/jdBasic.exe -c jdb/emu/emu_run.jdb    && ( cd jdb/emu && timeout 5 ./emu_run.exe )
```

The native compiler enforces STRICT + EXPLICIT, so the loose `native_test.jdb` is interpreter-only; its strict twin `native_test.strict.jdb` carries the native pass. Delete stale `.exe`s before a `-c` run - a compile error leaves the previous binary in place and it would report a stale green. TUI suites live under `tests/tui/` (need a TUI-enabled build).

Every native suite must end with `ALL TESTS PASSED!` or `0 failed`. `timeout 124` from a GUI smoke is the **good** signal (process killed at the deadline). `exit 139` = segfault. `exit 127` on Windows = missing `jdbrt.dll` next to the EXE. The full test-bank layout is described in [tests/README.md](../tests/README.md).

---

## 6. Packaging a redistributable (Windows)

`dist.bat` builds with the chosen feature set and assembles a clean `dist\jdBasic\` directory:

```bat
REM Usage: dist.bat [FEATURES...] [NOCUDA] [NOBUILD] [CLEAN]

REM Slim graphical build, no LLM
dist.bat GFX IMGUI CLEAN

REM Full build but skip the huge CUDA DLLs
dist.bat HTTP GFX IMGUI LLM ONNX NOCUDA CLEAN

REM Repackage from existing build\ without recompiling
dist.bat GFX IMGUI HTTP NOBUILD
```

Switches:

- `NOCUDA` - skip the CUDA runtime DLLs even when `LLM` is selected
- `NOBUILD` - don't recompile, just repackage from `build\`
- `CLEAN` - wipe `dist\jdBasic\` before assembling

### Target machine requirements (Windows)

The packaged `dist\jdBasic\` only needs the **Visual C++ 2015-2022 Redistributable (x64)** on the target machine: <https://aka.ms/vs/17/release/vc_redist.x64.exe>. Everything else is bundled as a DLL next to the EXE.

---

## 7. Troubleshooting

**`LNK1104: cannot open file 'build\jdBasic.exe'`**
A previous `jdBasic.exe` is still running and holds the file lock. Close or `taskkill /F /IM jdBasic.exe` and rebuild.

**`SDL3.dll not found` at startup (Windows)**
Either the `GFX` flag wasn't selected or the DLL wasn't copied to `build\`. Re-run `build.bat GFX ...`; the script copies the DLLs automatically.

**`libssl-3-x64.dll not found`**
Install OpenSSL 3 to `C:\Program Files\OpenSSL-Win64\` or edit `OPENSSL=` at the top of `build.bat`. Or just drop the `HTTP` flag.

**LLM build fails / unresolved external `llama_*`**
Match the llama.cpp release version with the headers in `libs/llama/`. If you rebuilt llama.cpp yourself, re-run the staging step (copy `*.h`, `*.a`/`*.lib` into `libs/llama/`).

**ONNX build fails / `onnxruntime.lib` not found**
Download ONNX Runtime 1.20.0 from <https://github.com/microsoft/onnxruntime/releases>, unpack so that `libs/onnxruntime/include/` and `libs/onnxruntime/lib/` exist. For GPU, use the `-gpu-` variant of the tarball.

**`MIX_Mixer / MIX_CreateMixerDevice not declared` (Linux)**
Your `libs/SDL3_mixer/` is too old - pre-3.2.2 ships the legacy `Mix_*` API. Run `( cd libs/SDL3_mixer && git checkout release-3.2.2 )` and re-run `./build_libs.sh`.

**NATIVEC build fails / `LLVM-C.lib` not found (Windows)**
Install LLVM 18.x for Windows from <https://github.com/llvm/llvm-project/releases> (pick the `LLVM-18.x.x-win64.exe` installer) and point it at `libs/LLVM/`. The build expects `libs/LLVM/include/llvm-c/`, `libs/LLVM/lib/LLVM-C.lib`, and `libs/LLVM/bin/LLVM-C.dll`.

**Generated EXE exits with code 127 (Windows) and prints nothing**
`jdbrt.dll` is missing next to the EXE. Since 2026-04-23 the `-c` step auto-copies it. If you moved the EXE later, copy `build\jdbrt.dll` next to it.

**Generated EXE returns 127 on Linux**
The runtime `.so` isn't on the loader path. `export LD_LIBRARY_PATH=build:libs/onnxruntime/lib` (or wherever you keep them) before running.

**Edited graphics/GUI code, change doesn't show in generated EXEs**
Generated executables link against the prebuilt `jdbrt.dll` / `libjdbrt.so`, not against `jdBasic.exe` itself. After changing anything under `src/graphics.cpp`, `src/gui.cpp`, `src/jdb_runtime.cpp`, `src/sprites.cpp`, `src/opengl.cpp`, run `build_rt.bat` (Windows) or re-run `./build.sh` (Linux) with the same flags.

**ONNX CUDA-EP crashes at process exit with `malloc(): unsorted double linked list corrupted`**
Cosmetic issue: the singleton `Ort::Env` destructor runs after the CUDA context is torn down. Inference itself works correctly; the crash only fires on shutdown. Workaround pending.

**NVML mismatch after `apt install nvidia-cuda-toolkit`**
`nvidia-smi` may print `NVML: Driver/library version mismatch` after the toolkit pulls in a different driver point-release. Reboot the host. llama.cpp uses `libcuda` directly and is unaffected during the mismatch window.
