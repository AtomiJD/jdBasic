# jdBasic + Godot - Roadmap after Tier 4

**Status:** 2026-06-01 - Tier 0 through Tier 4 + E3/E5/E6 shipped on
`godot_spinoff`, all pushed to origin. On top of that a **runtime API
layer** landed (input, signals, timers, `_draw` text, full type
round-trip) that pulled several items forward out of the far-out
"Sankt Nimmerleinstag" community backlog - T9 (input) in full, T12
(signal event bridge) in its core. The integration is now
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

### Shipped after Tier 4

- **GODOT.INPUT.\*** - full polling + event-queue input suite. See `INPUT.md`.
- **GODOT.CONNECT / GODOT.DISCONNECT** (2026-06-01) - runtime, code-driven
  signal wiring straight into jdBasic SUBs. Signal args marshal like
  `GODOT.GET` returns (Objects arrive as bridge handles). Re-entrant
  dispatch is queued and drained after the outer callback so the
  single-threaded interpreter is never nested. Connections are owned by
  the script bridge and dropped on hot-reload / detach. See `SIGNALS.md`,
  `connect_demo.tscn`, `connect_smoke.tscn`. This is the *runtime* path and
  sidesteps the parked "Missing connected method" editor warning below,
  which only affects connections authored in the editor's Signal panel.
- **GODOT.TIMER** (2026-06-01) - `GODOT.TIMER(secs, "sub" [, repeat])`
  spawns a Timer child, wires its timeout to a SUB, and starts it. One-shots
  free themselves after firing. Built on CONNECT. See `SIGNALS.md`.
- **Self-validating handles** (2026-06-01) - the bridge handle table now
  stores `ObjectID` and resolves through `ObjectDB` on every `lookup`, so a
  handle to a freed object (one-shot timer, queue_freed node) returns null
  instead of dangling.
- **GODOT.DRAW_TEXT / DRAW_STRING / TEXT_SIZE** (2026-06-01..02) -
  `GODOT.DRAW_TEXT(node, pos, "text" [, font_size [, color]])` draws a
  string in a CanvasItem's `_draw` using the ThemeDB fallback font, so a
  pure-jdBasic HUD lives entirely in `_draw` with no Label node.
  `GODOT.DRAW_STRING(node, pos, "text" [, align [, width [, font_size [, color]]]])`
  is the 1:1 of `CanvasItem.draw_string`: `align` 0/1/2/3 (left/center/right/fill)
  inside a `width` box gives centred / right-aligned text.
  `GODOT.TEXT_SIZE("text" [, font_size])` -> `[w, h]` measures a string in
  the fallback font for manual layout. Demo: `draw_text_demo.tscn`; headless
  regression: `draw_text_smoke.tscn`, `drawstr_smoke.tscn`.
- **Mouse-click / `_input` fix** (2026-06-02) - two stacked bugs killed
  discrete `_input` handling (polling-based movement always worked):
  1. Godot only auto-enabled `_process` from a Tier-3 script's method list,
     never the input callbacks, so `_input` SUBs never fired. The script
     instance now mirrors its method set onto the Node's flags
     (`set_process_input` / `set_process_unhandled_input` /
     `set_process_shortcut_input` / `set_physics_process`) in ctor,
     hot-reload and hard-reload.
  2. `call_method` serialized engine-callback args with a static serializer
     that dropped Objects to `0`, so `_input(event)` arrived as `_input(0)`.
     It now marshals through the bridge (Object -> handle), the same path
     the signal dispatch uses. `store()` self-prunes dead entries during its
     scan so the per-event objects don't accumulate. Regression:
     `input_inject_smoke.tscn` (synthetic click, end to end).
- **GODOT.REF** (2026-06-02) - `GODOT.REF(handle)` wraps a bridge handle so
  it marshals back to the actual Object when passed as a property/method
  *value*: `GODOT.SET(sprite, "texture", GODOT.REF(tex))`. A bare handle is
  just an int. Uses the same `__gd`-tagged-map machinery as RECT2/VEC2I.
- **GODOT.NEW / GODOT.LOAD RefCounted retain** (2026-06-02) - a freshly
  instantiated RefCounted (InputEvent, Material, Resource) was freed the
  instant the native's local Ref dropped, leaving a dead handle. The bridge
  now retains these for its lifetime.
- **Dictionary <-> MAP + typed-value marshalling** (2026-06-01) - a Godot
  `Dictionary` now marshals to a jdBasic `MAP` and back (was stringified).
  Godot types that a plain numeric array can't disambiguate use a `__gd`
  tag inside a map: `Rect2` (vs a 4-float `Color`) and `Vector2i` (vs a
  float `Vector2`) round-trip via `{__gd: "Rect2", x, y, w, h}`. Builders:
  `GODOT.RECT2(x, y, w, h)`, `GODOT.VEC2I(x, y)`. Regression: `type_smoke.tscn`.
- **GODOT.AUDIO.\*** (2026-06-01) - `PLAY(path [, vol_db [, pitch]])`
  (fire-and-forget SFX, self-freeing), `MUSIC(path [, vol_db])` (looping
  music on one reusable player, swappable), `STOP_MUSIC()`, `STOP(handle)`.
  Players parent to the script's Node; music loops format-agnostically by
  replaying on `finished`. The natives use real player pointers internally,
  sidestepping the "object handle is just an int" gap. See `AUDIO.md`,
  `audio_smoke.tscn`. First building block of the game-complete native set.

### Still open

- Remaining un-marshalled Variant types: `Transform2D` / `Transform3D` /
  `Basis` / `Quaternion` / `Vector4` / `Plane`. Each could follow the same
  `__gd`-tagged-map pattern with a matching `GODOT.*` builder if a use case
  shows up. 2D node transforms are already reachable by setting
  `position` / `rotation` / `scale` separately, so this is low priority.

## Parked: Godot engine bugs (not ours to fix)

- **GDExtension language icons missing in Attach-Script dialog**
  [godot#98800](https://github.com/godotengine/godot/issues/98800).
  Godot's ScriptCreateDialog doesn't query GDExtension icon paths for
  the language dropdown. Our `[icons]` section in jdb_godot.gdextension
  and `JdbScriptResource::_get_class_icon_path()` both populate the
  Scene-tree + FileSystem dock icons correctly; the dropdown shows a
  generic broken-document until Godot patches the dialog upstream.

## Parked T5 issues (rediscover before public reveal)

Both of these are cosmetic; the underlying machinery works at runtime.
Park-and-revisit pattern - they're not blocking T6 / T7 but should be
fixed before the showcase post lands.

- **RANGE step doesn't snap in the Spinbox**. Tested with both
  `AS RANGE(0.0, 10.0, 0.1)` and `AS RANGE(0, 10, 0.1)`; the Resource
  side passes hint_string=`"0,10,0.1"` (whitespace stripped) and
  hint=`PROPERTY_HINT_RANGE` (=1). Diagnostic confirmed Godot reads
  those values from `_get_script_property_list`. Inspector renders a
  slider with min/max applied but the spinbox arrow steps in 1.0, the
  slider drag is continuous. Same data path GDScript `@export_range`
  uses works for GDScript, so the issue is likely in how the property
  is registered on the script-instance side or a missing flag in our
  PropertyInfo Dictionary. **Next attempts:**
  1. Try emitting numbers via `String::num(v, 7)` to get fixed-precision
     floats without trailing zeros - "0,10,0.1" exactly
  2. Check if `PROPERTY_USAGE_NIL_IS_VARIANT` or another usage flag
     matters
  3. Diff our PropertyInfo dict keys against `GDScript::get_script_property_list`'s
     output - maybe a key is missing

- **"Missing connected method 'X'" editor warning** for signal handlers
  defined as jdBasic SUBs. The signal connection works at runtime (the
  SUB fires when the signal emits, confirmed by `GODOT.PRINT` output),
  but Godot's editor still warns at scene-load. `JdbScriptResource::_has_method`
  AND `_get_script_method_list` are both implemented and return the
  scanned FUNC/SUB names; diagnostic confirmed neither was actually
  called by Godot. The verification path must go through a different
  route (possibly `Object::has_method` against `ClassDB` only, which
  never consults extensions). **Next attempts:**
  1. Override `_get_method_info(name)` (separate from list)
  2. Investigate whether `ScriptInstance::has_method` is the path Godot
     uses for connected-signal verification - if so, the editor-mode
     bouncer needs to mirror the runtime one
  3. Look at how Lua-for-Godot / Python-for-Godot plugins handle this
     same warning

### Tier 5 - editor experience polish (1-2 weeks)

The script-language registration ships the bare minimum for the Inspector
+ run loop to work. To make daily editing feel like GDScript, the
following round out the editor side:

- **Inspector hint types** - parse `INSPECTOR DIM hp AS RANGE(0, 100)`,
  `INSPECTOR DIM source AS FILE("*.txt")` into PROPERTY_HINT values so
  the Inspector renders sliders / file pickers / colour pickers
- **Signal declarations** - *runtime side shipped*: `GODOT.CONNECT` /
  `GODOT.DISCONNECT` / `GODOT.EMIT` wire and fire signals from code. What's
  left is the *editor* side: a `SIGNAL hit(damage)` declaration at the top
  of a .jdb surfaced through `_get_script_signal_list` so the Inspector's
  "Signals" tab lists them and editor-authored connections verify (this is
  also the fix for the parked "Missing connected method" warning)
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

### Tier 8 - shippable compiled scripts (1-2 weeks)

The hybrid library/script model. `jdbasic --compile foo.jdb -o foo.jdb.dll`
already produces an LLVM-native .dll with a C-ABI from the existing
`-c` pipeline; T8 wires that into the GDExtension so the same `.jdb`
either runs interpreted (dev / hot-reload) or routes through a native
sidecar .dll (ship). Scripts keep all their Tier-3 behaviour - Inspector
properties, signals, engine callbacks - the only thing that changes is
where the FUNC / SUB bodies live.

Components:

- **Build pipeline.** `embed/godot/compile_scripts.bat` (and the SCons /
  shell equivalents for cross-platform) walks `*.jdb` next to the .tscn
  files, calls `jdbasic --compile` for each, drops the resulting
  `foo.jdb.dll` next to the source. CI build = compile + zip. Dev =
  no DLL, interpreter handles everything.

- **Sidecar lookup.** `JdbScriptInstance::ctor` checks for
  `m_script->get_path() + ".dll"`. If present, `LoadLibrary` it,
  resolve each user FUNC / SUB via `GetProcAddress` with a known name
  mangling (e.g. `jdb_userfn_<name>`), populate a dispatch table.
  `call_method` becomes "if name in native dispatch table → call C
  function with marshalled args, else fall back to interpreter eval".
  Engine callbacks (`_ready` / `_process`) flow through the same path
  so they get the native speedup automatically.

- **Metadata sidecar.** INSPECTOR DIM defaults / hints, EXTENDS base
  type, SIGNAL declarations all live in source. Two options:
  1. Generate `foo.jdb.meta.json` at compile time and load it
     alongside the .dll. Compiler grows a `--emit-metadata` flag.
  2. Embed metadata as a resource section inside the .dll
     (`.rsrc` on Windows, `.note` on ELF, `__DATA,__jdbmeta` on
     Mach-O). Cleaner distribution, more platform-specific code.

  Recommendation: start with (1) - one read at script load, zero
  platform-specific code.

- **Lost: hot-reload.** Compiled scripts can't merge_funcs the way
  interpreted ones do. `_reload_tool_script` for a compiled script
  prints "compiled script - changes require recompile + Godot restart"
  and skips the fan-out. Dev workflow stays: keep the .jdb source +
  delete the .dll while iterating.

- **Lost: live-tweak via eval.** `vm.eval("rot_speed = 10")` from the
  Tier 4 Stellar-Drift live-coding loop only works against interpreted
  scripts. Compiled scripts get a stub that pushes
  "live-tweak unavailable in compiled mode".

- **Win: distribution.** Shipped `.dll` contains compiled jdBasic
  bytecode - source is not in the package. Performance is native
  (the existing `-c` benches show 4-10x speedup vs. interpreter for
  hot loops). The jdbrt.dll runtime is still needed at load time for
  the metadata + lifecycle bridges, but it's tiny (HEADLESS is 1.8MB).

Pre-T8 dependencies that are already shipped:
- `jdbasic --compile` works (Tier 3 RPG demo proved it).
- HEADLESS jdbrt is the shippable runtime.
- C-ABI for embedders (jdb_embed_*) is the contract everything plugs
  into.

Open question for the T8 spike: do we want a **single** "is compiled
mode" flag at the script level (whole script is native, or whole
script is interpreted), or a **per-FUNC** dispatch (some FUNCs in the
native dispatch table, the rest fall through to the interpreter)?
The per-FUNC version enables a "compile hot paths, leave cold ones
editable" workflow that nothing else offers. Worth the extra
complexity if Atomi sees Stellar-Drift-style live-coding as the
differentiated story.

### Tier 7 - debugger (IN PROGRESS, started 2026-06-02)

Breakpoints / step / locals / call stack / watch in the Godot Debugger
panel. Lua / Python plugins for Godot punt on this, so it's the headline
differentiator.

**This is bridge work, not a debugger from scratch.** The pieces already
exist on both sides:
- VM side: `DebugInfo` (pause / step_over / step_in / step_out, a
  breakpoints map, a per-line `debug_check()` hook) plus
  `debug_get_stack_frames()` / `debug_current_line()` / `debug_call_depth()`.
  Today only driven by the socket DAP (`src/dap.cpp`).
- Godot side: `EngineDebugger::script_debug(language, ...)` / `line_poll()`
  are reachable from a GDExtension (verified), and `ScriptLanguageExtension`
  declares 11 `_debug_*` virtuals.

What's missing is the bridge: the embed C-ABI exposes none of the VM debug
primitives, and `JdbScriptLanguage` overrides none of the `_debug_*`
virtuals.

Flow:
```
breakpoint hit in VM -> debug-hook callback (new ABI)
  -> JdbScriptLanguage fires EngineDebugger.script_debug(this)  [blocks + pumps]
  -> editor queries the _debug_* virtuals
  -> forwarded to the paused instance's VM (new ABI)
  -> continue / step  ->  VM resumes
```

Phases:
- **P1 - embed debug ABI** (started): `jdb_embed_debug_*` - set/clear
  breakpoint, register hook, stack count/line/function/source, locals /
  globals at a level, eval-in-frame, step / continue / pause. Mostly
  forwarding to the existing `DebugInfo`. Headless-testable without Godot.
- **P2 - break wiring** (DONE 2026-06-02): per-line breakpoint predicate
  added to the VM (`DebugInfo.line_break`, polled in `debug_check`) +
  `jdb_embed_debug_set_line_hook`. `JdbScriptInstance::on_debug_break` enters
  `EngineDebugger::script_debug(language)` and translates the returned
  `get_lines_left()`/`get_depth()` into continue / step-in / step-over.
  `is_break_line` polls `EngineDebugger::is_breakpoint(line, script_path)`.
  Hooks are wired only when a debug session is active (zero overhead in a
  shipped game).
- **P3 - the `_debug_*` virtuals** (DONE 2026-06-02) on `JdbScriptLanguage`,
  routing to the paused instance's VM (tracked via `set_break_instance`).
  Locals / globals returned in Godot's `{"<kind>": [names], "values": [...]}`
  shape; stack info + current-stack-info implemented.
- **P4 - breakpoint sync** (DONE 2026-06-02): editor gutter breakpoints are
  polled per line via `is_breakpoint`, so no separate sync step was needed.
  `.jdb` line maps 1:1 to the VM line (the .jdb is the source).
- **P5 - polish** (DONE 2026-06-02): per-level locals
  (`debug_get_locals_at(level)`, so selecting any stack frame shows its own
  locals), the function-name prefix stripped from local names (`ADD_THEM_P`
  -> `p`) in the panel, and watch-expression eval wired
  (`_debug_parse_stack_level_expression` -> `jdb_embed_debug_eval`, global
  scope, guarded by a `suppress` flag so the eval can't re-break).

**Verified in the editor 2026-06-02**: breakpoints set/clear, break, step
over/into, continue, the call stack, per-level locals (function prefix
stripped) and globals all work against `breakout.jdb`. Standalone/VS-Code
DAP path unchanged; pre-commit gate green.

Notes / future:
- Godot's editor surfaces runtime values only in the Debugger dock's
  variable tree - there is no code-hover value tooltip for any language, so
  the watch-eval virtual has no built-in editor trigger today (it's ready if
  a flow or a custom dock calls it).
- Values cross as strings, so the tree can't expand arrays / maps into
  sub-trees. Returning real Variants (so the tree expands) is the next
  optional step if richer inspection is wanted.

Key decisions:
- Per-instance VM vs. global language: the language tracks the active
  `JdbScriptInstance` (set around the `call_method` eval) so a break knows
  which VM to query.
- Pausing blocks the game's main thread while the editor inspects (correct
  debugger behaviour). Works because the running game is a separate process
  from the editor.

### Production-grade follow-ups (each independent)

- **`YIELD` opcode** - cooperative coroutine support so a long-running
  jdBasic FUNC can park itself between frames instead of stalling the
  16 ms render budget
- **Threaded VM** - run jdBasic on a worker thread, marshal calls /
  results across; needed for CPU-heavy procedural / AI scripts
- **`res://` aware IMPORT** - jdBasic's IMPORT resolves through Godot's
  virtual filesystem instead of OS paths
- ~~**More GODOT.* natives**~~ - *shipped*: `GODOT.NEW`, `GODOT.CONNECT`,
  `GODOT.LOAD`, `GODOT.INSTANTIATE`, `GODOT.TIME_MS` / `GODOT.TIME_SEC`,
  plus `GODOT.TIMER` / `GODOT.DRAW_TEXT` / `GODOT.RECT2` / `GODOT.VEC2I`.
- **Distribution tiering** - `jdbrt-mini.dll` (interpreter only,
  ~1 MB), `jdbrt-llm.dll` (+ LLM bridge, ~150 MB + weights) so users
  pick what they need
- **macOS / Linux ports** of `build.bat` / `build_rt.bat` / `SConstruct`
  paths so the GDExtension builds cross-platform

### Former community backlog - pulled forward

These four surfaced while building the weekend rpg-demo as Godot APIs with
no jdBasic surface, and were originally parked as "only if a community
shows up". Building the input demo + the RPG made them worth doing now, so
two shipped ahead of schedule:

- ~~**T9 - `GODOT.INPUT.*` natives**~~ - **shipped** (2026-05-31). Full
  polling suite (`is_action_pressed`, `get_axis`, `get_vector`,
  `is_key_pressed`, mouse position/velocity/buttons) plus an event queue
  and `_input(event)` introspection. See `INPUT.md`.
- **T10 - `GODOT.MESH.*` natives** - still open.
  `ArrayMesh.add_surface_from_arrays` binding + `HeightMapShape3D.map_data`
  setter. The math-heavy compute (heightmaps, voxel fields) is already a
  jdBasic strength; this would let jdBasic drive the mesh-build side too,
  without marshalling raw arrays back to GDScript.
- **T11 - Static engine singletons via `GODOT.CALL`** - still open. `Time.*`,
  `ProjectSettings.*`, `Engine.*`, `OS.*` - `GODOT.CALL` needs an object
  handle, so static-class methods are unreachable. Extend with a
  string-keyed singleton table (e.g. `GODOT.SINGLETON("OS")` -> handle).
- **T12 - `GODOT.AWAIT$` for engine signals** - **core shipped** via
  `GODOT.CONNECT` (2026-06-01): the event-loop bridge that pumps Godot
  signals into the VM (with re-entrancy-safe deferred dispatch) is exactly
  what `await node.body_entered` needed, just expressed as a connected SUB
  instead of an inline `await`. A literal `AWAIT$` expression that parks a
  FUNC mid-line is the only remaining piece, and depends on the `YIELD`
  opcode below.

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
