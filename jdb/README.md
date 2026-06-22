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
│   ├── games/   space shooter, snake, tetris, chess, mines, raytracer, ...
│   ├── graphics/ fractals, sine fields, N-body, plots, raytracer
│   ├── gl/      OpenGL P1-P4 (raw GL, shaders, VBO, textures)
│   ├── ai/      LLM, RAG, ONNX, classifier, GPT clients
│   ├── gui/     Dear ImGui apps + spreadsheet + sequencer studio
│   ├── sound/   SOUND.* synth, sequencer parts, APL additive synth
│   ├── apl/     vectorised idioms - Game of Life, primes, OUTER, one-liners
│   ├── data/    vectors / matrices / dates - AGG, TALLY, EOMONTH, DATERANGE, MVINS
│   ├── tui/     terminal UI, FTXUI, markdown browser, cowsay
│   ├── web/     HTTP client + server + weather/ticker/wflib
│   ├── bridges/ FFI, COM (Excel/Word/Access/Outlook), SQLite, serial
│   ├── async/   ASYNC / AWAIT, threads, task queue
│   ├── turtle/  classic turtle graphics (dragon, Koch, fib, tree)
│   ├── sprites/ sprite engine + tilemap + Invaders variants
│   ├── workflow/ regex, eval, event bus, n8n-style runners
│   └── tensor/  TF-style tensor + neural-net step-by-step series
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
├── doom/        DOOM port (frozen, see release/ notes)
├── art/         shared image / sprite assets used by the demos
└── _scratch/    development-time scratch - not curated, not for newcomers
```

---

## Modules - reusable libraries

There is no central `modules/` folder: each `IMPORT`-able library lives **next to
the scripts that use it**. jdBasic's `IMPORT` resolves a module from the importing
script's own directory first (then walks up), so co-locating a library with its
consumers just works - a demo in `demos/sound/` can `IMPORT SQ` and pick up
`demos/sound/SQ.jdb` transparently.

Where the main libraries live now:

| Module | Home | What it gives you |
|---|---|---|
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

* **`demos/games/space_shooter/space_shooter.jdb`** - *Stellar Drift*, 80s-style vector shooter. Also the canonical test bed for live-coding via MCP (`/jdvibe` skill).
* **`demos/games/snake_game.jdb`** - console snake with `ON "KEYDOWN"` (the POSIX `KEYDOWN` raw-mode bridge lives here).
* **`demos/games/chess_engine.jdb`** - a chess engine in one file.
* **`demos/games/raytracer.jdb`** - software raytracer rendered pixel by pixel.

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

See [`doc/howto-vector-matrix-data.md`](../doc/howto-vector-matrix-data.md) for the full field guide.

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
* **`demos/tensor/tensor_train.jdb`** - scalar-mode training loop.

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
