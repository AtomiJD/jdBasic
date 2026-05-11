# TUI.* — Implementation Plan

**Status:** design proposal · drafted 2026-05-11
**Sister doc:** [`ftxui_plan.md`](./ftxui_plan.md), [`ftxui_api.md`](./ftxui_api.md), [`ftxui_testing.md`](./ftxui_testing.md)

## Budget

Realistic time-to-ship: **8–12 hours of focused work** spread across the phases below. Assumes the FTXUI build pipeline is already in place (it is — that landed in commit `b28e3b2`).

Worst-case ceiling: ~16 hours if the immediate-mode-on-retained-FTXUI adapter has edge cases we can't see from the design doc.

## Files to add

```
src/tui.h            — public API (extern C-ish wrappers VM calls)
src/tui.cpp          — registration of all TUI.* natives + adapter logic
src/tui_state.h      — TuiState struct (component tree builder,
                        modal stack, event cache, theme handle)
src/tui_state.cpp    — TuiState implementation
jdb/tui_demo.jdb     — the showcase (see ftxui_test_cases.md)
tests/test_tui_smoke.jdb — headless smoke for the pre-commit gate
```

Total new C++ surface: ~1800–2200 LoC. The bulk is one-line-per-native registration boilerplate.

## Files to touch

```
src/main.cpp         — call register_tui_natives(vm) from setup_console_builtins
src/repl_ftxui.cpp   — set a global hook so TUI.BEGIN inside the REPL
                        knows to WithRestoredIO
build.bat            — add src\tui.cpp src\tui_state.cpp to EXTRA_SRC
                        under the FTXUI flag
doc/help.txt         — append TUI.* entries (mirrors GUI.* section)
doc/languages.md     — TUI.* reference, copy from ftxui_api.md
release/ftxui_refactor_plan.md — link this doc set from Phase 5+
```

## Phase breakdown

### Phase A — Scaffolding (2 h)

* `TuiState` struct holds:
  * Active component tree as a `std::vector<ftxui::Element>` stack
    (each layout-begin pushes a child collector, layout-end pops and
    appends to the parent)
  * A modal stack (queued + active modal ids)
  * Event cache: last key string, mouse x/y/button mask, wheel delta
  * Theme handle (reuses `jdb_theme` from `src/ftxui_theme.h`)
  * `screen_owned`: a `ScreenInteractive` we create on `TUI.BEGIN` if
    the script isn't already inside the REPL
  * `repl_screen_ptr`: optional pointer set by `repl_ftxui.cpp` so we
    know to use `WithRestoredIO` instead of constructing our own screen

* Hook in `repl_ftxui.cpp` exporting `tui_set_host_screen()` /
  `tui_clear_host_screen()` so the REPL can install/uninstall the
  pointer around its lifetime.

* `register_tui_natives(vm)` skeleton: registers all 63 names with
  stubs that just print "TUI.X not yet implemented" — gets the
  build green before we wire any logic.

### Phase B — Core loop (1.5 h)

Concrete first six natives:

* `TUI.BEGIN` — pushes the root vbox to the state stack. If
  `repl_screen_ptr` is set, ask it to `WithRestoredIO`; otherwise
  build our own `ScreenInteractive::Fullscreen()`.
* `TUI.END` — pops the root vbox. Doesn't render yet — that's
  `TUI.RENDER`.
* `TUI.RENDER` — wraps the built tree in a one-shot Renderer +
  CatchEvent that fills the event cache, runs one screen loop tick
  (we drive the loop manually so the script controls frame rate).
* `TUI.WAIT_EVENT` — blocks via FTXUI's task receiver until an event
  arrives, then drains.
* `TUI.QUIT` / `TUI.EXIT` — read/write the quit flag (mirrors
  `running=0`).

At end of Phase B: a script that calls `TUI.BEGIN "hi"`, `TUI.TEXT
"world"`, `TUI.END`, `TUI.RENDER` shows a "world" line until the
user hits Ctrl+Q.

### Phase C — Layout primitives (1 h)

`TUI.HBOX_BEGIN/END`, `TUI.VBOX_BEGIN/END`, `TUI.GRID_BEGIN/END`,
`TUI.BORDER_BEGIN/END`, `TUI.SEPARATOR`, `TUI.SEPARATOR_TEXT`,
`TUI.SPACER`, `TUI.SIZE`, `TUI.SAME_LINE`.

The state stack treats begin/end pairs like XML — anything emitted
between them goes into a child collector. End pops the collector
and produces one `Element` for the parent.

### Phase D — Input widgets (2 h)

`TUI.BUTTON`, `TUI.CHECKBOX`, `TUI.RADIO`, `TUI.INPUT`,
`TUI.INPUT_INT`, `TUI.INPUT_DOUBLE`, `TUI.SLIDER`, `TUI.MENU`,
`TUI.DROPDOWN`, `TUI.SELECTABLE`.

These return interaction state. Trickiest part: FTXUI components
own state in `Component` objects, but we recreate the tree every
frame. We sidestep by keeping a per-widget-id state map keyed on
the call-site (label + nesting hash) — same trick ImGui uses
internally for `ImGuiID`.

For inputs that write back to a jdBasic variable, we use the
existing VM `byref` plumbing (same as `GUI.SLIDER` does).

### Phase E — Display + tables (1.5 h)

`TUI.PROGRESS`, `TUI.GAUGE`, `TUI.SPINNER`, `TUI.CANVAS_*`,
`TUI.LINE`, `TUI.PIXEL`, `TUI.TABLE_BEGIN/ROW/END`,
`TUI.PARAGRAPH`, `TUI.HEADING`, `TUI.LINK`.

`TUI.CANVAS` wraps FTXUI's braille `Canvas` so scripts can draw
shapes (heartbeat charts, sparklines, mini-pixel-art) into 2×4
sub-cell precision per terminal cell.

### Phase F — Modal + menu bar + tabs (1 h)

`TUI.MODAL_OPEN/BEGIN/END/CLOSE`, `TUI.MENUBAR_BEGIN/END`,
`TUI.SUBMENU_*`, `TUI.MENUITEM`, `TUI.TAB_BAR_*`, `TUI.TAB_*`.

The modal stack lives in `TuiState`. Each frame, after the main
tree, we wrap any active modal id in a `Modal()` component and
overlay.

### Phase G — Colour + theme + events + diagnostics (1 h)

`TUI.COLOR`, `TUI.BG_COLOR`, `TUI.POP_*`, `TUI.STYLE_PUSH/POP`,
`TUI.THEME`, `TUI.KEY$`, `TUI.MOUSE_*`, `TUI.ON`, `TUI.WIDTH`,
`TUI.HEIGHT`, `TUI.LAST_RENDER_MS`, `TUI.VERSION$`.

`TUI.THEME` rewrites `jdb_theme::*` slots — the REPL picks them up
on its next render too, so a script that sets `TUI.THEME "warm"`
permanently retints the FTXUI REPL session.

### Phase H — Polish + tests (1 h)

* Append every TUI.* to `doc/help.txt`.
* `tests/test_tui_smoke.jdb` — a no-render assertion run that
  exercises every command in headless mode (`TUI.RENDER_HEADLESS`
  for the gate).
* `jdb/tui_demo.jdb` polish (the showcase).
* Run pre-commit gate; commit + push.

## Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| FTXUI's `WithRestoredIO` doesn't compose with our recreate-every-frame model | Mid | Phase A spike: write a 50-line proof of concept first; if it hangs, fall back to running TUI scripts in a child process (same as the abandoned `--ftxui-edit` plan) |
| Widget-id collisions in our per-widget state map | Mid | Hash on (file_path, line_number, call_index_within_frame) — same shape ImGui uses |
| Mouse on legacy cmd.exe (pre-Win Terminal) | Low | Document the requirement; smoke-test on Win Terminal + Konsole only |
| Build size explosion | Low | We already link FTXUI for the REPL — the marginal cost is the ~2000 LoC of glue, single-digit KB binary growth |
| Animations vs MCP single-thread VM | Mid | TUI scripts running under the MCP worker queue would block other tool calls. Same constraint as space_shooter today — document the trade-off, don't fix architecturally in this phase |

## Sequencing relative to other work

* **Master-Plan release sprint** (Linux port, demo video, HN post) takes priority — TUI.* is a Phase ≥5 inner build, not a launch blocker.
* **TUI.* can begin AFTER Strix Halo Linux port lands** — we want the lib working on both platforms before exposing a new API surface to users.
* **Editor parity in FTXUI** stays explicitly deferred (legacy `Editor` handles it via `WithRestoredIO`); TUI.* is for application UIs, not editors.

## Definition of done

Same bar as Phase 2c:

1. `jdb/tui_demo.jdb` runs and looks the way the screenshots in this doc tree promise.
2. Smoke test in `tests/test_tui_smoke.jdb` exercises every TUI.* native at least once and exits clean (added to the pre-commit gate).
3. ImGui port test: take an existing GUI.* script (`fluppi/rpg_demo.jdb` or smaller), do a sed `s/GUI\./TUI./` + drop the `SCREEN`/`SCREENFLIP`, and verify it runs in the terminal.
4. Pre-commit gate (4 suites × 2 backends + GUI smokes + new TUI smoke) all green.
5. Linux test on Strix Halo + NVIDIA machines — no Win-Terminal-specific assumptions sneak in.
