# TUI.* — Test Plan

**Status:** design proposal · drafted 2026-05-11
**Sister doc:** [`ftxui_plan.md`](./ftxui_plan.md), [`ftxui_api.md`](./ftxui_api.md), [`ftxui_implementation.md`](./ftxui_implementation.md)

The TUI namespace is interactive by nature — the visible UI is the contract — so testing has two tracks:

1. **Headless smoke** — exercise every native at least once with `TUI.RENDER_HEADLESS` (a render variant that runs one frame against an in-memory screen buffer instead of the terminal). Catches crashes, returns the buffer as a string we can grep. Runs in the pre-commit gate.
2. **Interactive demos** — `jdb/tui_demo.jdb` is the showcase that gets eyeballed on Win Terminal + Konsole during release prep. Not run by CI.

## Pre-commit gate addition

Add to `.claude/skills/jdbgate/SKILL.md`:

```
./build/jdBasic.exe tests/test_tui_smoke.jdb  → ALL TESTS PASSED!
```

The smoke is interpreter-only — TUI commands are runtime-native, no value in native-compile testing them.

## Headless smoke — `tests/test_tui_smoke.jdb`

```basic
' Headless TUI exercise — every native at least once.
' TUI.RENDER_HEADLESS replaces the live screen with an in-memory
' buffer; we then assert on the buffer's string content.

OPTION "EXPLICIT"

DIM passed AS INTEGER = 0
DIM failed AS INTEGER = 0

SUB expect(name$, cond)
    IF cond THEN
        passed = passed + 1
    ELSE
        PRINT "FAIL: "; name$
        failed = failed + 1
    ENDIF
ENDSUB

' ── 1. Core loop ────────────────────────────────────────────
TUI.BEGIN "smoke"
TUI.TEXT "hello world"
TUI.END
DIM buf$ = TUI.RENDER_HEADLESS$()
expect "text rendered", INSTR(buf$, "hello world") > 0

' ── 2. Layout — hbox, vbox, border, separator ───────────────
TUI.BEGIN "layout"
TUI.HBOX_BEGIN
TUI.TEXT "left"
TUI.SEPARATOR
TUI.TEXT "right"
TUI.HBOX_END
TUI.SEPARATOR
TUI.BORDER_BEGIN "boxed"
TUI.TEXT "inside"
TUI.BORDER_END
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "hbox left", INSTR(buf$, "left") > 0
expect "hbox right", INSTR(buf$, "right") > 0
expect "border title", INSTR(buf$, "boxed") > 0
expect "border body", INSTR(buf$, "inside") > 0

' ── 3. Input widgets — return state under simulated input ──
DIM checked AS INTEGER = 0
DIM input_text$ AS STRING = "hello"
DIM int_val AS INTEGER = 42

TUI.BEGIN "inputs"
DIM clicked = TUI.BUTTON("Click")
TUI.CHECKBOX("Toggle", checked)
TUI.INPUT("Name", input_text$)
TUI.INPUT_INT("Count", int_val)
TUI.END
TUI.RENDER_HEADLESS$()
expect "button initial state", clicked = 0
expect "checkbox preserved", checked = 0
expect "input preserved", input_text$ = "hello"
expect "int preserved", int_val = 42

' ── 4. Display — progress, gauge, spinner ───────────────────
TUI.BEGIN "display"
TUI.PROGRESS 0.5, "loading"
TUI.GAUGE 0.75
TUI.SPINNER 3
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "progress label", INSTR(buf$, "loading") > 0

' ── 5. Table ────────────────────────────────────────────────
TUI.BEGIN "table"
TUI.TABLE_BEGIN ["Name", "Score"]
TUI.TABLE_ROW ["Alice", "99"]
TUI.TABLE_ROW ["Bob",   "42"]
TUI.TABLE_END
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "table header", INSTR(buf$, "Name") > 0 AND INSTR(buf$, "Score") > 0
expect "table rows",   INSTR(buf$, "Alice") > 0 AND INSTR(buf$, "Bob") > 0

' ── 6. Menu bar + submenu ───────────────────────────────────
TUI.BEGIN "menus"
TUI.MENUBAR_BEGIN
IF TUI.SUBMENU_BEGIN("File") THEN
    TUI.MENUITEM "New"
    TUI.MENUITEM "Open"
    TUI.SUBMENU_END
ENDIF
TUI.MENUBAR_END
TUI.TEXT "body"
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "menubar File", INSTR(buf$, "File") > 0

' ── 7. Tabs ─────────────────────────────────────────────────
DIM active_tab AS INTEGER = 0
TUI.BEGIN "tabs"
TUI.TAB_BAR_BEGIN ["A", "B", "C"], active_tab
IF TUI.TAB_BEGIN("A") THEN
    TUI.TEXT "alpha"
    TUI.TAB_END
ENDIF
IF TUI.TAB_BEGIN("B") THEN
    TUI.TEXT "beta"
    TUI.TAB_END
ENDIF
TUI.TAB_BAR_END
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "active tab body", INSTR(buf$, "alpha") > 0

' ── 8. Modal — open + render + dismiss ─────────────────────
TUI.MODAL_OPEN "confirm"
TUI.BEGIN "with-modal"
TUI.TEXT "main"
IF TUI.MODAL_BEGIN("confirm", "Confirm?") THEN
    TUI.TEXT "Sure?"
    TUI.MODAL_END
ENDIF
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "modal title", INSTR(buf$, "Confirm?") > 0
expect "modal body", INSTR(buf$, "Sure?") > 0
TUI.MODAL_CLOSE

' ── 9. Canvas — draw a line, assert visible braille ────────
TUI.BEGIN "canvas"
TUI.CANVAS_BEGIN 40, 10
TUI.LINE 0, 0, 39, 9, 7
TUI.CANVAS_END
TUI.END
buf$ = TUI.RENDER_HEADLESS$()
expect "canvas has glyphs", LEN(buf$) > 100

' ── 10. Theme + colour stacks ──────────────────────────────
TUI.THEME "warm"
TUI.BEGIN "theme"
TUI.COLOR 255, 100, 50
TUI.TEXT "tinted"
TUI.POP_COLOR
TUI.END
TUI.RENDER_HEADLESS$()
expect "theme switch survives", TRUE
TUI.THEME "cool"

' ── 11. Diagnostics ────────────────────────────────────────
expect "width positive",  TUI.WIDTH()  > 0
expect "height positive", TUI.HEIGHT() > 0
expect "version present", LEN(TUI.VERSION$()) > 0

' ── Summary ─────────────────────────────────────────────────
PRINT "============================================="
PRINT "RESULTS: "; passed; " passed, "; failed; " failed"
PRINT "============================================="
IF failed = 0 THEN PRINT "ALL TESTS PASSED!"
```

## Manual interactive test matrix

Run `jdb/tui_demo.jdb` on each terminal/OS and confirm:

| What | Win Terminal | cmd.exe (legacy) | Konsole / gnome-terminal | macOS Terminal.app |
|---|---|---|---|---|
| Text renders, no garbled glyphs | ✓ | (degraded — no truecolour) | ✓ | ✓ |
| Mouse-click on buttons | ✓ | ✗ (no mouse mode) | ✓ | ✓ |
| Mouse wheel scroll | ✓ | ✗ | ✓ | ✓ |
| Window resize live | ✓ | ✓ | ✓ | ✓ |
| Alt+Enter (or whatever fullscreen toggle) doesn't crash | ✓ | n/a | ✓ | n/a |
| Modal overlays the main view | ✓ | ✓ | ✓ | ✓ |
| Canvas braille renders | ✓ | partial | ✓ | ✓ |
| Theme switch live | ✓ | ✓ | ✓ | ✓ |

If a row is ✗ on a terminal, the demo's status bar prints `(unsupported: <feature>)` instead of crashing.

## Edge cases that need explicit tests

| Case | Test |
|---|---|
| Calling `TUI.RENDER` before `TUI.BEGIN` | Must error with "TUI.RENDER without TUI.BEGIN", not crash |
| Mismatched `HBOX_BEGIN`/`VBOX_END` | Error "TUI layout stack mismatch", not crash |
| Modal opened but `TUI.MODAL_BEGIN` never called | Modal queued, drained next time `TUI.MODAL_BEGIN` is hit |
| `TUI.INPUT` with an undefined variable | Error "TUI.INPUT: variable 'foo' not declared" |
| `TUI.BEGIN` called twice without `TUI.END` | Error "TUI.BEGIN already active" |
| Empty `TUI.TABLE_BEGIN [...]` headers | Renders zero-column table with no row guides |
| 0-frac on `TUI.PROGRESS` | Empty bar |
| 1.5-frac on `TUI.PROGRESS` | Clamped to 1.0, no overrun |
| `TUI.WAIT_EVENT` inside `TUI.MODAL_BEGIN` | Modal stays drawn; event fires when user types into it |
| `TUI.THEME "noexist"` | Logs warning, leaves current theme unchanged |
| Script crashes mid-frame | Atexit handler restores terminal state — no zombie raw-mode |

## Coverage target

* Every TUI.* native called by `test_tui_smoke.jdb` at least once
* Branch coverage on `tui_state.cpp` ≥ 75 %
* No specific test for animation timing — `TUI.LAST_RENDER_MS()` is a fps-budget tool, not a tested behaviour

## What CI does NOT cover

* Visual correctness — gate trusts that if the buffer-grep passes, the render is "close enough"
* Theme aesthetics — eyeballed during release prep
* Mouse-drag selection (no headless mouse simulation)
* `TUI.LINK` URL-launching the OS browser (would actually open URLs in CI 😅)

## Test-plan dependencies

* `TUI.RENDER_HEADLESS$()` must land in Phase A — it's the test foundation. If we skip it, every smoke test becomes a manual eyeball check.
* The gate skill picks up `test_tui_smoke.jdb` automatically once it's in `tests/` (the skill globs `tests/test_*.jdb`).
