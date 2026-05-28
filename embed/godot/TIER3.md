# Tier 3 - jdBasic as a peer ScriptLanguage in Godot

**Branch:** `godot_spinoff`
**Started:** 2026-05-28
**Status:** design locked 2026-05-28, pre-T3.0

## Locked decisions

| # | Decision           | Choice                                           |
|---|---|---|
| 1 | Callback naming    | `_process`, `_ready`, `_input` (Godot 1:1)       |
| 2 | Base class         | new keyword `EXTENDS Node3D` at top of file      |
| 3 | Inspector exports  | new keyword `INSPECTOR DIM speed = 1.0`          |
| 4 | Editor scope (E1)  | T3b - includes syntax highlighting in Godot editor |
| 5 | Hot reload         | recompile_source on soft reload, full re-eval on hard |
| 6 | Naming             | C++ class `JdbScript`, label "jdBasic", ext `.jdb` |

Decisions 2 and 3 introduce two new jdBasic-core keywords. See the
"Implementation strategy for EXTENDS / INSPECTOR" section below.

Decision 1 means Tier 2's `JDBScript` Node convention (`on_process`)
becomes inconsistent. Plan: deprecate `on_*` naming in Tier 2,
migrate `JDBScript` to also accept `_process` (or remove the auto-dispatch
once Tier 3 ships).

Goal: a Godot user clicks **Attach Script** on any Node, picks **jdBasic**
from the language dropdown next to GDScript / C#, the `.jdb` file is
attached to the Node and Godot dispatches `_ready` / `_process` /
`_input` / etc. into the embedded jdBasic VM. Inspector exposes jdBasic
globals as editable properties. Saving the `.jdb` while running hot-reloads.

This is the big one. The existing Tier 2 `JDBScript : Node` is a
companion-node pattern; Tier 3 is **language replacement** at the
Godot scripting API level.

---

## The three classes we have to implement

Godot's GDExtension scripting API has three layers. We subclass each:

| Layer                          | Role                                  | godot-cpp virtuals | Critical-path subset |
|---|---|---|---|
| `ScriptLanguageExtension`      | the language itself (1 instance)      | 60                 | ~8                   |
| `ScriptExtension`              | one script file as a Resource         | 37                 | ~10                  |
| `GDExtensionScriptInstanceInfo3` | per-Node attached-script instance   | 27 function ptrs   | ~10                  |

The first two are normal C++ subclasses bound via godot-cpp's macros.
The third is a C-style function-pointer table - we fill it once, return
pointers into our C++ class from each callback. Same pattern any other
language extension uses.

Source headers we're going to read against:

- `embed/godot/godot-cpp/gen/include/godot_cpp/classes/script_language_extension.hpp`
- `embed/godot/godot-cpp/gen/include/godot_cpp/classes/script_extension.hpp`
- `embed/godot/godot-cpp/gen/include/gdextension_interface.h` (lines 661-689 for `GDExtensionScriptInstanceInfo3`)

---

## Minimum viable Tier 3 - what counts as "shipped"

A scene with a single Node3D. Atomi clicks **Attach Script**, picks
**jdBasic**, a `node.jdb` is created with template content:

```basic
' extends Node3D

EXPORT DIM speed = 1.0

SUB on_ready()
    PRINT "node.jdb ready"
ENDSUB

SUB on_process(delta)
    angle = angle + speed * delta
ENDSUB
```

He saves, runs, the cube spins. He selects the Node, the Inspector
shows `speed` as a 1.0 number-field. He changes it to 5.0, the cube
speeds up. He hits Ctrl+S in his editor with new code, Godot reloads,
the new behaviour kicks in without dropping `angle`'s value.

That's the bar. No syntax highlighting yet, no autocomplete, no debugger.

---

## Phase plan

| Phase    | Deliverable                                                      | Est.   |
|---|---|---|
| **T3.0** | JdbScriptLanguage + JdbScript skeleton classes register, "jdBasic" shows in Attach-Script menu | 0.5 day |
| **T3.1** | `.jdb` resource round-trip + Path-B preprocessing (parse EXTENDS / INSPECTOR, rewrite source) | 1 day |
| **T3.2** | Engine callback dispatch via GDExtensionScriptInstanceInfo3 (`_ready`, `_process`) | 1-2 days |
| **T3.3** | `INSPECTOR DIM` globals visible + editable in Inspector          | 1-2 days |
| **T3.4** | Hot-reload via `_reload` -> `jdb_embed_recompile_source`         | 0.5 day |
| **T3.5** | Reserved words, comment delimiters, basic editor template (T3b) | 1 day  |
| **T3.6** | (optional) Autocomplete + symbol lookup                          | 1 week |
| **T3.7** | (optional) Live debugger - breakpoints, stack inspection         | 1 week |

Realistic schedule for **T3.0 through T3.5** (locked scope): **5-7
focused workdays**.

T3.7 and T3.8 are stretch goals. The original `plan_godot_embedding.md`
called this whole thing "2-3 weeks of work to make embedding production-grade".

---

## Open design decisions (need Atomi's call)

### 1. Engine callback naming convention

Godot calls `_process(delta)` / `_ready()` / `_input(event)` / `_physics_process(delta)`.
jdBasic identifiers traditionally don't start with underscore (need to verify
the parser accepts it). Options:

| # | Mapping              | Pro                                | Con                                       |
|---|---|---|---|
| a | Exact: `_process`    | matches Godot's docs 1:1            | needs jdBasic parser to accept `_xxx`     |
| b | Prefix swap: `_process` -> `on_process` | matches our Tier 2 `JDBScript` convention | extra translation table; we have to document it everywhere |
| c | Strip prefix: `_process` -> `process` | clean jdBasic identifier        | collides if user defines a `process()` for unrelated reasons |

**Default recommendation: (b)** - keeps the convention we already shipped
in Tier 2 (`on_process`, `on_ready`, `on_exit`).

(Pre-check 2026-05-28: jdBasic's parser accepts `_process` style names,
so option (a) is technically viable. The tradeoff is consistency: someone
who learns `on_process` in a Tier-2 JDBScript will be confused if Tier 3
wants `_process`. Pick one and apply it everywhere.)

### 2. Base-class declaration

Godot needs to know `extends Node3D` so the script can use Node3D-specific
calls. Options:

| # | Form                                | Pro                                 | Con                                     |
|---|---|---|---|
| a | First-line comment `' extends Node3D` | zero jdBasic syntax pollution; parser already ignores it | brittle (one typo = wrong base) |
| b | New keyword `EXTENDS Node3D` at top | explicit, parseable                 | new keyword in the language             |
| c | Always extend Object                | no parsing                          | scripts can't call Node3D-specific methods directly |

**Default recommendation: (a)** - comment-based, parsed by the
ScriptLanguageExtension itself, doesn't touch the jdBasic core lexer.

### 3. Inspector-exported properties

GDScript: `@export var x: float = 0.0`. We need an equivalent in jdBasic.

| # | Form                          | Pro                                | Con                                     |
|---|---|---|---|
| a | Reuse `EXPORT DIM x = 0.0`    | keyword already exists in jdBasic  | module-EXPORT and Inspector-EXPORT have different semantics |
| b | New `INSPECTOR DIM x = 0.0`   | dedicated keyword, no overload     | adds another reserved word              |
| c | Pragma comment `' @export\nDIM x = 0.0` | no lang change at all      | unusual look; needs source-text parsing |

**Default recommendation: (a)** - EXPORT is already a jdBasic
language-level signal that "this is part of the public surface".
For a script attached to a Node, "public" naturally means "Inspector-visible".
Module-export and script-export don't collide because scripts aren't modules.

### 4. Editor integration scope (first ship)

| Tier  | Includes                                          | Effort |
|---|---|---|
| T3a   | Language dropdown + attach + run + Inspector + hot-reload | 5-7d   |
| T3b   | + syntax highlighting (import existing tmLanguage)| +1d    |
| T3c   | + autocomplete + symbol lookup                    | +1w    |
| T3d   | + breakpoints + step debugger                     | +1-2w  |

**Default recommendation: ship T3a, then evaluate.** A working "attach
a .jdb and it runs" tweet is more valuable than a polished editor
experience nobody has used yet.

### 5. Hot-reload semantics

When the user saves a `.jdb` and Godot calls `Script._reload(keep_state)`:

| keep_state | Behaviour we'd implement                              |
|---|---|
| true       | `jdb_embed_recompile_source` (FUNC bodies swap, globals stay) - the Tier 2 Recompile button pattern |
| false      | Drop the VM, create a fresh one, full eval of the source |

**Default recommendation:** implement both, switch on the flag. Soft
reload is the live-coding magic moment; hard reload is the safety net.

### 6. Naming + branding

| Element                    | Proposed       |
|---|---|
| GDExtension C++ class      | `JdbScript`    |
| Language label in dropdown | "jdBasic"      |
| File extension             | `.jdb`         |
| Highlight token name       | "jdb"          |

Counter-proposals welcome - but this is the smallest set of names
that's unambiguous and aligned with how Atomi writes the language name elsewhere.

---

## Risks + known gotchas

- **jdBasic identifier rules**: need to verify the lexer accepts
  identifiers starting with `_` (for option 1a) or that we're firmly on
  option 1b. Quick `jdb_eval` test will settle this.
- **Thread model**: Godot calls `_call` on the script's owning thread.
  For nodes in the scene tree that's the main thread. jdBasic's
  `jdb_embed_eval` is synchronous on the calling thread. A long-running
  FUNC blocks Godot's frame. Same constraint as Tier 2; not a Tier 3
  regression, but the YIELD opcode from the original plan would matter
  more here because users will write more code per script.
- **GDExtensionScriptInstanceInfo3 is C-only**: we have to write a
  `extern "C"` wrapper layer that hands C function pointers to Godot
  and calls into our C++ class. No `std::function` shortcuts. Roughly
  10 short static functions, each a single-line bounce into the class.
- **Reload during _process**: if Godot reloads the script while
  `on_process` is mid-eval, what happens? Our embed API is synchronous,
  so this can't physically race. But if we ever lift the worker-thread
  pattern from `mcp_stdio.cpp`, this becomes a real concern.
- **Editor-mode VM**: `JDBScript` (Tier 2) skipped VM init in the editor
  via `Engine::is_editor_hint()`. For Tier 3 the editor needs to
  introspect the script (method list, property list) without running it.
  Plan: parse-only mode that builds the symbol table but doesn't execute.

---

## Implementation strategy for EXTENDS / INSPECTOR

Both are net-new jdBasic syntax. Two viable paths:

### Path A - real jdBasic core keywords

Add to `src/lexer.cpp` + `src/parser.cpp` proper. The interpreter and
the native `-c` compiler both learn them; AST gets new node types. The
embedder asks the VM for `vm.get_extends_target()` / `vm.list_inspector_vars()`.

- Pro: clean, one source of truth, `jdbasic.exe foo.jdb` works on a
  Tier-3-authored script even outside Godot
- Pro: native-`-c` compiled jdBasic-script EXEs can also expose
  Inspector metadata (future MCP/IDE tooling)
- Con: ~4-8 hours of careful core work (lexer rule + parser rule +
  AST + compiler/interp ignore-on-emit + introspection API)

### Path B - pre-process in the GDExtension

Our `JdbScriptLanguage::_set_source_code` parses EXTENDS / INSPECTOR
itself, captures the metadata, then rewrites the source before handing
to jdBasic:

- `EXTENDS Node3D` -> `' EXTENDS Node3D` (commented out)
- `INSPECTOR DIM x = 5.0` -> `DIM x = 5.0` (modifier stripped)

The rewritten source goes to `jdb_embed_eval`. Metadata stays in C++
side.

- Pro: zero jdBasic-core changes, Tier 3 stays contained in `embed/godot/`
- Pro: faster to ship (a couple hours of regex work)
- Con: `jdbasic.exe foo.jdb` outside Godot errors on `EXTENDS Node3D` /
  `INSPECTOR DIM x = 5.0` - the user has to either run via Godot or
  manually strip those lines
- Con: error messages reference the rewritten source, not what the user typed

**Locked 2026-05-28: Path B** - Atomi picked the GDExtension-preprocessing
route. Trade-off accepted: `jdbasic.exe foo.jdb` won't run a Tier-3-authored
script outside Godot (would error on `EXTENDS Node3D` line). Sentinel-tagged
metadata stays in the GDExtension's C++ layer; jdBasic core never sees
those tokens. If we later want core-keyword support, Path A is additive
to the existing surface, not a rewrite.

---

## What we will NOT do in Tier 3

- C# / GDScript-style strong typing in jdBasic. Type hints stay loose.
- Auto-generating GDExtension `Variant` bindings for jdBasic-defined types.
- Performance work. If the per-frame eval is too slow we revisit in a
  separate phase (likely with `YIELD` + the value-handle accessor from
  E3 of the original plan).
- Console / .NET / mobile-export coverage. Desktop Win/Lin/Mac only.

---

## Pre-implementation checklist

- [x] **jdBasic accepts `_process` style identifiers** - verified via
      `SUB _process(delta) ... ENDSUB`, parses OK. Option 1a (exact-match)
      is technically viable.
- [x] **Comment `' extends Foo` is a no-op for the parser** - verified.
- [x] **`EXPORT DIM x = N` parses outside MODULE** - verified.
- [ ] Atomi locks in the 6 design decisions above.

After that's locked in, T3.0 skeleton can start.
