# TUI.* — Terminal UI for jdBasic (FTXUI bridge)

**Status:** design proposal · drafted 2026-05-11
**Sister doc:** [`ftxui_api.md`](./ftxui_api.md), [`ftxui_implementation.md`](./ftxui_implementation.md), [`ftxui_testing.md`](./ftxui_testing.md)

## Goal

Give jdBasic programs the same kind of "build a UI from a script" power that the existing **GUI.*** namespace (SDL3+ImGui, 69 natives) gives for graphical windows — but rendered into the **terminal** via FTXUI. Scripts target either backend with near-identical mental model:

```basic
' ImGui (SDL window)             ' FTXUI (terminal)
SCREEN 1024, 768, "App", 0       OPTION "NOPAUSE"
DO                               DO
    GUI.BEGIN "App"                  TUI.BEGIN "App"
    GUI.TEXT "Hello"                 TUI.TEXT "Hello"
    IF GUI.BUTTON("OK") THEN ...     IF TUI.BUTTON("OK") THEN ...
    GUI.END                          TUI.END
    SCREENFLIP                       TUI.RENDER
LOOP UNTIL quit                  LOOP UNTIL TUI.QUIT()
```

**The whole point:** a jdBasic dev who already wrote a GUI.* app should be able to retarget the same script to the terminal with minimal renaming. The TUI.* namespace shadows the GUI.* one slot-for-slot wherever the underlying widgets exist on both sides.

## Why FTXUI and not raw ANSI

The Phase 0–2c REPL refactor already linked FTXUI into the jdBasic build (`libs/ftxui/build/ftxui.lib`, ~17 MB) and proved out the architecture in `src/repl_ftxui.cpp`. The lib is paid for. Routing jdBasic scripts at it costs ~1 KB of glue per widget vs the alternatives:

* **Raw ANSI**: every script has to reinvent cursor movement, mouse handling, resize events, and the differences between Win Terminal / cmd.exe / Linux konsole. The legacy `editor.cpp` shows the cost — 1540 lines of mostly-terminal-plumbing.
* **ncurses**: POSIX-only, ugly C API, hostile to retained-state idioms, no out-of-box mouse support on Windows.
* **Notcurses**: more capable than FTXUI but C-only and harder to embed.
* **Roll-our-own**: explicit non-goal — we want to write demos, not terminal abstractions.

FTXUI handles every cross-platform pain point we'd otherwise have to reimplement: rendering, focus tracking, mouse, resize events, colour palettes, key parsing, animation loops.

## Architectural tension and how we resolve it

ImGui is **immediate-mode**: every frame, the script re-issues every widget call and ImGui figures out what changed. FTXUI is **component-based / retained**: a component tree exists across frames, animations and focus carried by component state.

These models don't mix naturally — but the script-side API doesn't have to expose the implementation. We pick:

* **External API: immediate-mode** (matches GUI.*, low cognitive load for jdBasic devs).
* **Internal implementation: rebuild a fresh FTXUI Element tree every TUI.RENDER call**, driven by what the script issued between `TUI.BEGIN` and `TUI.END`.

Cost: every render rebuilds the tree. FTXUI is fast enough that this isn't a problem for terminal-sized UIs (~thousands of cells, not millions of pixels).

Benefit: jdBasic scripts written for GUI.* port to TUI.* almost mechanically.

## Two interaction loops, one script

ImGui scripts already pump a per-frame loop with `SCREENFLIP`. TUI.* scripts get a matching `TUI.RENDER` that:

1. Takes the Element tree the script just built (the calls between `TUI.BEGIN` and `TUI.END`).
2. Hands it to FTXUI for one render pass.
3. Drains keyboard + mouse + resize events into per-event globals (`TUI.EVENT_KEY$()`, `TUI.MOUSE_X()`, etc.) the script can poll on the next iteration.
4. Returns once the frame is committed.

If the script wants animation, it loops with a `SLEEP 16`. If it wants event-driven idle, it calls `TUI.WAIT_EVENT()` and only redraws on input. Both modes are supported by the same primitives.

## Coexistence with the FTXUI REPL

The REPL itself owns a `ScreenInteractive`. A script using TUI.* needs its own. We can't nest two screens in the same process — same constraint the `edit <file>` command hits in the REPL.

**Resolution:** when a script calls `TUI.BEGIN` the first time:

* Detect whether we're running from the FTXUI REPL (via a global hook set by `run_repl_ftxui`).
* If yes — request `screen.WithRestoredIO` for the duration of the script (REPL is suspended, script's TUI session takes the terminal, REPL resumes when the script returns).
* If no — start a fresh `ScreenInteractive::Fullscreen()` directly.

This matches the `edit` integration that already works.

## What this is NOT

* **Not a full editor framework** — TUI.* is for *building app UIs* from jdBasic scripts; the FTXUI editor inside the REPL stays a separate concern (and for now delegates to the legacy `Editor` class anyway).
* **Not a multi-window window manager** — one TUI script owns the terminal at a time. Use FTXUI's modal/popup for in-screen layers.
* **Not a complete ImGui parity sheet** — features that don't make sense in a terminal (font glyphs, custom paint, ImGui drag-n-drop within the same window) are explicitly out of scope. The Befehlsliste in `ftxui_api.md` makes the mapping table explicit.

## Success criteria

The bar for "we shipped TUI.*":

1. `jdb/tui_demo.jdb` runs and shows: tabs, table, live progress, menu bar, modal, color picker, animated heartbeat — without crashing on Win Terminal or Konsole.
2. A jdBasic dev can port a small ImGui script to TUI.* by replacing `GUI.` with `TUI.` and dropping `SCREEN`/`SCREENFLIP` for `TUI.RENDER`. Edge-cases get flagged in `ftxui_api.md`.
3. Mouse + keyboard work on both Windows Terminal and a Linux Konsole/gnome-terminal session.
4. Pre-commit gate passes (4 suites × 2 backends + 2 GUI smokes + a new `tui_demo` headless smoke).

If any of those slips, the public TUI.* surface gets demoted to `OPTION "EXPERIMENTAL"` and we don't put it in the README yet.
