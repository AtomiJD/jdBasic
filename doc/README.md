# jdBasic documentation

**Docs home** · [Tour](tour.md) · [Language reference](languages.md) · [Module library](../lib/README.md)

Four ways in, depending on where you are:

| [Start](#start) | [Guides](#guides) | [Reference](#reference) | [Contribute](#contribute) |
|---|---|---|---|
| never used jdBasic | want to get a job done | need the exact rules | work on jdBasic itself |

## Start

| | |
|---|---|
| [Project README](../README.md) | What jdBasic is, in one screen: the four things it does that other BASICs do not, and how to run it. |
| **[A tour of jdBasic](tour.md)** | **Start here.** Fifteen minutes from the first `PRINT` to a compiled `.exe`; every example is run by CI. |
| [Browser playground](https://jdbasic.org/live/index.html) | Try the language without installing anything. |
| [Train jdBasic on YouTube](https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ) | 14 video lessons, from "Hello, World" to native compilation. |
| [Sample gallery](../jdb/README.md) | 250+ complete programs by domain: games, emulators, tools, web apps, AI, sound. |

## Guides

Task-oriented, each with runnable code.

**Arrays and data**

| | |
|---|---|
| [From loops to array pipelines](APL_pipeline.md) | Rewriting tight `FOR` loops as whole-array update steps - and when the array form loses. |
| [Vectors, matrices and data](howto-vector-matrix-data.md) | Cookbook: build, transform, group, sort, reshape, dates, rendering. |
| [Idioms from Python](idioms-from-python.md) | Python-to-jdBasic cheat sheet with the gotchas; good context for an AI assistant too. |

**Applications**

| | |
|---|---|
| [Web apps: JDWEB and TMPL](WebDev.md) | From a one-line server to an app with a theme, cookie login and SQLite. |
| [Native Windows forms](languages.md#native-windows-forms-form) | Real Win32 windows with events bound by name; the VS Code [form designer](../vscode_extension/vscode_readme.md). |
| [ImGui](languages.md#imgui-functions) | Immediate-mode tools and dashboards inside a graphics window. |
| [Vallys Reise](../fluppi/README.md) | A complete top-down RPG in jdBasic, with its [structure](../fluppi/doc/PROJECT_STRUCTURE.md) and [tilemap guide](../fluppi/doc/TILEMAP_GUIDE.md). |

**Sound**

| | |
|---|---|
| [Sequencer](SequencerHelp.md) | The `SOUND.*` live-coding sequencer and synth: tracks, voices, effects, patterns. |
| [Audio FX](AudioFX.md) | The `FX.*` effect chain, FFT analysis and a cookbook of named tones. |
| [FX how-to](HowTo-FX.md) | Offline render, live guitar through the chain, the ImGui pedalboard. |

**AI**

| | |
|---|---|
| [MCP server](MCP.md) | Let Claude Code, Cursor, Cline & Co. run, inspect and live-patch a jdBasic program; client configs and the tools. |
| [MCP server in jdBasic](../jdb/demos/mcp_server/README.md) | The same protocol served by a jdBasic program. |
| [AI and machine learning](languages.md#ai--machine-learning) | Local LLMs (llama.cpp), ONNX models, embeddings and RAG. |

**Platforms**

| | |
|---|---|
| [On a board](languages.md#on-a-board-rp2350-and-esp32-s3) | What the RP2350 and ESP32-S3 builds offer; then [Pico/PicoCalc](../embedded/pico/README.md), [ESP32-S3](../embedded/esp32/README.md), [board demos](../embedded/pico/demos/README.md) and [board lessons](../embedded/lessons/README.md). |
| [Bare metal](../embedded/kernel/README.md) | jdBasic OS: written in jdBasic, compiled with `--target=kernel`, boots on x86-64 and runs a small language of its own. |
| [Godot 4](../embed/godot/README.md) | The GDExtension: a `.jdb` file as a Godot script; [audio](../embed/godot/AUDIO.md), [input](../embed/godot/INPUT.md), [signals](../embed/godot/SIGNALS.md), and the [RPG setup](../godot/rpg-native/README_SETUP.md). |
| [VS Code](../vscode_extension/vscode_readme.md) | The extension: highlighting, lint, hover, debugger, form designer. |

## Reference

| | |
|---|---|
| **[Language reference](languages.md)** | Every statement, function and build-flag-gated API. Contents at the top, A-Z index at the end. Read at runtime by the MCP `jdb_doc` tool, so its one-bullet-per-function format is load-bearing. |
| [help.txt](../help.txt) | One entry per command, served by `HELP "name"` in the REPL. |
| [Module library](../lib/README.md) | 45 modules written in jdBasic, one page each: testing, web, data and files, text, money. |
| [Release notes](../RELEASE_NOTES.md) | What changed in each release, and what is not released yet. |
| [Benchmarks](../jdb/bench/Results.md) | jdBasic against C++ and Python on two CPU-bound benchmarks, interpreted and compiled. |

## Contribute

| | |
|---|---|
| [CONTRIBUTING.md](../CONTRIBUTING.md) | How to send changes: the gate, commit conventions, documenting a new builtin. |
| [Building from source](BUILD.md) | Prerequisites, third-party libraries, feature flags, Windows/Linux/macOS, packaging. |
| [Coding style](CODING_STYLE.md) | Conventions for the C++ core and the jdBasic sources. |
| [Test bank](../tests/README.md) | The regression tests and the pre-commit gate: suites, naming, GUI smoke. |
| [Browser build](../wasm/DEPLOY.md) | How the playground at jdbasic.org/live is built and deployed. |
| [Video production](../jdb/tv/README.md) | The jdBasic program that records, voices and uploads the video lessons. |
| `src/builtin_sigs.h` | Return kind and call behaviour of every builtin, shared by the VM and the native compiler. |
| `tools/check_builtin_sigs.jdb` | Checks every name in `builtin_sigs.h` against the documentation; run by CI. |
| `tools/gen_doc_index.jdb` | Regenerates the contents and A-Z index of `languages.md`; `--check` is run by CI. |
| `tools/check_doc_examples.jdb` | Runs every `basic` block that has a `text` block after it and compares the output; CI runs it on the tour. |

## Design notes

Plans and working notes, not user documentation: [local AI co-processor](design/agent_coprocessor_plan.md),
[VB6 migration](design/vb6_migration_plan.md), [guitar measurement rig](design/guitar_measurement_rig.md),
[microcontroller diet](../embedded/mcu_diet_plan.md), [board consistency](../embedded/consistency_plan.md),
[Godot roadmap](../embed/godot/ROADMAP.md) and [tier 3](../embed/godot/TIER3.md),
[livecoder GUI](../godot/livecoder/GUI_PLAN.md).

Screenshots used by the READMEs live in [img/](img/).
