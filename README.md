# jdBasic — A Persistent Experimental BASIC Environment

**jdBasic** is a modern BASIC interpreter built around a custom **bytecode virtual machine** with APL-style array programming, hot-reloadable code, a persistent REPL workspace, and first-class graphics, GUI, audio, networking, AI, and **Godot-engine** integration.

It combines the immediacy of classic BASIC with powerful built-in capabilities and a "stay in the session" philosophy — no constant restarts, no rebuild loops, just **think and run**.

> **Reduce friction between thinking and running code.**

> **🎥 Train jdBasic** is on YouTube — 14 video lessons covering everything from PRINT to native compilation, all auto-generated and voiced by jdBasic itself. **[Watch the playlist →](https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ)**

You can:

- explore ideas in a powerful interactive REPL
- save and restore entire sessions with `SAVEWS` / `LOADWS`
- write vectorized data pipelines using APL-inspired array operators
- prototype graphics, games, and tools with SDL3 + Dear ImGui
- embed in **Godot 4** and script whole 3D games, tools, and visualizers in pure BASIC
- talk to local LLMs (llama.cpp) and run ONNX models inline
- build automation tools, REST clients, and serial-device controllers
- extend the language with native modules

---

## Try jdBasic in your browser

[jdbasic.org/live](https://jdbasic.org/live/index.html) — no installation required.

---

## Watch the lessons

The **Train jdBasic** YouTube series walks through the language from
"Hello, World" to native compilation — 14 episodes, 5–10 minutes each,
with auto-generated chapters and code-on-screen as it happens.

▶️ **[Full playlist on YouTube](https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ)**

| #  | Lesson | Topic |
|----|--------|-------|
| 01 | [Hello jdBasic](https://youtu.be/4qvPFoqxPHE) | PRINT, DIM, basic types |
| 02 | [If and For](https://youtu.be/RTI-f9cHldI) | IF/ELSE, FOR/NEXT, FizzBuzz |
| 03 | [Arrays](https://youtu.be/V33CGCt1zB8) | Vector ops, broadcasting, reductions |
| 04 | [Strings](https://youtu.be/NUhdrMU9T9c) | Slice, search, SPLIT |
| 05 | [Functions and SUBs](https://youtu.be/_4Q7qR1sA3Q) | FUNC, SUB, recursion |
| 06 | [Maps](https://youtu.be/vp0tCYa6__A) | Key-value data |
| 07 | [INPUT and DO Loops](https://youtu.be/1Sd7jCzY8Zc) | User-driven programs |
| 08 | [File I/O](https://youtu.be/gbFvgqIYNMM) | TXTWRITER, TXTREADER$ |
| 09 | [Graphics](https://youtu.be/qtBCNaVaRLA) | SCREEN, shapes, colours |
| 10 | [Modules](https://youtu.be/fg0ib3SgGio) | EXPORT, IMPORT, code reuse |
| 11 | [REPL Workflow](https://youtu.be/Noa4mqwEZ5w) | PRETTY, LINT, SAVEWS |
| 12 | [Higher-Order Functions](https://youtu.be/WPpzO0tHJNE) | SELECT, FILTER, REDUCE, lambdas |
| 13 | [HTTP and JSON](https://youtu.be/ecq8uZHAV7U) | Talk to the web |
| 14 | [Native Compilation](https://youtu.be/4DlthnUo56w) | Compile to .exe with `jdbasic -c` |

The entire production pipeline — voice synthesis, screen recording,
FFmpeg compositing, even the YouTube uploads — is itself a jdBasic
program. See [`jdb/tv/`](jdb/tv/) if you want to fork the rig.

---

## What's inside (v2)

This is the **v2 rewrite**. Compared to the original tree-walking interpreter, jdBasic now ships with:

- A **bytecode compiler + virtual machine** with inline caches, opcode fusion, and a fast intrusive‑refcount value type
- **APL-style vectorization** — `SIN`, `COS`, `+`, `*`, scatter/gather, `IOTA`, `REDUCE`, `SCAN`, `FILTER`, `SELECT` all operate over arrays in a single op
- **Eigen-backed linear algebra & DSP** — `SVD`, `QR`, `DET`, `EIG`, and `FFT`/`IFFT` as first-class array builtins
- **SDL3** graphics with letterboxed logical presentation, `TOGGLE_FULLSCREEN`, and a streaming-texture batch plotter (`GFX.PLOT_POINTS_TEX`) that can push 70k coloured pixels per frame at 30+ FPS from pure BASIC
- **Dear ImGui** integration for instant-mode tools and debuggers
- **Godot 4 embed (GDExtension)** — run jdBasic *inside* Godot: `.jdb` files become engine scripts and the `GDX.*` suite reaches the whole engine (nodes, physics, 3D meshes, audio, signals), with an in-editor debugger and Inspector-exposed variables — see [`embed/godot/`](embed/godot/)
- **Music sequencer** — a tracker-style `SOUND.*` engine (patterns, voices, effects) with a device-less `SOUND_DSP` pull mode for embedding — see [`doc/SequencerHelp.md`](doc/SequencerHelp.md)
- **llama.cpp** for local LLM inference (CPU + optional CUDA)
- **ONNX Runtime** for classical ML inference
- **HTTP/HTTPS** client (OpenSSL), **COM** automation (Windows), **Serial** I/O for embedded
- **Reactive variables** (`->` operator) with automatic dependency propagation
- **Hot reload** of source files without losing the workspace
- **DAP debug adapter** so you can step through BASIC code from VS Code
- A **persistent REPL workspace** that survives restarts via `SAVEWS`/`LOADWS`
- An **MCP server** (`jdbasic --mcp`) that exposes the persistent VM to LLM agents like Claude Code, Cursor, or Cline — see [`doc/MCP.md`](doc/MCP.md)

> The original v1 codebase is preserved on the [`legacy-v1`](https://github.com/AtomiJD/jdBasic/tree/legacy-v1) branch and the [`v1-legacy`](https://github.com/AtomiJD/jdBasic/releases/tag/v1-legacy) tag for archival and bugfixes.

---

## Language tour

### 1. Reactive variables

```basic
DIM BaseValue AS INTEGER = 10
DIM Multiplier AS INTEGER = 5

DIM Result AS REACT INTEGER
Result -> BaseValue * Multiplier

PRINT Result    ' 50

Multiplier = 10
PRINT Result    ' 100  — updated automatically
```

### 2. Vectorized array math (APL-style)

```basic
' Generate 10 numbers, keep > 5, multiply by 10
result = IOTA(10) |> FILTER(LAMBDA x -> x > 5, ?) |> SELECT(LAMBDA v -> v * 10, ?)
PRINT result    ' [60 70 80 90 100]
```

```basic
' All trig is vectorized — one call processes the whole array
DIM angles = IOTA(360) * (PI / 180)
DIM sines  = SIN(angles)
DIM cosines = COS(angles)
```

### 3. Immediate-mode GUI

```basic
SCREEN 800, 600, "My Tool"
DIM BgColor[4] = [0.2, 0.3, 0.3, 1.0]

DO
    CLS
    IF GUI.BEGIN("Control Panel", 50, 50, 300, 200) THEN
        GUI.TEXT "Welcome to jdBasic GUI"
        GUI.SEPARATOR()
        IF GUI.BUTTON("Click Me") THEN
            PRINT "Clicked at " + TIME$
        ENDIF
        GUI.COLOR("Background", BgColor)
    ENDIF
    GUI.END()
    SCREENFLIP
    SLEEP 16
LOOP UNTIL INKEY$() = "q"
```

### 4. High-performance graphics

The [`jdb/universe.jdb`](jdb/universe.jdb) demo plots **70 000 coloured pixels per frame at 30+ FPS** from pure BASIC, by combining vectorized inner loops with a single GPU upload via `GFX.PLOT_POINTS_TEX`.

```bash
build\jdBasic.exe jdb\universe.jdb
```

### 5. APL-style pipelines

Vectorized arithmetic + bitops let you push real workloads — physics, cellular automata, SAT, DSP — through whole-array operations instead of per-cell loops. See **[doc/APL_pipeline.md](doc/APL_pipeline.md)** for a tutorial walking from "tight FOR loops" to "one line per update step" using the demos under `jdb/bench/` and `jdb/`. Highlights:

- `jdb/life_demo.jdb` — live Conway 200 × 150 at 60 FPS via an ONNX 3×3-conv backend
- `jdb/boids_apl.jdb` — 5 000 particles at ~630 FPS, all-vector update
- `jdb/synth_apl.jdb` — additive synthesis, 4 096 samples/frame in five vector ops
- `jdb/bench/life_bench.jdb` / `jdb/bench/mandelbrot_bench.jdb` — when APL form wins (Conway, 4–13×) and when it loses (Mandelbrot, ~4×)

Numbers from the latest run: **[jdb/bench/Results.md](jdb/bench/Results.md)**.

---

## jdBasic in Godot

jdBasic embeds in **Godot 4** as a GDExtension: a `.jdb` file *is* a Godot script. The `GDX.*` native suite reaches the engine directly — nodes, the physics server, 3D meshes and materials, audio buses, and signals — so you can build whole scenes without a line of GDScript and step through them in the in-editor debugger.

```basic
EXTENDS Node3D            ' this .jdb file IS the Godot script

DIM cube_h = 0

SUB _ready()
    cube_h = GDX.CALL(GDX.SELF(), "get_node", "Cube")
ENDSUB

SUB _process(delta)
    ' spin the cube; rotation:y indexes into the Vector3 property
    GDX.SET(cube_h, "rotation:y", GDX.GET(cube_h, "rotation:y") + delta)
ENDSUB
```

Three projects under [`godot/`](godot/) show how far it goes — all logic in jdBasic, running on Windows and Linux (NVIDIA/CUDA and AMD/Vulkan):

- **`rpg-native`** — a 3D action-RPG: procedural heightmap terrain, a data-driven dungeon, day/night, an item shop, and **NPCs whose entire dialogue brain is a local LLM** (llama.cpp) with RAG over the world lore — quests, memory, and tool-calling, all in jdBasic.
- **`audioviz`** — a live **microphone → FFT → 3D spectrum waterfall**, built on the new `FFT` builtin with an orbiting camera.
- **`jd-one`** — a sandbox of small scenes (Breakout, a spinning cube, a reactive audio tunnel, GDScript-vs-jdBasic benchmarks).

See [`embed/godot/README.md`](embed/godot/README.md) for the embed architecture and the `GDX.*` reference.

---

## Getting started

### Run it
- **Online**: [jdbasic.org/live](https://jdbasic.org/live/index.html)
- **Windows binaries**: see [Releases](https://github.com/AtomiJD/jdBasic/releases)

### Build it from source
- See **[doc/BUILD.md](doc/BUILD.md)** for the full build guide (prerequisites, third-party libraries, feature flags, packaging)

### Learn the language
- **Video tutorials**: [Train jdBasic playlist on YouTube](https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ) — 14 episodes, beginner to native compile
- **Language reference**: [doc/languages.md](doc/languages.md)
- **Built-in command reference**: [help.txt](help.txt) (also accessible via `HELP` in the REPL)
- **Examples**: browse the [`jdb/`](jdb/) folder

### Tooling
- **VS Code extension**: syntax highlighting and DAP debugger support — see [vscode_extension/vscode_readme.md](vscode_extension/vscode_readme.md)

---

## Project layout

```
src/        — interpreter source (lexer, parser, compiler, VM, runtime modules)
embed/      — Godot GDExtension (jdb_godot) that embeds the VM in Godot 4
godot/      — Godot projects: rpg-native (LLM RPG), audioviz, jd-one sandbox
jdb/        — example .jdb programs
doc/        — language reference and build guide
tests/      — regression suite; tests/gate/ holds the four pre-commit suites
fonts/      — bundled TTF fonts
sfx/        — bundled sound effects
resources/  — icon, manifest, version info
syntaxes/   — editor syntax highlighting files
tools/      — auxiliary scripts
vscode_extension/ — VS Code extension (.vsix) and install instructions
libs/       — third-party libraries (not in git, see doc/BUILD.md)
build/      — compile output (not in git)
dist/       — packaged distribution (not in git)
```

---

## Contributing

Contributions, bug reports, and feedback are welcome! Please make sure that the regression suite still passes after your changes — four test files cover the language, the native compiler, and the APL pipeline:

```bash
build\jdBasic.exe tests\gate\comprehensive_test.jdb
build\jdBasic.exe tests\gate\native_test.jdb
build\jdBasic.exe tests\gate\test_apl_complete.jdb
build\jdBasic.exe tests\gate\test_apl_pipelines.jdb
```

All four should report `0 failed`. For changes that touch the LLVM codegen, run each suite again through the compiler — `build\jdBasic.exe -c tests\gate\<suite>.jdb && tests\gate\<suite>.exe` — so both the interpreter and native paths stay green.

---

## License

See [LICENSE.txt](LICENSE.txt).
