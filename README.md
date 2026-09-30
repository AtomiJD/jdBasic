# jdBasic

**A BASIC you can change while it runs, by hand or through an AI agent.**

[![CI](https://github.com/AtomiJD/jdBasic/actions/workflows/ci.yml/badge.svg)](https://github.com/AtomiJD/jdBasic/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)
[![Latest release](https://img.shields.io/github/v/release/AtomiJD/jdBasic?label=release)](https://github.com/AtomiJD/jdBasic/releases/latest)
[![Try it online](https://img.shields.io/badge/try_it-in_your_browser-brightgreen)](https://jdbasic.org/live/index.html)
[![YouTube](https://img.shields.io/badge/YouTube-Train_jdBasic-red)](https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ)

[![Claude Code pauses the running shooter over MCP, edits one SUB and resumes it](doc/img/live_patching.jpg)](https://youtu.be/s-BRaSy1EQo)

<p align="center"><em>Claude Code pauses a running game over MCP, rewrites one SUB and lets the game continue with its score and enemies unchanged. <a href="https://youtu.be/s-BRaSy1EQo">Video, 73 seconds</a>.</em></p>

jdBasic is a BASIC interpreter with a bytecode VM and APL-style arrays.
Graphics, GUI, sound, web and local AI are built in. Programs can be compiled
to a native `.exe`, and an MCP server lets an AI agent inspect and change a
program without restarting it.

**[Try it in the browser](https://jdbasic.org/live/index.html)** · **[Download](https://github.com/AtomiJD/jdBasic/releases/latest)** · **[Take the tour](doc/tour.md)**

## Four examples

### 1. An AI agent edits the program while it runs

Press F6 in the running game and the VM pauses. Ask Claude to turn the ship red
and set the lives to 100, and it changes two variables through `jdb_eval`. Ask
it to move the shield bar 20 pixels up, and it edits the source; `jdb_recompile`
then swaps the changed SUB into the paused program and restores the program
pointer. After resuming, the game continues where it stopped with the new
drawing code. Any MCP client works, for example Claude Code, Cursor, Cline or Zed.

[MCP setup and tool reference](doc/MCP.md) · [Blog post about it](https://www.atomijd.onl/blog-post-19.html)

### 2. Arrays instead of loops

```basic
N = 1000000
X = RND(IOTA(N)) * 2 - 1
Y = RND(IOTA(N)) * 2 - 1
PRINT 4 * SUM(X*X + Y*Y <= 1) / N     ' about 3.14
```

<p align="center"><img src="doc/img/pi_montecarlo.png" width="55%" alt="Monte Carlo estimate of Pi: 28 million darts drawn live in an ImGui window"/></p>

Operators and functions work on whole arrays, as in APL: `IOTA`, `SCAN`,
`OUTER`, `GRADE`, matrices, `SVD`, `FFT`. See [From loops to array pipelines](doc/APL_pipeline.md).

### 3. Windows applications compiled to a native `.exe`

```basic
DIM frm = FORM.CREATE("Hello", 320, 200, "MAIN")
DIM btn = FORM.BUTTON(frm, "btnGo", "&Go", 110, 80, 100, 28)

SUB BTNGO_CLICK(e)
    MSGBOX("Hello from jdBasic.", 64, "Hello")
ENDSUB

FORM.RUN(frm)
```

`jdbasic -c hello.jdb` compiles this to `hello.exe`. Events are bound by name.
The controls are Win32 controls, including menus, toolbars, list views, tabs
and MDI windows, and the VS Code extension has a visual form designer.

<p align="center">
  <img src="doc/img/forms_mdi.png" width="49%" alt="An MDI application with real Win32 windows, menus and a grid, about 40 lines of jdBasic"/>
  <img src="doc/img/vscode_forms_designer.png" width="46%" alt="The VS Code visual form designer editing a .jdform layout"/>
</p>

### 4. An operating system written in jdBasic

<p align="center"><img src="doc/img/jdos_prompt.png" width="60%" alt="jdBasic OS: DIR, LOAD pong, COMP compiles it to 3006 bytes of x86, CALL runs it"/></p>

jdBasic OS is compiled with `--target=kernel` and boots on bare x86-64 without
libc or a host operating system. The screen driver, keyboard handling, editor,
RAM disk, an interpreter and a JIT for x86-64 are all written in jdBasic. Its
prompt runs a small C-like language: `?` interprets a line, `??` compiles it to
machine code and runs it, and `COMP` compiles a whole program (Pong in the
screenshot) into 3006 bytes of x86. See [Bare metal](embedded/kernel/README.md).

The jdBasic interpreter also runs on microcontrollers, on an RP2350 (PicoCalc)
and on an ESP32-S3 with a touch display. See [On a board](embedded/).

## Getting started

- **In the browser:** [jdbasic.org/live](https://jdbasic.org/live/index.html), no installation needed.
- **On Windows:** download a bundle from [Releases](https://github.com/AtomiJD/jdBasic/releases/latest), unzip it and run `jdBasic.exe`. The **VB6 pack** covers most features and includes the demos; **mcp-native** is the bundle for working with an AI agent.
- **On Linux and macOS:** build from source as described in [doc/BUILD.md](doc/BUILD.md).

```basic
? PRINT "Hello, jdBasic!"
Hello, jdBasic!
? PRINT SUM(IOTA(100))
5050
? HELP "SORT"
```

> **First run on Windows:** the binaries are code-signed, but the certificate is
> new, so SmartScreen may show *"Windows protected your PC"*. SmartScreen shows
> this for certificates without a download history; no malware was found. Click
> "More info", then "Run anyway", or tick **Unblock** in the properties of the
> `.zip` before extracting it.

After that, the [tour](doc/tour.md) goes from the first `PRINT` to a compiled
`.exe` in about fifteen minutes.

## I want to ...

| ... | Start here |
|---|---|
| learn the language | [The tour](doc/tour.md), then the [video lessons](https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ) |
| look something up | [Language reference](doc/languages.md) (contents at the top, A-Z index at the end) or `HELP "name"` in the REPL |
| program together with an AI agent | [MCP server](doc/MCP.md) |
| write games and graphics | [Sample gallery](jdb/README.md), [graphics functions](doc/languages.md#graphics-and-multimedia-functions) |
| build desktop tools | [ImGui](doc/languages.md#imgui-functions) or [native Windows forms](doc/languages.md#native-windows-forms-form) |
| work with arrays and data | [Array pipelines](doc/APL_pipeline.md), [vector and matrix cookbook](doc/howto-vector-matrix-data.md) |
| build a web app or an API | [Web development](doc/WebDev.md) |
| make music or process audio | [Sequencer](doc/SequencerHelp.md), [Audio FX](doc/AudioFX.md), [FX how-to](doc/HowTo-FX.md) |
| run local LLMs and ML models | [AI and machine learning](doc/languages.md#ai--machine-learning) |
| script Godot 4 | [The Godot embed](embed/godot/README.md) |
| run it on a microcontroller | [On a board](doc/languages.md#on-a-board-rp2350-and-esp32-s3), [ESP32 bring-up](embedded/esp32/README.md) |
| reuse ready-made modules | [Module library](lib/README.md): 45 modules, from TESTKIT to XLSX |
| switch from Python | [Idioms from Python](doc/idioms-from-python.md) |
| build jdBasic or contribute | [Building from source](doc/BUILD.md), [CONTRIBUTING.md](CONTRIBUTING.md) |

All documentation, grouped by topic: [doc/README.md](doc/README.md).

## Gallery

<p align="center">
  <img src="doc/img/apple2.png" width="32%" alt="An Apple II emulator running Applesoft BASIC, itself written in jdBasic"/>
  <img src="doc/img/godot_rpg.jpg" width="32%" alt="A 3D RPG in Godot whose NPCs talk through a local LLM, scripted in jdBasic"/>
  <img src="doc/img/minicalc.png" width="32%" alt="Mini Calc, a spreadsheet with formulas in an ImGui window"/>
</p>
<p align="center">
  <img src="doc/img/sequencer_oscilloscope.png" width="32%" alt="The live music sequencer with an ImGui oscilloscope"/>
  <img src="doc/img/tui_minesweeper.png" width="32%" alt="Minesweeper in the terminal"/>
  <img src="doc/img/garden_dashboard.png" width="32%" alt="The scoring dashboard of a garden championship, with live standings"/>
</p>
<p align="center"><em>An Apple II emulator, a Godot RPG with LLM characters, a spreadsheet, a music sequencer, Minesweeper in the terminal and the scoring app of a garden championship, all written in jdBasic. The <a href="jdb/README.md">sample gallery</a> has more than 250 other programs.</em></p>

<details>
<summary><b>Feature list</b></summary>

- Bytecode compiler and VM with inline caches, opcode fusion and a refcounted value type
- APL-style vectorization: `SIN`, `+`, `*`, scatter/gather, `IOTA`, `REDUCE`, `SCAN`, `FILTER` and `SELECT` work on whole arrays
- Linear algebra and DSP based on Eigen: `SVD`, `QR`, `DET`, `EIG`, `FFT`/`IFFT`
- Native compiler via LLVM: `jdbasic -c program.jdb` writes a standalone `.exe`
- SDL3 graphics with a batch plotter that draws 70 000 coloured pixels per frame at more than 30 FPS ([`universe.jdb`](jdb/demos/graphics/universe.jdb))
- Dear ImGui for immediate-mode tools, native Win32 forms with a visual designer
- Music sequencer (`SOUND.*`) and an effect chain for guitar and synth
- llama.cpp for local LLMs, ONNX Runtime for ML models
- HTTP/HTTPS client and server, COM automation, serial I/O, SQLite
- Reactive variables (`->`), hot reload, a persistent workspace (`SAVEWS`/`LOADWS`)
- DAP debug adapter and a VS Code extension with lint, hover and the form designer
- MCP server (`jdbasic --mcp`) for Claude Code, Cursor, Cline and other MCP clients
- Godot 4 embed: a `.jdb` file is a Godot script, and `GDX.*` reaches nodes, physics, 3D and audio (experimental; [`godot/`](godot/) has four projects)
- Microcontroller ports of the interpreter (RP2350, ESP32-S3), and a kernel target (`--target=kernel`) that compiles jdBasic into a freestanding x86-64 operating system
- A library of 45 modules written in jdBasic

The original v1 codebase is kept on the [`legacy-v1`](https://github.com/AtomiJD/jdBasic/tree/legacy-v1) branch.
</details>

<details>
<summary><b>The 14 video lessons</b></summary>

The **Train jdBasic** series goes from "Hello, World" to native compilation in
episodes of 5 to 10 minutes. The videos are produced by a jdBasic program that
handles voice, screen recording, FFmpeg and the upload, see [`jdb/tv/`](jdb/tv/).

| # | Lesson | Topic |
|---|---|---|
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
</details>

<details>
<summary><b>Project layout</b></summary>

```
src/        interpreter, compiler, VM (vm.cpp + vm_builtins_*.cpp), runtime modules
lib/        the module library, written in jdBasic (lib/README.md)
jdb/        example programs, start at jdb/README.md
doc/        documentation (doc/README.md), screenshots in doc/img/
tests/      regression bank; tests/gate/ holds the pre-commit gate
embedded/   microcontroller ports (RP2350/PicoCalc, ESP32-S3) and bare metal
embed/      the Godot 4 GDExtension;  godot/ has the Godot projects
selfhost/   jdbc, a jdBasic compiler written in jdBasic
wasm/       the browser build behind jdbasic.org/live
fluppi/     "Vallys Reise", a top-down RPG
tools/      helper scripts (doc index, doc examples, syntax highlighting, ...)
bridges/    optional native bridges;  modules/  modules IMPORT finds by name
vscode_extension/  the VS Code extension (.vsix)
libs/       vendored third-party sources; SDL3, LLVM and the rest per doc/BUILD.md
```
</details>

## Contributing

Bug reports, ideas and code are welcome. [CONTRIBUTING.md](CONTRIBUTING.md)
describes the build, the conventions and the pre-commit gate: four suites that
must report `0 failed` in the interpreter and, for compiler changes, also as
native programs. The test bank is described in [tests/README.md](tests/README.md).

## License

MIT, see [LICENSE.txt](LICENSE.txt). Third-party components are listed in [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt).
