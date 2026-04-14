# jdBasic — A Persistent Experimental BASIC Environment

**jdBasic** is a modern BASIC interpreter built around a custom **bytecode virtual machine** with APL-style array programming, hot-reloadable code, a persistent REPL workspace, and first-class graphics, GUI, audio, networking, and AI integration.

It combines the immediacy of classic BASIC with powerful built-in capabilities and a "stay in the session" philosophy — no constant restarts, no rebuild loops, just **think and run**.

> **Reduce friction between thinking and running code.**

You can:

- explore ideas in a powerful interactive REPL
- save and restore entire sessions with `SAVEWS` / `LOADWS`
- write vectorized data pipelines using APL-inspired array operators
- prototype graphics, games, and tools with SDL3 + Dear ImGui
- talk to local LLMs (llama.cpp) and run ONNX models inline
- build automation tools, REST clients, and serial-device controllers
- extend the language with native modules

---

## Try jdBasic in your browser

[jdbasic.org/live](https://jdbasic.org/live/index.html) — no installation required.

---

## What's inside (v2)

This is the **v2 rewrite**. Compared to the original tree-walking interpreter, jdBasic now ships with:

- A **bytecode compiler + virtual machine** with inline caches, opcode fusion, and a fast intrusive‑refcount value type
- **APL-style vectorization** — `SIN`, `COS`, `+`, `*`, scatter/gather, `IOTA`, `REDUCE`, `SCAN`, `FILTER`, `SELECT` all operate over arrays in a single op
- **SDL3** graphics with letterboxed logical presentation, `TOGGLE_FULLSCREEN`, and a streaming-texture batch plotter (`GFX.PLOT_POINTS_TEX`) that can push 70k coloured pixels per frame at 30+ FPS from pure BASIC
- **Dear ImGui** integration for instant-mode tools and debuggers
- **llama.cpp** for local LLM inference (CPU + optional CUDA)
- **ONNX Runtime** for classical ML inference
- **HTTP/HTTPS** client (OpenSSL), **COM** automation (Windows), **Serial** I/O for embedded
- **Reactive variables** (`->` operator) with automatic dependency propagation
- **Hot reload** of source files without losing the workspace
- **DAP debug adapter** so you can step through BASIC code from VS Code
- A **persistent REPL workspace** that survives restarts via `SAVEWS`/`LOADWS`

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

---

## Getting started

### Run it
- **Online**: [jdbasic.org/live](https://jdbasic.org/live/index.html)
- **Windows binaries**: see [Releases](https://github.com/AtomiJD/jdBasic/releases)

### Build it from source
- See **[doc/BUILD.md](doc/BUILD.md)** for the full build guide (prerequisites, third-party libraries, feature flags, packaging)

### Learn the language
- **Language reference**: [doc/languages.md](doc/languages.md)
- **Built-in command reference**: [help.txt](help.txt) (also accessible via `HELP` in the REPL)
- **Examples**: browse the [`jdb/`](jdb/) folder

### Tooling
- **VS Code extension**: syntax highlighting and DAP debugger support — see [vscode_extension/vscode_readme.md](vscode_extension/vscode_readme.md)

---

## Project layout

```
src/        — interpreter source (lexer, parser, compiler, VM, runtime modules)
jdb/        — example .jdb programs
doc/        — language reference and build guide
tests/      — regression suite (comprehensive_test.jdb + crash_test.jdb)
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

Contributions, bug reports, and feedback are welcome! Please make sure that the regression suite still passes after your changes:

```bash
build\jdBasic.exe tests\comprehensive_test.jdb
build\jdBasic.exe tests\crash_test.jdb
```

184 + 11 tests should be green.

---

## License

See [LICENSE.txt](LICENSE.txt).
