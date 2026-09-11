# jdBasic Sample Gallery

Curated collection of `.jdb` source files demonstrating the language. Everything here runs with the standard interpreter from the repo root:

```bash
./build/jdBasic.exe jdb/demos/games/space_shooter.jdb
./build/jdBasic.exe jdb/demos/ai/mini_llm.jdb
./build/jdBasic.exe jdb/demos/apl/life_demo.jdb
```

The `-c` flag compiles a script to a native EXE; the runtime `jdbrt.dll` is auto-copied next to it:

```bash
./build/jdBasic.exe -c jdb/demos/games/snake_game.jdb
./jdb/demos/games/snake_game.exe
```

---

## Layout

```
jdb/
├── demos/       polished showcase samples, by domain
│   ├── games/   TILT, PRISMA, chess engine, space shooter, snake, tetris, raytracer, ...
│   ├── graphics/ fractals, sine fields, N-body, plots, raytracer
│   ├── gl/      OpenGL P1-P4 (raw GL, shaders, VBO, textures)
│   ├── ai/      LLM, RAG, ONNX, classifier, GPT clients
│   ├── gui/     Dear ImGui apps + spreadsheet + sequencer studio
│   ├── forms/   native Win32 forms - control gallery, designer-built apps, MDI
│   ├── sound/   SOUND.* synth, sequencer parts, APL additive synth
│   ├── audio/   FX.* effect chains - FX rack, live guitar FX, tone designer
│   ├── apl/     vectorised idioms - Game of Life, primes, OUTER, one-liners
│   ├── data/    vectors / matrices / dates - AGG, TALLY, EOMONTH, DATERANGE, MVINS, ZIP archives
│   ├── jdlibs/  the module library by example - TESTKIT, CLI, SCHEMA, LLMAPI, CONF, LOGGER, XLSX, JWT
│   ├── tui/     terminal UI, FTXUI, markdown browser, cowsay
│   ├── web/     HTTP server + client - jdTrakr kanban, JDWEB framework, dashboards
│   ├── bridges/ FFI, COM (Excel/Word/Access/Outlook), SQLite, serial
│   ├── async/   ASYNC / AWAIT, threads, task queue
│   ├── turtle/  classic turtle graphics (dragon, Koch, fib, tree)
│   ├── sprites/ sprite engine + tilemap + Invaders variants
│   ├── workflow/ regex, eval, event bus, n8n-style runners
│   ├── tensor/  TF-style tensor + neural-net step-by-step series
│   ├── mcp_server/ an MCP server written in jdBasic itself
│   └── showcase/ scripted feature self-tests (harness, not eye candy)
├── tutorials/   bite-size language exercises (DIM, MAP, IF, lambda, TRY, ...)
├── tools/       small utilities you might use day-to-day
├── analytic/    end-to-end data-analysis pipelines (occupancy, ...)
├── bench/       benchmarks - APL vs loop, SAT, subset-sum, Mandelbrot, ONNX
├── emu/         6502 + Apple II emulator core, tests + bench
├── deusexmachina/ multi-file agent framework (event bus, dispatch, LLM brain, RAG)
├── jdtriage/    log-triage TUI (UTF-16 aware)
├── tv/          jdBasic-TV pipeline (lesson scripts + director)
├── parallax_game/ multi-file game project
├── udt_full_demo/ multi-file UDT demo (INIT/DISPOSE lifecycle)
├── art/         shared image / sprite assets used by the demos
└── _scratch/    development-time scratch - not curated, not for newcomers
```

---

## Modules - reusable libraries

Most `IMPORT`-able libraries here live **next to the scripts that use them**.
`IMPORT` looks in the importing script's own directory first, then in a
`modules/` subdirectory of it, then in the working directory. There is no
walk-up into parent directories, so a demo in `demos/sound/` picks up
`demos/sound/SQ.jdb` and can never accidentally reach into a sibling project.

A library meant for every project instead of one folder goes into the shared
library at the repo's `lib/`, and is installed by copying it to
`~/.jdbasic/lib` or to the `lib` folder beside `jdBasic.exe`. `JDBASIC_PATH`
points at a directory without copying anything, which is the convenient form
while developing. The full search order is under `IMPORT` in
[`doc/languages.md`](../doc/languages.md).

Where the main libraries live now:

| Module | Home | What it gives you |
|---|---|---|
| `testkit.jdb`               | `lib/` (repo root) | assertions, suites, TAP and JUnit output, non-zero exit on failure |
| `cli.jdb`                   | `lib/` (repo root) | command line parsing - flags, options, positionals, subcommands, generated help |
| `MATH.jdb` / `MLAB.jdb`      | `tutorials/`      | math constants + matrix / statistical / financial helpers |
| `sys_paths.jdb`             | `tutorials/`      | OS-agnostic path joining |
| `PLOTTER.jdb`               | `demos/graphics/` | 2D chart routine (`DATA_PLOTTER`) for the graphics demos |
| `text_viz.jdb`              | `demos/tui/`      | text-mode plotter for terminals |
| `SQ.jdb`                    | `demos/sound/`    | sequencer engine driving the `demos/sound/` series |
| `sprite_core.jdb`           | `demos/sprites/`  | sprite engine wrapper used by `demos/sprites/` |
| `sqlite.jdb`                | `demos/bridges/`  | DECLARE-FUNC wrapper around `sqlitebridge.dll` |
| `cpu6502.jdb` / `apple2.jdb`| `emu/`            | 6502 CPU core + Apple II platform skeleton |
| `claude_live.jdb`           | `tools/`          | MCP live-coding hooks + window positioning + Alt-press refocus |

---

## Try these first

A short curated list - the demos most likely to make a "wait, that's nice" impression.

### Games

* **`demos/games/tilt.jdb`** - *TILT / Schlagseite*: Tetris meets ship physics - stack cargo one-sided and the whole deck tips over. Run with a `shot` argument for an automatic screenshot.
* **`demos/games/prisma.jdb`** - *PRISMA - Color Alchemy*: match-3 where three primaries fuse into a secondary instead of vanishing.
* **`demos/games/space_shooter/space_shooter.jdb`** - *Stellar Drift*, 80s-style vector shooter. Also the canonical test bed for live-coding via MCP (`/jdvibe` skill).
* **`demos/games/snake_game.jdb`** - console snake with `ON "KEYDOWN"` (the POSIX `KEYDOWN` raw-mode bridge lives here).
* **`demos/games/chess_engine.jdb`** - a chess engine in one file.
* **`demos/games/raytracer.jdb`** - software raytracer rendered pixel by pixel.
* **`demos/games/hanoi.jdb`** - Towers of Hanoi, animating the minimal 2^n - 1 solution.

### Forms (Windows)

* **`demos/forms/gallery.jdb`** - every Win32 control on three tab pages.
* **`demos/forms/tasklist.jdb`** + **`tasklist.jdform`** - a small app whose layout comes from the VS Code visual form designer.
* **`demos/forms/mdi_demo.jdb`** - MDI frame with menus, toolbar and a grid, in ~40 lines.

### Graphics

* **`demos/graphics/mandel_vec.jdb`** - vectorised Mandelbrot (compare against `mandel_core.jdb` for the loop-form baseline).
* **`demos/graphics/sine_wave_3d_wire_rot.jdb`** - fully rotating wireframe sine surface (WASD + Z/X).
* **`demos/graphics/nbody_galaxy.jdb`** - n-body galaxy sim.
* **`demos/graphics/universe.jdb`** - bubble universe (APL form) vs. `universe_naive.jdb` (loop form) for benchmarking.

### OpenGL

* **`demos/gl/gl_p4_texcube.jdb`** - textured rotating cube. Walk back through `gl_p1..p3` for context (just clear → triangle → wireframe cube → texture).

### AI / LLM

* **`demos/ai/mini_llm.jdb`** - local LLM inference via llama.cpp.
* **`demos/ai/mini_onnx.jdb`** - ONNX inference as a generic compute backend (used by APL demos for Conv2D).
* **`demos/ai/rag_demo.jdb`** - RAG Studio: HNSW index + JSON mode + persistence.
* **`demos/ai/ai_chat_demo.jdb`** - full chat studio with history + GPU streaming.

### GUI

* **`demos/gui/gui_full.jdb`** - ImGui widget showcase.
* **`demos/gui/spreadsheet.jdb`** - mini calc (formula bar + grid).
* **`demos/gui/app_master.jdb`** - *JD-Basic Sequencer Studio*, ImGui front-end for the audio engine.
* **`demos/gui/piano_ui.jdb`** - on-screen piano driving `SOUND.*`.

### Sound

* **`demos/sound/sq_core.jdb`** - synth + sequencer foundations.
* **`demos/sound/sq_fluent.jdb`** - fluent-interface DSL showcase.
* **`demos/sound/synth_apl.jdb`** - APL-style additive synthesis + waveform visualisation.

### Audio FX (FX-flag builds)

* **`demos/audio/fx_rack.jdb`** - JSON-driven ImGui FX rack with oscilloscope, FFT spectrum, chromatic tuner, MIDI mapping and an AI tone designer.
* **`demos/audio/live_fx.jdb`** - live guitar/mic effect chain with record-while-monitoring.
* **`demos/audio/fx_chain_demo.jdb`** - build and render an effect chain in plain code.

### Web

* **`demos/web/jdtrakr.jdb`** - a complete kanban board (sessions, login, SQLite, templates) - the deployed reference app; see [`doc/WebDev.md`](../doc/WebDev.md) and `demos/web/deploy/DEPLOY.md` for putting it on a real server.
* **`demos/web/wm_dashboard.jdb`** - live sports dashboard pulling real data.
* **`demos/web/weather.jdb`** - tiny HTTP-client starter.
* **`demos/web/webhook_signing.jdb`** - `CODEC.HMAC$`: sign a payload the way GitHub and Stripe do, then walk a receiver through the four requests it has to turn away, with a timestamp against replay and the RFC 4231 vector to check the implementation against.

### MCP

* **`demos/mcp_server/server.jdb`** - an MCP server implemented in jdBasic itself (the C++ `--mcp` server's little sibling); see its `README.md`.

### APL idioms

* **`demos/apl/life_demo.jdb`** - Conway's Life with `AI.RUN` Conv2D as the neighbour count.
* **`demos/apl/prime_sieve.jdb`** - sieve via set membership.
* **`demos/apl/outer_prod.jdb`** - `OUTER` patterns.
* **`demos/apl/fib_reduce.jdb`** - Fibonacci via `REDUCE`.
* **`demos/apl/oneliners.jdb`** - one-line array art (sine wave, biorhythm, ASCII table, `|>` pipeline).
* **`demos/apl/array_idioms.jdb`** - loops turned into APL: rotate as `REVERSE(TRANSPOSE)`, neighbour counts via `CONVOLVE`, Tetris line-clear via `FILTER`.

### Data / vectors / matrices

* **`demos/data/agg.jdb`** - group + reduce with `AGG` (sum / mean / max / count per key).
* **`demos/data/tally.jdb`** - `TALLY` value-counts and descending sort via `GRADE`.
* **`demos/data/daterange.jdb`** - `DATERANGE` / `EOMONTH` date vectors and leap-safe days-in-month.
* **`demos/data/mvins.jdb`** - insert rows / columns into a matrix with `MVINS`.
* **`demos/data/zip_bundle.jdb`** - `ZIP.WRITE` / `ZIP.READ` / `ZIP.LIST`: pack a report from values in memory, check every entry with `CODEC.CRC32$`, keep a NUL inside a binary entry intact, then pack a directory off disk.

See [`doc/howto-vector-matrix-data.md`](../doc/howto-vector-matrix-data.md) for the full field guide.

### Testing (the module library)

* **`demos/jdlibs/testkit_demo.jdb`** - a slug builder, a thousands separator and a price parser, with the test file that holds them to it. Shows every assertion, and takes an argument: `tap` and `junit` switch the report format, `fail` injects one wrong expectation so the failure line and the exit code can be seen.
* **`demos/jdlibs/cli_demo.jdb`** - a tool called `packer` with two subcommands, built with `CLI`: flags, options with defaults, a repeatable option, a required positional, and the help text it generates for the tool and for each command. Run it with no arguments and it walks four command lines itself.
* **`demos/jdlibs/llm_facts.jdb`** - a structured answer from a language model, checked: `SCHEMA` declares the shape, `LLMAPI` asks OpenAI, Anthropic or a local GGUF model for exactly that shape, `SCHEMA` validates the reply, `REQ` carries the HTTP. `LLM_PROVIDER` picks the backend; without it the demo prints the JSON Schema and the messages it would send.
* **`demos/jdlibs/service_report.jdb`** - a service run end to end: `CONF` reads the TOML settings, `LOGGER` writes to the console and a rotating file, `XLSX` writes the sales report with a bold header, widths, number formats and a frozen pane, `JWT` signs the download link and verifies it, `CLI` takes a `--quiet` flag.
* **`demos/jdlibs/req_demo.jdb`** - an API client and the API it talks to in one process: a bearer token, a query string, JSON and form bodies, a cookie set by a login, a multipart upload, an endpoint that fails twice before it answers, and errors that raise. `REQ` on the client side, `HTTP.SERVER` on the other.
* **`demos/jdlibs/schema_demo.jdb`** - an order declared once with `SCHEMA` (nested customer, address and line items), then six payloads pushed through it: clean, with holes, wrong types, string-typed the way a form delivers them, with and without coercion, with an undeclared key in strict mode. Ends with the JSON Schema the declaration produces.
* **`demos/jdlibs/llm_tools_demo.jdb`** - a tool-calling loop with `LLMAPI`: two tools declared as jdBasic functions, the model asks for both, the loop runs them and hands the results back until the answer comes in words; then a structured answer read as a map. Offline by default against a stand-in served from the same process; `LLM_PROVIDER` switches to a real vendor.
* **`demos/jdlibs/logger_demo.jdb`** - one `LOGGER` with a coloured console, a text file that rotates at 600 bytes, a JSON lines file, and a fourth sink written by hand on `LOGGER.RECORD`; the JSON file is read back and counted per level.
* **`demos/jdlibs/conf_demo.jdb`** - settings from four places with `CONF`: shipped TOML defaults, an operator's INI, secrets in `.env`, the command line through `CLI`; each layer overrides the last, and the demo prints where every value came from.
* **`demos/jdlibs/jwt_demo.jdb`** - a login that hands out `JWT` tokens and an endpoint that verifies them, in one process; then what an attacker tries: an edited payload, an expired token, a token from another issuer, a guessed secret, `alg: none`.
* **`demos/jdlibs/xlsx_demo.jdb`** - an inventory workbook with `XLSX`: a formatted sheet with formulas and a frozen header, a sheet grouped with `AGG`, a facts sheet; read back and printed as a table, plus a CSV that makes the round trip to xlsx and back.

### TUI

* **`demos/tui/tui_demo.jdb`** - FTXUI showcase (menubar / tabs / modal / table / braille canvas / theme cycle).
* **`demos/tui/md_browser.jdb`** - terminal markdown browser.
* **`demos/tui/sys_monitor.jdb`** - "hacker screen" system monitor.

### Bridges (FFI / COM / SQL)

* **`demos/bridges/dll_demo.jdb`** - Win32 FFI: console transparency via `SetLayeredWindowAttributes`.
* **`demos/bridges/sqlite_demo.jdb`** - SQLite via `DECLARE FUNC`.
* **`demos/bridges/word_auto.jdb`** - Word automation, generate a `.docx`.
* **`demos/bridges/excel_com.jdb`** - Excel automation.

### Turtle

* **`demos/turtle/turtle_dragon.jdb`** - dragon curve.
* **`demos/turtle/turtle_koch.jdb`** - Koch snowflake.
* **`demos/turtle/turtle_tree.jdb`** - recursive tree.

### Workflow / Reactive

* **`demos/workflow/workflow_v3.jdb`** - n8n-style JSON workflow runner with triggers.
* **`demos/workflow/event_bus.jdb`** - custom event bus + `KEYDOWN` handlers.

### Tensor / Neural-net teaching series

* **`demos/tensor/nl_start.jdb`** ... `nl_part4.jdb` - build a neuron step by step, then the layer.

### Tools

* **`tools/winpos_probe.jdb`** - interactive window-position calibrator (used to set up the launch-video recording slot).
* **`tools/bundler.jdb`** - combine multiple `.jdb` files into one for distribution.

### Tutorials

`tutorials/` is the right place to send a beginner. Each file is ~30 lines and demonstrates exactly one feature: `if_blocks`, `loop_control`, `map_basics`, `str_format`, `try_catch`, `enum_types`, `lambda_capture`, `destructure`, ... 48 of them, naming should be self-explanatory.

---

## 6502 / Apple II emulator (`jdb/emu/`)

Standalone subdir with the emulator core + a graphical front-end and a fistful of self-tests:

* `emu_run.jdb` - graphical front-end (SCREEN window, runs the demo program from `boot_probe`).
* `boot_probe.jdb` - load Apple ROMs, reset, run a fixed cycle budget.
* `bench_cpu_speed.jdb` - tight-loop benchmark of the 6502 step rate.
* `test_*.jdb` - self-tests for opcodes, glyph cache, PC hooks, module-global persistence, etc.

The CPU + Apple II modules (`cpu6502.jdb` + `apple2.jdb`) live right here in `jdb/emu/` and are picked up by `IMPORT`'s in-directory resolution.

---

## What's in `_scratch/`?

Files that didn't make the curated cut - older experiments, single-purpose debugging snippets, work-in-progress that didn't pan out. They still parse and (mostly) still run; they're just not documented. Browse if you're curious, ignore for getting started.
