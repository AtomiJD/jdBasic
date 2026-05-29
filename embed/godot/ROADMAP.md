# jdBasic + Godot - Roadmap after Tier 4

**Status:** 2026-05-29 - Tier 0 through Tier 4 + E3/E5/E6 shipped on
`godot_spinoff` branch, all pushed to origin. The integration is now
feature-complete for daily use:

  * `.jdb` files attach to any Node as the actual `script` resource
  * Engine callbacks (`_ready`, `_process`, `_input`, ...) dispatch into
    jdBasic SUBs
  * `INSPECTOR DIM x = value` surfaces as Inspector properties, edits
    serialise into the .tscn override and round-trip back into the
    running VM
  * Hot-reload: edit the .jdb in any editor, save, FUNC bodies swap into
    the running VM with state preserved
  * Typed Variant marshalling - numeric arrays return as
    `PackedFloat64Array`, maps as `Dictionary`, etc.
  * `GODOT.*` native suite lets jdBasic call Godot APIs directly -
    `GODOT.SET(self, "rotation:y", angle)` works without any GDScript glue

The demo scenes prove every layer of the stack:

| Scene              | What it shows |
|---|---|
| `node_3d.tscn`     | Minimal Tier 3 - .jdb attached to Node3D, _ready prints, INSPECTOR DIM speed in Inspector |
| `cube_t4.tscn`     | Tier 4 pure-jdBasic spinning cube + hue cycle, no GDScript |
| `terrain.tscn`     | Tier 1 + E3/E6 - jdBasic vector ops generate a heightmap that GDScript meshes |
| `main.tscn` (gone) | Tier 1 REPL panel, removed during cleanup |

---

## Where the integration goes next

### Tier 5 - editor experience polish (1-2 weeks)

The script-language registration ships the bare minimum for the Inspector
+ run loop to work. To make daily editing feel like GDScript, the
following round out the editor side:

- **Inspector hint types** - parse `INSPECTOR DIM hp AS RANGE(0, 100)`,
  `INSPECTOR DIM source AS FILE("*.txt")` into PROPERTY_HINT values so
  the Inspector renders sliders / file pickers / colour pickers
- **Signal declarations** - `SIGNAL hit(damage)` at the top of a .jdb,
  surfaced through `_get_script_signal_list` so the Inspector's "Signals"
  tab shows them and `_connect` works
- **`@tool` mode** - drop the editor-hint VM-skip guard for scripts
  whose first non-comment line is `' @tool` (or a new `OPTION TOOL`),
  so editor-time logic (procedural mesh previews) becomes possible
- **Better validation** - `_validate` plugs into jdBasic's `JDB.CHECK$`
  for live syntax errors with line + column

### Tier 6 - editor tooling (1-2 weeks each)

- **Autocomplete** - `_complete_code` actually completes against the
  parsed symbol table (FUNC / SUB / DIM in scope, plus the `GODOT.*`
  builtin set)
- **Symbol lookup** - Ctrl-click jump to definition via `_lookup_code`,
  scanning the FUNC / SUB declarations
- **Inline diagnostics** - jdBasic parse errors surface as red squiggles
  in the Godot script editor

### Tier 7 - debugger (multi-week)

The `_debug_*` virtuals on `ScriptLanguageExtension` cover breakpoints,
stack inspection, expression evaluation. jdBasic already has a DAP
implementation (`src/dap.cpp`); wiring it through the GDExtension
debugger contract gets us:

- Breakpoints honoured during `_process` / `_ready`
- Step over / step into / step out
- Locals / globals / call stack in the Godot Debugger panel
- Watch expressions evaluating against the running VM

This is the big one - Lua / Python plugins for Godot typically punt on
it. Having a working debugger would be a meaningful differentiator.

### Production-grade follow-ups (each independent)

- **`YIELD` opcode** - cooperative coroutine support so a long-running
  jdBasic FUNC can park itself between frames instead of stalling the
  16 ms render budget
- **Threaded VM** - run jdBasic on a worker thread, marshal calls /
  results across; needed for CPU-heavy procedural / AI scripts
- **`res://` aware IMPORT** - jdBasic's IMPORT resolves through Godot's
  virtual filesystem instead of OS paths
- **More GODOT.* natives** -
  `GODOT.NEW("StandardMaterial3D")`, `GODOT.SIGNAL.CONNECT(obj, "pressed", callback)`,
  `GODOT.LOAD("res://x.png")`, `GODOT.INSTANTIATE("res://prefab.tscn")`,
  `GODOT.TIME.GET_TICKS_MSEC()` etc.
- **Distribution tiering** - `jdbrt-mini.dll` (interpreter only,
  ~1 MB), `jdbrt-llm.dll` (+ LLM bridge, ~150 MB + weights) so users
  pick what they need
- **macOS / Linux ports** of `build.bat` / `build_rt.bat` / `SConstruct`
  paths so the GDExtension builds cross-platform

### Showcase / outreach

- **Tweet-ready GIF** of `cube_t4.tscn` running and the .jdb source
  in a split editor view - "jdBasic, attached to a Godot Node as its
  actual script"
- **HN Show-HN** post once T5 polish + a real demo project lands
- **Devlog post** walking through the four tiers - from "C-ABI exists"
  to "language integration" to "engine bridge"
- **Conference talk** material: comparing the four embed strategies
  (Tier 1 library, Tier 2 host node, Tier 3 ScriptLanguage, Tier 4
  bidirectional bridge) - useful for anyone embedding any language

---

## Branch hygiene

`godot_spinoff` has accumulated ~50+ commits since cutting from main.
Once Atomi has confidence the integration is stable, options are:

1. **Merge into main as one squash** - clean history, single
   "godot integration" entry
2. **Merge with full history** - preserves the tier-by-tier story
   useful for future readers
3. **Keep `godot_spinoff` as a long-lived branch** - if Godot work
   continues to be a separate track from core jdBasic dev

The non-Godot-specific pieces (`jdb_embed_*` C-ABI in `jdbrt.dll`,
the `HEADLESS` build flag, the `VM::extra_no_vectorize` per-VM
override) are all useful for non-Godot embedders too - they could
land on main independently of the Godot folder.

---

## What "done" looks like before T5

The current branch state is already publishable. The minimum to declare
the "Godot integration" landmark complete:

- [x] Tier 1 / 2 / 3 / 4 all working with example scenes
- [x] Hot-reload preserves state across `.jdb` edits
- [x] Inspector properties round-trip through the `.tscn` override
- [x] HEADLESS jdbrt option keeps the embed DLL small and SDL-free
- [x] Typed Variant marshalling for arrays / maps / Vector / Color
- [ ] README + ROADMAP committed (this file)
- [ ] At least one short video / GIF for outreach
- [ ] A "first 10 minutes with jdBasic in Godot" tutorial

After those last three the branch is ready for a Show-HN-style reveal.

---

## Open questions for Atomi

1. **Merge strategy** - squash, full history, or keep the branch?
2. **Public reveal timing** - immediately after the README polish, or
   wait until T5 lands (autocomplete + signal declarations)?
3. **Tier 5 vs Tier 7 priority** - editor polish (T5) is incremental
   user-experience wins; debugger (T7) is the headline-grabbing
   "Lua-for-Godot never shipped this" feature
4. **Cross-platform** - is the macOS / Linux GDExtension port a
   blocker for public launch, or can the initial reveal be
   Windows-only?
