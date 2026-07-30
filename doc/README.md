# jdBasic documentation index

Start here if you landed in `doc/` directly. The repo-level [README](../README.md) has the project overview, quickstart and sample gallery.

## User documentation

| Doc | What it covers |
|-----|----------------|
| [languages.md](languages.md) | **The language reference** - every statement, function and build-flag-gated API. Also read at runtime by the MCP `jdb_doc` tool, so its one-bullet-per-function format is load-bearing. |
| [BUILD.md](BUILD.md) | Building from source: prerequisites, third-party libs, feature flags, Windows/Linux/macOS, packaging. |
| [MCP.md](MCP.md) | The built-in MCP server: transports, client configs (Claude Code, Cursor, Cline, Zed, ...), the 14 tools, live pair-coding on a running program. |
| [WebDev.md](WebDev.md) | Web apps in jdBasic: `HTTP.SERVER`, the TMPL template engine, the JDWEB framework (sessions, login, themes), SQLite persistence, deployment. |
| [SequencerHelp.md](SequencerHelp.md) | The `SOUND.*` live-coding sequencer and synth: tracks, voices, ADSR, effects, pattern DSL, sidechain, scales. |
| [AudioFX.md](AudioFX.md) | The `FX.*` effect-chain engine: building blocks, chain API, FFT analysis, a cookbook of named guitar/synth tones. |
| [HowTo-FX.md](HowTo-FX.md) | The FX/pedalboard walkthrough: offline render, live guitar chain, the ImGui pedalboard with presets, tuner and MIDI. |
| [APL_pipeline.md](APL_pipeline.md) | Tutorial: from tight FOR loops to whole-array update steps - and when the APL form loses. |
| [howto-vector-matrix-data.md](howto-vector-matrix-data.md) | Data-wrangling cookbook: build, transform, group, sort, reshape, dates, rendering. |
| [idioms-from-python.md](idioms-from-python.md) | Python-to-jdBasic cheat sheet with hard-won gotchas. Written with AI coding agents in mind - great context to feed your assistant. |

## Contributor documentation

| Doc | What it covers |
|-----|----------------|
| [CODING_STYLE.md](CODING_STYLE.md) | Conventions for the C++ core and the keyword-registration checklist. |
| [../tests/README.md](../tests/README.md) | The test bank and the pre-commit gate (suites, naming, GUI smoke procedure). |

`agent_coprocessor_plan.md` is an internal design note, not user documentation. Screenshots used by the READMEs live in `img/`.
