# TUI.* — API Reference (proposal)

**Status:** design proposal · drafted 2026-05-11
**Sister doc:** [`ftxui_plan.md`](./ftxui_plan.md), [`ftxui_implementation.md`](./ftxui_implementation.md), [`ftxui_testing.md`](./ftxui_testing.md)

Every command shown here is **proposed** — none of these exist yet. They map onto FTXUI v6.x building blocks. Where a matching GUI.* command already exists, the table column "GUI.* twin" names it so devs porting from ImGui can find the analogue.

Conventions:

* All commands live in the `TUI` namespace (`TUI.BEGIN`, etc.).
* Boolean returns are `0` / non-zero (matches GUI.*).
* Modifying a script-side variable uses jdBasic's `BYREF`-style: the variable name is passed, the command updates it in place. This matches `GUI.SLIDER`, `GUI.CHECKBOX`, etc.
* Colours are 0-based `Color` codes (we expose the FTXUI 256-colour palette via integer codes, plus named constants `TUI.RED`, `TUI.CYAN3`, etc., defined as DIMs in a stdlib).

## 1 · Core loop

| Command | Signature | What it does | GUI.* twin |
|---|---|---|---|
| `TUI.BEGIN` | `TUI.BEGIN title$` | Opens a frame's component tree. Title shown in top border if no menu bar is drawn. | `GUI.BEGIN` |
| `TUI.END` | `TUI.END` | Closes the frame. Must match `TUI.BEGIN`. | `GUI.END` |
| `TUI.RENDER` | `TUI.RENDER` | Submits the current tree to FTXUI, draws one pass, drains events. Blocking by default — returns within ~16 ms (or sooner on input). | `SCREENFLIP` |
| `TUI.WAIT_EVENT` | `TUI.WAIT_EVENT [timeout_ms]` | Blocks until any event arrives or the timeout expires. Returns 1 if there was an event, 0 on timeout. Use instead of `TUI.RENDER` for event-driven (non-animated) UIs. | (none — ImGui is always animated) |
| `TUI.QUIT` | `TUI.QUIT() -> bool` | True once the user has asked to leave (Ctrl+Q, window close, or the script calling `TUI.EXIT`). The natural loop predicate. | (none — ImGui uses `running=0`) |
| `TUI.EXIT` | `TUI.EXIT` | Signal the loop to terminate next iteration. | (none) |

## 2 · Layout primitives

FTXUI layouts compose via `hbox`, `vbox`, `gridbox`, etc. We mirror those plus the immediate-mode `GUI.SAME_LINE` / `GUI.SEPARATOR` shortcuts.

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.HBOX_BEGIN` | `TUI.HBOX_BEGIN` | Start a horizontal box. Subsequent widgets stack left-to-right. | (implicit in ImGui) |
| `TUI.HBOX_END` | `TUI.HBOX_END` | End horizontal box. | |
| `TUI.VBOX_BEGIN` | `TUI.VBOX_BEGIN` | Start a vertical box (default). | |
| `TUI.VBOX_END` | `TUI.VBOX_END` | End vertical box. | |
| `TUI.GRID_BEGIN` | `TUI.GRID_BEGIN cols` | Start a grid with `cols` columns. Each subsequent widget fills one cell. | (none) |
| `TUI.GRID_END` | `TUI.GRID_END` | End grid. | |
| `TUI.BORDER_BEGIN` | `TUI.BORDER_BEGIN [title$]` | Draw a border around the next group. Title optional in top edge. | `GUI.BEGIN_CHILD` |
| `TUI.BORDER_END` | `TUI.BORDER_END` | Close border. | `GUI.END_CHILD` |
| `TUI.SAME_LINE` | `TUI.SAME_LINE` | Place the next widget on the same row as the previous (auto-converts current container to hbox if needed). | `GUI.SAME_LINE` |
| `TUI.SEPARATOR` | `TUI.SEPARATOR` | One horizontal line. | `GUI.SEPARATOR` |
| `TUI.SEPARATOR_TEXT` | `TUI.SEPARATOR_TEXT s$` | Separator with caption. | `GUI.SEPARATOR_TEXT` |
| `TUI.SPACER` | `TUI.SPACER` | Flexible space (consumes remaining width/height in its box). | `GUI.DUMMY` (different semantics) |
| `TUI.SIZE` | `TUI.SIZE w, h` | Constrain the next widget to width × height cells. -1 = flex. | (none — ImGui uses ItemWidth) |

## 3 · Static text

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.TEXT` | `TUI.TEXT s$ [, style$]` | Plain text. `style$` may be any of: `"bold"`, `"dim"`, `"inverted"`, `"underlined"`, `"blink"`, or a comma-separated combination (`"bold,underlined"`). | `GUI.TEXT` |
| `TUI.HEADING` | `TUI.HEADING s$ [, level]` | Larger title (level 1–3 controls weight: bold > bold-underline > underline). | (none) |
| `TUI.PARAGRAPH` | `TUI.PARAGRAPH s$` | Word-wraps to the available width. | `GUI.TEXT_WRAPPED` |
| `TUI.LINK` | `TUI.LINK label$, url$` | A clickable text that opens `url$` in the OS default browser. | (none) |
| `TUI.HELP_MARKER` | `TUI.HELP_MARKER s$` | "(?)" glyph with tooltip on hover. | `GUI.HELPMARKER` |
| `TUI.TOOLTIP` | `TUI.TOOLTIP s$` | Tooltip attached to the previously emitted widget. | `GUI.TOOLTIP` |

## 4 · Input widgets

Most widgets follow the GUI.* `byref` pattern: pass a variable name; the command writes the new value back. Returns whether the user interacted this frame (so the script can branch on "just changed").

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.BUTTON` | `TUI.BUTTON(label$ [, style$]) -> bool` | Returns true on the frame it was clicked or activated via Enter while focused. | `GUI.BUTTON` |
| `TUI.CHECKBOX` | `TUI.CHECKBOX(label$, var) -> bool` | Toggleable. `var` is bool; returns true if toggled this frame. | `GUI.CHECKBOX` |
| `TUI.RADIO` | `TUI.RADIO(label$, group_var, my_value) -> bool` | Radio button in a `group_var`-shared set. Returns true on selection. | `GUI.RADIO` |
| `TUI.INPUT` | `TUI.INPUT(label$, text_var$) -> bool` | Single-line text input. Returns true on Enter. | `GUI.INPUT` |
| `TUI.INPUT_INT` | `TUI.INPUT_INT(label$, int_var) -> bool` | Numeric input; rejects non-digits. | `GUI.INPUT_INT` |
| `TUI.INPUT_DOUBLE` | `TUI.INPUT_DOUBLE(label$, dbl_var) -> bool` | Float input. | `GUI.INPUT_DOUBLE` |
| `TUI.SLIDER` | `TUI.SLIDER(label$, val_var, min, max) -> bool` | Visual slider, drag with mouse or ←/→ when focused. Returns true on value change. | `GUI.SLIDER` |
| `TUI.MENU` | `TUI.MENU(label$, items_arr, selected_var) -> bool` | A vertical pickable menu (FTXUI's `Menu` component). Returns true on Enter. | `GUI.LISTBOX` |
| `TUI.DROPDOWN` | `TUI.DROPDOWN(label$, items_arr, selected_var) -> bool` | A collapsed menu that expands on click. | `GUI.COMBO` |
| `TUI.SELECTABLE` | `TUI.SELECTABLE(label$, selected_var) -> bool` | A row that the user can pick / unpick — building block for tables. | `GUI.SELECTABLE` |

## 5 · Display / visualisation

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.PROGRESS` | `TUI.PROGRESS frac [, label$]` | Bar fraction 0..1. | `GUI.PROGRESS` |
| `TUI.GAUGE` | `TUI.GAUGE frac` | Inline bar (no label, sized to current box). | (none) |
| `TUI.SPINNER` | `TUI.SPINNER frame_idx [, style]` | Rotating glyph. `style` 0..21 picks one of FTXUI's spinner sets. | (none — ImGui has no spinner) |
| `TUI.CANVAS_BEGIN` | `TUI.CANVAS_BEGIN w, h` | Open a drawable area (FTXUI braille canvas). | (none) |
| `TUI.CANVAS_END` | `TUI.CANVAS_END` | Close. | |
| `TUI.PIXEL` | `TUI.PIXEL x, y, color` | Single sub-cell pixel inside the canvas. | (none) |
| `TUI.LINE` | `TUI.LINE x1, y1, x2, y2, color` | Bresenham line in the canvas. | `LINE` (graphics native) |
| `TUI.TABLE_BEGIN` | `TUI.TABLE_BEGIN headers_arr` | Open a table with the given header row. | `GUI.BEGIN_TABLE` (existing) |
| `TUI.TABLE_ROW` | `TUI.TABLE_ROW cells_arr` | Append one row (array of N strings). | (auto in ImGui) |
| `TUI.TABLE_END` | `TUI.TABLE_END` | Close table. Renders all rows with auto-sized columns. | `GUI.END_TABLE` |

## 6 · Modal & windows

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.MODAL_OPEN` | `TUI.MODAL_OPEN id$` | Queue a modal with that id. Render it inside the matching `TUI.MODAL_BEGIN`/`END`. | `GUI.OPEN_POPUP` |
| `TUI.MODAL_BEGIN` | `TUI.MODAL_BEGIN(id$ [, title$]) -> bool` | Returns true if the modal is currently visible — issue widgets inside. | `GUI.BEGIN_POPUP` |
| `TUI.MODAL_END` | `TUI.MODAL_END` | Close the modal block. | `GUI.END_POPUP` |
| `TUI.MODAL_CLOSE` | `TUI.MODAL_CLOSE` | Dismiss the current modal. | `GUI.CLOSE_CURRENT_POPUP` |

## 7 · Menu bar

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.MENUBAR_BEGIN` | `TUI.MENUBAR_BEGIN` | Top menu bar (always rendered above `TUI.BEGIN`'s frame). | `GUI.BEGIN_MAIN_MENU_BAR` |
| `TUI.MENUBAR_END` | `TUI.MENUBAR_END` | Close menu bar. | `GUI.END_MAIN_MENU_BAR` |
| `TUI.SUBMENU_BEGIN` | `TUI.SUBMENU_BEGIN(label$) -> bool` | Open a drop-down inside the menu bar. Returns true if the submenu is currently shown. | `GUI.BEGIN_MENU` |
| `TUI.SUBMENU_END` | `TUI.SUBMENU_END` | Close. | `GUI.END_MENU` |
| `TUI.MENUITEM` | `TUI.MENUITEM(label$ [, shortcut$ [, checked]]) -> bool` | A clickable menu entry. Returns true on click. | `GUI.MENU_ITEM` |

## 8 · Tabs

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.TAB_BAR_BEGIN` | `TUI.TAB_BAR_BEGIN tabs_arr, active_var` | Render a horizontal tab strip. Updates `active_var` (0-based index) on click. | `GUI.BEGIN_TAB_BAR` |
| `TUI.TAB_BAR_END` | `TUI.TAB_BAR_END` | Close. | `GUI.END_TAB_BAR` |
| `TUI.TAB_BEGIN` | `TUI.TAB_BEGIN(label$) -> bool` | Body of a tab. Returns true if this tab is currently the active one (caller renders body only if true). | `GUI.BEGIN_TAB_ITEM` |
| `TUI.TAB_END` | `TUI.TAB_END` | Close. | `GUI.END_TAB_ITEM` |

## 9 · Colour and style

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.COLOR` | `TUI.COLOR r, g, b` | Push a foreground colour. Affects subsequent widgets until `TUI.POP_COLOR`. | `GUI.COLOR` (existing!) |
| `TUI.BG_COLOR` | `TUI.BG_COLOR r, g, b` | Push background. | (none) |
| `TUI.POP_COLOR` | `TUI.POP_COLOR` | Pop one fg colour. | (none) |
| `TUI.POP_BG_COLOR` | `TUI.POP_BG_COLOR` | Pop one bg colour. | (none) |
| `TUI.STYLE_PUSH` | `TUI.STYLE_PUSH style$` | Push bold/dim/inverted/etc. styles. | (none) |
| `TUI.STYLE_POP` | `TUI.STYLE_POP` | Pop. | (none) |
| `TUI.THEME` | `TUI.THEME name$` | Switch the whole jdb_theme palette. `name$` = `"cool"`, `"warm"`, `"neon"`, `"cyber"`, `"gold"`. Reuses the space_shooter palette set. | (none) |

## 10 · Events

| Command | Signature | What | GUI.* twin |
|---|---|---|---|
| `TUI.KEY$` | `TUI.KEY$() -> string` | Returns the key pressed this frame (e.g. `"A"`, `"Enter"`, `"F5"`), or `""` if no key. | `GFX.POLLEVENT`-ish |
| `TUI.MOUSE_X` | `TUI.MOUSE_X() -> int` | Mouse column. | `MOUSE_X` |
| `TUI.MOUSE_Y` | `TUI.MOUSE_Y() -> int` | Mouse row. | `MOUSE_Y` |
| `TUI.MOUSE_BUTTON` | `TUI.MOUSE_BUTTON(n) -> bool` | True if button `n` (0=left, 1=middle, 2=right) is currently down. | `MOUSE_BUTTON` |
| `TUI.MOUSE_WHEEL` | `TUI.MOUSE_WHEEL() -> int` | +1 / -1 / 0 for the current frame's wheel delta. | (none) |
| `TUI.ON` | `TUI.ON event$ CALL handler` | Register a SUB to fire on specific events (`"QUIT"`, `"RESIZE"`, `"KEYDOWN"`). Mirrors `ON "..." CALL ...`. | (existing `ON "..."`) |

## 11 · Diagnostics

| Command | Signature | What |
|---|---|---|
| `TUI.WIDTH` | `TUI.WIDTH() -> int` | Terminal width in cells. |
| `TUI.HEIGHT` | `TUI.HEIGHT() -> int` | Terminal height in cells. |
| `TUI.LAST_RENDER_MS` | `TUI.LAST_RENDER_MS() -> double` | Wall-clock time the previous `TUI.RENDER` took. Useful for animation budgeting. |
| `TUI.VERSION$` | `TUI.VERSION$() -> string` | FTXUI version + jdBasic build. |

## What's NOT in scope

Explicitly **not** part of the TUI namespace because the terminal can't render them sensibly:

* Custom font glyphs (we get exactly one monospace cell)
* Pixel-perfect drag-and-drop (we have mouse but no sub-cell precision outside `TUI.CANVAS`)
* The full ImGui Demo Window — that's a feature graveyard. We hit the 80 % use cases (forms, tables, charts, modals) and stop.
* `GUI.PLOT_LINES` style charts — replace with `TUI.CANVAS_BEGIN` + `TUI.LINE` and let the script draw what it wants.

## Total command count

* Core loop: 6
* Layout: 11
* Text: 6
* Inputs: 10
* Display: 10
* Modal: 4
* Menu bar: 5
* Tabs: 4
* Colour/style: 7
* Events: 6
* Diagnostics: 4

**63 commands total**, close to the GUI.* count (69). Coverage is intentionally similar.
