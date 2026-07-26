#pragma once
//
// TUI.* runtime state - Phase A scaffolding.
//
// Compiled only when /DTUI is set. The struct holds the per-script
// state that survives across native calls within one frame:
//   - layout/component stack (one collector per HBOX/VBOX/BORDER/...)
//   - modal stack
//   - last keyboard/mouse event for TUI.KEY$/TUI.MOUSE_*
//   - theme handle (drives jdb_theme slots and FTXUI palette)
//   - quit flag drained by TUI.QUIT()
//
// Phase A keeps the surface minimal - Phases B+ flesh out the
// collectors and event cache as widgets land.

#ifdef TUI

#include <memory>
#include <string>
#include <vector>

#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>

namespace ftxui { class ScreenInteractive; }

namespace jdb_tui {

// One frame in the layout stack - a collector for child elements
// plus the parameters needed to fold them into an Element on close.
struct LayoutFrame {
    enum Kind { ROOT, HBOX, VBOX, BORDER, GRID, MODAL, TAB };
    Kind kind = ROOT;
    std::vector<ftxui::Element> children;
    std::string title;   // BORDER (becomes window title) - empty → border()
    int cols = 1;        // GRID - number of columns
    // When true, fold_frame returns an empty element and pop_and_attach
    // drops the result. Used for inactive tabs and inactive modal blocks
    // so the script can keep its IF TUI.TAB_BEGIN(...) THEN ... ENDIF idiom.
    bool discard = false;
};

struct TuiState {
    bool in_frame = false;       // between TUI.BEGIN and TUI.END
    bool quit_requested = false; // toggled by TUI.EXIT / Ctrl+Q
    bool alt_screen_active = false; // true once we swapped to the alt buffer
    bool pending_same_line = false; // SAME_LINE marker - joins next emit hbox-style
    std::string frame_title;

    // Layout stack. Always size >= 1 between BEGIN/END; layout_stack[0]
    // is the ROOT frame whose children become the rendered document.
    std::vector<LayoutFrame> layout_stack;

    // ── Focus + input plumbing (Phase D) ───────────────────
    // Each interactive widget bumps `widget_seen_this_frame`. The
    // count at TUI.END becomes the modulus for `focus_index` so Tab
    // wraps around cleanly. `pending_action` holds the key drained
    // at the END of the previous frame, ready to be consumed by
    // exactly one focused widget in THIS frame.
    int focus_index = 0;
    int widget_seen_this_frame = 0;
    int widget_count_last_frame = 0;
    std::string pending_action;

    // ── Canvas builder (Phase E) ───────────────────────────
    // TUI.CANVAS_BEGIN/END brackets a sequence of TUI.LINE /
    // TUI.PIXEL calls. We queue the draw commands as a closure
    // list; CANVAS_END emits a single canvas() Element that
    // replays them when FTXUI renders the frame.
    struct CanvasOps {
        bool active = false;
        int w = 0, h = 0;
        std::vector<std::function<void(ftxui::Canvas&)>> ops;
    } canvas;

    // ── Table builder (Phase E) ────────────────────────────
    // TUI.TABLE_BEGIN(headers[]) seeds row 0; TUI.TABLE_ROW(cells[])
    // appends. TABLE_END turns the rows into one ftxui::Table and
    // emits its rendered Element.
    struct TableState {
        bool active = false;
        std::vector<std::vector<std::string>> rows;
    } table;

    // Last drained event - fed by the FTXUI CatchEvent in TUI.RENDER.
    std::string last_key;
    int mouse_x = 0, mouse_y = 0;
    int mouse_buttons = 0;
    int mouse_wheel = 0;

    // Active theme name. Phase G uses it as a title-bar tint;
    // future phases can drive jdb_theme::* slots from here.
    std::string theme = "cool";

    // Colour/style stacks (Phase G). Each entry decorates the NEXT
    // emit_element until popped. The top-of-stack effect is applied
    // when emitting; emitting does NOT pop - POP_COLOR/STYLE_POP do.
    struct StyleEntry {
        bool has_fg = false;
        bool has_bg = false;
        ftxui::Color fg;
        ftxui::Color bg;
        int style_mask = 0; // bitflags: 1=bold, 2=dim, 4=italic, 8=underlined, 16=inverted
    };
    std::vector<StyleEntry> style_stack;

    // Owned screen when running outside the FTXUI REPL.
    std::unique_ptr<ftxui::ScreenInteractive> screen_owned;
    // Borrowed pointer to the REPL's screen when running embedded -
    // set via tui_set_host_screen() by repl_ftxui.cpp.
    ftxui::ScreenInteractive* host_screen = nullptr;

    // Diagnostics
    double last_render_ms = 0.0;

    // ── Modal (Phase F) ────────────────────────────────────
    // active_id is non-empty while a modal is "shown". MODAL_OPEN sets
    // it directly; MODAL_BEGIN(id) returns true iff its arg matches.
    // The captured body is overlaid on top of the main doc in RENDER.
    struct ModalState {
        std::string active_id;
        ftxui::Element captured_body; // null when no modal this frame
    } modal;

    // ── Menubar (Phase F) ──────────────────────────────────
    // Each MENUBAR_BEGIN/END pair runs once per frame; submenus seen
    // in order get an implicit index. F-keys F1..F10 open submenus.
    // open_index = -1 → no submenu open. item_focus tracks which row
    // inside the open submenu is highlighted; Up/Down navigates,
    // Enter fires the item.
    struct MenuState {
        bool bar_active = false;
        int open_index = -1;
        int submenus_seen_this_frame = 0;
        int items_seen_in_open = 0;
        int item_focus = 0;
        int item_count_last_frame = 0;
        bool item_enter_pending = false; // set on Enter while menu open
        std::vector<ftxui::Element> bar_row;     // submenu labels for menubar
        std::vector<ftxui::Element> popup_rows;  // items of the open submenu
        int current_submenu_idx = -1;            // -1 outside SUBMENU
    } menu;

    // ── Tab bar (Phase F) ──────────────────────────────────
    struct TabBarState {
        bool active = false;
        std::vector<std::string> labels;
        int active_idx = 0;
    } tab_bar;
};

TuiState& state();

// Hook for repl_ftxui.cpp - call when the REPL enters/leaves its
// main loop so TUI.BEGIN can suspend it via WithRestoredIO instead
// of constructing its own ScreenInteractive.
void set_host_screen(ftxui::ScreenInteractive* screen);

} // namespace jdb_tui

#endif // TUI
