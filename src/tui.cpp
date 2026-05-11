// ============================================================
// TUI.* — FTXUI bridge for jdBasic.
//
// Phase B implements the core loop: BEGIN / END / RENDER /
// WAIT_EVENT / QUIT / EXIT, plus a minimal TUI.TEXT so a script
// can actually display something. The remaining 56 names stay
// as Phase-A stubs until their phase lands.
//
// Compiled only under /DTUI. The #else branch at the bottom
// provides a no-op register_tui_natives so callers don't have
// to guard the call site.
// ============================================================

#ifdef TUI

#include "tui.h"
#include "tui_state.h"
#include "vm.h"
#include "value.h"

#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>
#include <algorithm>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <conio.h>   // _kbhit / _getch — non-blocking key poll
  #include <io.h>
  #include <windows.h>
  // windows.h leaks macros that collide with ftxui::Color factories
  // and elements.hpp enum values. Drop the ones we hit.
  #ifdef RGB
    #undef RGB
  #endif
  #ifdef DOUBLE
    #undef DOUBLE
  #endif
#endif

namespace {

// ── Alt-screen bookkeeping ────────────────────────────────
//
// On the very first TUI.BEGIN we swap to the alternate screen
// buffer (xterm sequence 1049h) so the script paints on a clean
// canvas without scrolling the user's shell history. atexit
// registers the inverse so the terminal is always restored, even
// when the script aborts.

// Console-input mode captured the first time we enter alt-screen,
// so atexit can put the terminal exactly back where it was before
// the script started fiddling with mouse / QuickEdit / line modes.
#ifdef _WIN32
static DWORD g_saved_in_mode = 0;
static bool g_saved_in_mode_valid = false;
#endif

void atexit_restore_terminal() {
    auto& s = jdb_tui::state();
    if (s.alt_screen_active) {
        std::cout << "\033[?25h"      // show cursor
                  << "\033[?1049l"    // leave alt screen
                  << std::flush;
        s.alt_screen_active = false;
    }
#ifdef _WIN32
    if (g_saved_in_mode_valid) {
        HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleMode(hin, g_saved_in_mode);
        g_saved_in_mode_valid = false;
    }
#endif
}

void enter_alt_screen_once() {
    auto& s = jdb_tui::state();
    if (s.alt_screen_active) return;
#ifdef _WIN32
    // Win Terminal honours ANSI; the legacy conhost needs VT mode
    // enabled. Toggle it idempotently — no-op on systems that
    // already had it on.
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    // Enable mouse + window events on stdin and DROP QuickEdit so
    // clicks don't trigger select-region instead of firing as
    // MOUSE_EVENTs. ENABLE_EXTENDED_FLAGS is required for the
    // QuickEdit toggle to actually stick.
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD in_mode = 0;
    if (GetConsoleMode(hin, &in_mode)) {
        if (!g_saved_in_mode_valid) {
            g_saved_in_mode = in_mode;
            g_saved_in_mode_valid = true;
        }
        DWORD desired = (in_mode | ENABLE_MOUSE_INPUT |
                                   ENABLE_WINDOW_INPUT |
                                   ENABLE_EXTENDED_FLAGS)
                        & ~ENABLE_QUICK_EDIT_MODE
                        & ~ENABLE_LINE_INPUT
                        & ~ENABLE_ECHO_INPUT
                        & ~ENABLE_PROCESSED_INPUT;
        SetConsoleMode(hin, desired);
    }
#endif
    std::cout << "\033[?1049h"   // enter alt screen
              << "\033[?25l"     // hide cursor (we don't blink one)
              << std::flush;
    s.alt_screen_active = true;
    static bool atexit_registered = false;
    if (!atexit_registered) {
        std::atexit(atexit_restore_terminal);
        atexit_registered = true;
    }
}

// ── Key polling ───────────────────────────────────────────
//
// Phase B only cares about Ctrl+Q (sets quit_requested) and
// raw printable bytes (stored in last_key for the script to
// observe via TUI.KEY$). Phase G fleshes out arrow/F-key
// decoding and mouse events.

#ifdef _WIN32
// Map Windows virtual-key codes for arrow / F-key / nav keys to the
// same string names the script-facing API expects.
static const char* vk_to_name(WORD vk) {
    switch (vk) {
        case VK_UP:     return "Up";
        case VK_DOWN:   return "Down";
        case VK_LEFT:   return "Left";
        case VK_RIGHT:  return "Right";
        case VK_HOME:   return "Home";
        case VK_END:    return "End";
        case VK_PRIOR:  return "PgUp";
        case VK_NEXT:   return "PgDn";
        case VK_DELETE: return "Delete";
        case VK_F1:  return "F1";  case VK_F2:  return "F2";
        case VK_F3:  return "F3";  case VK_F4:  return "F4";
        case VK_F5:  return "F5";  case VK_F6:  return "F6";
        case VK_F7:  return "F7";  case VK_F8:  return "F8";
        case VK_F9:  return "F9";  case VK_F10: return "F10";
        case VK_F11: return "F11"; case VK_F12: return "F12";
        default: return nullptr;
    }
}
#endif

// Drain one console-input event into TuiState. Returns true when a
// KEY event filled `out`; mouse / resize events update state in
// place and return false so the caller's poll loop keeps walking.
bool poll_one_event(std::string& out) {
#ifdef _WIN32
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD avail = 0;
    if (!GetNumberOfConsoleInputEvents(hin, &avail) || avail == 0)
        return false;
    INPUT_RECORD rec;
    DWORD nread = 0;
    if (!ReadConsoleInputW(hin, &rec, 1, &nread) || nread == 0)
        return false;

    auto& s = jdb_tui::state();
    switch (rec.EventType) {
        case KEY_EVENT: {
            const auto& ke = rec.Event.KeyEvent;
            if (!ke.bKeyDown) return false;
            const char* nm = vk_to_name(ke.wVirtualKeyCode);
            // Shift+Tab arrives as VK_TAB with shift held.
            if (ke.wVirtualKeyCode == VK_TAB) {
                if (ke.dwControlKeyState & SHIFT_PRESSED) { out = "S-Tab"; return true; }
                out = "Tab"; return true;
            }
            if (nm) { out = nm; return true; }
            wchar_t wc = ke.uChar.UnicodeChar;
            if (wc == 0) return false; // modifier-only
            if (wc == 17) { out = "C-q"; return true; }
            if (wc == 3)  { out = "C-c"; return true; }
            if (wc == 20) { out = "C-t"; return true; }
            if (wc == 13) { out = "Enter"; return true; }
            if (wc == 8 || wc == 127) { out = "Backspace"; return true; }
            if (wc == 27) { out = "Esc"; return true; }
            if (wc < 32) {
                // Other control chars — emit "C-<letter>" for the
                // common ASCII range so script keymaps stay readable.
                if (wc >= 1 && wc <= 26) {
                    char buf[4] = { 'C', '-', (char)('a' + (int)wc - 1), 0 };
                    out = buf; return true;
                }
                return false;
            }
            // Unicode characters: best-effort narrow round-trip.
            // jdBasic strings are byte-oriented; printables fit one
            // byte, multi-byte glyphs get UTF-8-encoded so INPUT can
            // still accumulate them.
            if (wc < 128) { out = std::string(1, (char)wc); return true; }
            char enc[8] = {0};
            int n = 0;
            if (wc < 0x800) {
                enc[n++] = (char)(0xC0 | (wc >> 6));
                enc[n++] = (char)(0x80 | (wc & 0x3F));
            } else {
                enc[n++] = (char)(0xE0 | (wc >> 12));
                enc[n++] = (char)(0x80 | ((wc >> 6) & 0x3F));
                enc[n++] = (char)(0x80 | (wc & 0x3F));
            }
            out.assign(enc, n);
            return true;
        }
        case MOUSE_EVENT: {
            const auto& me = rec.Event.MouseEvent;
            s.mouse_x = me.dwMousePosition.X;
            s.mouse_y = me.dwMousePosition.Y;
            // Bottom 5 bits of dwButtonState are the button-mask:
            //   bit 0 = left, 1 = right, 2 = middle, etc.
            s.mouse_buttons = (int)(me.dwButtonState & 0x1F);
            if (me.dwEventFlags & MOUSE_WHEELED) {
                SHORT delta = (SHORT)HIWORD(me.dwButtonState);
                s.mouse_wheel += (delta > 0 ? 1 : -1);
            }
            return false;
        }
        case WINDOW_BUFFER_SIZE_EVENT:
        default:
            return false;
    }
#else
    (void)out;
    return false; // Linux poll deferred — see project_linux_port.md
#endif
}

// Drain everything in the kbd buffer; updates state's last_key
// to the MOST RECENT key. Tab/Shift+Tab steer focus directly so
// widgets never see them. Everything else becomes pending_action
// for next frame's focused widget to consume.
void drain_keys(jdb_tui::TuiState& s) {
    std::string k;
    while (poll_one_event(k)) {
        s.last_key = k;
        if (k == "C-q") s.quit_requested = true;

        // F1..F10 open submenus when a menubar exists AND the index
        // points at an actual submenu. F-keys above the submenu count
        // fall through so scripts can bind them (tab switchers etc).
        if (k.size() >= 2 && k[0] == 'F' &&
            std::isdigit((unsigned char)k[1])) {
            int idx = std::atoi(k.c_str() + 1) - 1;
            if (idx >= 0 && idx < s.menu.submenus_seen_this_frame) {
                s.menu.open_index = (s.menu.open_index == idx) ? -1 : idx;
                s.menu.item_focus = 0;
                s.last_key.clear(); // don't let the script ALSO see this F-key
                continue;
            }
        }
        if (k == "Esc") {
            if (!s.modal.active_id.empty()) {
                s.modal.active_id.clear();
                continue;
            }
            if (s.menu.open_index != -1) {
                s.menu.open_index = -1;
                continue;
            }
        }
        // Within an open submenu: Up/Down navigates items.
        // Enter is left in pending_action AND mirrored to a menu flag
        // so the focused MENUITEM can claim it.
        if (s.menu.open_index != -1) {
            if (k == "Up") {
                if (s.menu.item_count_last_frame > 0)
                    s.menu.item_focus =
                        (s.menu.item_focus + s.menu.item_count_last_frame - 1)
                        % s.menu.item_count_last_frame;
                continue;
            }
            if (k == "Down") {
                if (s.menu.item_count_last_frame > 0)
                    s.menu.item_focus =
                        (s.menu.item_focus + 1) % s.menu.item_count_last_frame;
                continue;
            }
            if (k == "Enter") {
                s.menu.item_enter_pending = true;
                continue;
            }
        }

        if (k == "Tab") {
            if (s.widget_count_last_frame > 0)
                s.focus_index = (s.focus_index + 1) % s.widget_count_last_frame;
            continue;
        }
        if (k == "S-Tab") {
            if (s.widget_count_last_frame > 0)
                s.focus_index = (s.focus_index + s.widget_count_last_frame - 1)
                                 % s.widget_count_last_frame;
            continue;
        }
        s.pending_action = k;
    }
}

// ── Widget focus helpers ──────────────────────────────────
//
// alloc_widget_id() bumps the per-frame counter and returns the
// id this widget call gets. is_focused() compares against the
// (clamped) focus_index from last frame.

int alloc_widget_id() {
    return jdb_tui::state().widget_seen_this_frame++;
}

bool is_focused(int id) {
    auto& s = jdb_tui::state();
    if (s.widget_count_last_frame == 0) return id == 0; // first frame
    return id == (s.focus_index % s.widget_count_last_frame);
}

// Returns true (and clears) when pending_action matches `key`.
// Also clears last_key so scripts that consult TUI.KEY$() don't
// see a key the focused widget has already swallowed.
bool consume_action(const std::string& key) {
    auto& s = jdb_tui::state();
    if (s.pending_action == key) {
        s.pending_action.clear();
        if (s.last_key == key) s.last_key.clear();
        return true;
    }
    return false;
}

// Returns the pending action as a single-char string IF it's a
// printable ASCII character; clears the slot. Used by INPUT.
bool consume_printable(std::string& out) {
    auto& s = jdb_tui::state();
    if (s.pending_action.size() == 1) {
        unsigned char c = (unsigned char)s.pending_action[0];
        if (c >= 32 && c < 127) {
            out = s.pending_action;
            s.pending_action.clear();
            if (s.last_key == out) s.last_key.clear();
            return true;
        }
    }
    return false;
}

// ── Layout-stack helpers ──────────────────────────────────
//
// Every TUI.* widget appends to the top frame on the layout stack.
// Begin-style natives push a new frame; end-style natives pop the
// frame and fold its children into one Element which is then
// appended to the parent (the now-current top of stack).

// Apply the currently active fg/bg/style decorators (top of the
// stacks) to `el`. Called once for every widget that pushes itself
// onto a layout collector.
ftxui::Element apply_style(ftxui::Element el) {
    using namespace ftxui;
    auto& s = jdb_tui::state();
    if (s.style_stack.empty()) return el;
    auto& top = s.style_stack.back();
    if (top.has_fg) el = el | color(top.fg);
    if (top.has_bg) el = el | bgcolor(top.bg);
    int m = top.style_mask;
    if (m & 1)  el = el | bold;
    if (m & 2)  el = el | dim;
    if (m & 4)  el = el | italic;
    if (m & 8)  el = el | underlined;
    if (m & 16) el = el | inverted;
    return el;
}

void emit_element(ftxui::Element el) {
    using namespace ftxui;
    auto& s = jdb_tui::state();
    if (s.layout_stack.empty()) {
        // Outside a frame — silently drop. Stricter mode could warn.
        return;
    }
    el = apply_style(std::move(el));
    auto& kids = s.layout_stack.back().children;
    if (s.pending_same_line && !kids.empty()) {
        // SAME_LINE: retroactively join the previous element and the
        // new one into an hbox so the next pair sits on one row.
        auto prev = std::move(kids.back());
        kids.pop_back();
        kids.push_back(hbox({std::move(prev), std::move(el)}));
        s.pending_same_line = false;
    } else {
        kids.push_back(std::move(el));
    }
}

ftxui::Element fold_frame(jdb_tui::LayoutFrame frame) {
    using namespace ftxui;
    if (frame.discard) return text(""); // caller will drop it
    switch (frame.kind) {
        case jdb_tui::LayoutFrame::HBOX:
            return hbox(std::move(frame.children));
        case jdb_tui::LayoutFrame::VBOX:
        case jdb_tui::LayoutFrame::TAB:
        case jdb_tui::LayoutFrame::MODAL:
            return vbox(std::move(frame.children));
        case jdb_tui::LayoutFrame::BORDER:
            if (frame.title.empty())
                return border(vbox(std::move(frame.children)));
            return window(text(" " + frame.title + " "),
                          vbox(std::move(frame.children)));
        case jdb_tui::LayoutFrame::GRID: {
            int cols = frame.cols < 1 ? 1 : frame.cols;
            std::vector<std::vector<Element>> lines;
            std::vector<Element> row;
            for (auto& c : frame.children) {
                row.push_back(std::move(c));
                if ((int)row.size() == cols) {
                    lines.push_back(std::move(row));
                    row.clear();
                }
            }
            if (!row.empty()) {
                while ((int)row.size() < cols) row.push_back(filler());
                lines.push_back(std::move(row));
            }
            if (lines.empty()) lines.push_back({filler()});
            return gridbox(std::move(lines));
        }
        case jdb_tui::LayoutFrame::ROOT:
        default:
            return vbox(std::move(frame.children));
    }
}

void pop_and_attach(jdb_tui::LayoutFrame::Kind expected, VM& vm, const char* who) {
    auto& s = jdb_tui::state();
    if (s.layout_stack.size() <= 1) {
        vm.emit(std::string("[TUI] ") + who + " without matching begin\n");
        return;
    }
    if (s.layout_stack.back().kind != expected) {
        vm.emit(std::string("[TUI] ") + who + " — layout stack mismatch\n");
    }
    auto frame = std::move(s.layout_stack.back());
    s.layout_stack.pop_back();
    if (frame.discard) return;
    emit_element(fold_frame(std::move(frame)));
}

void push_layout(jdb_tui::LayoutFrame::Kind kind,
                 std::string title = std::string(),
                 int cols = 1) {
    auto& s = jdb_tui::state();
    jdb_tui::LayoutFrame f;
    f.kind = kind;
    f.title = std::move(title);
    f.cols = cols;
    s.layout_stack.push_back(std::move(f));
}

// Theme name → primary tint. Used in the title bar, the active
// tab, selected list rows, focused widget overlays, and the
// filled portion of TUI.SLIDER / PROGRESS / GAUGE. Add entries
// here when shipping a new theme.
ftxui::Color theme_tint(const std::string& name) {
    using namespace ftxui;
    if (name == "warm")  return Color::Orange1;
    if (name == "neon")  return Color::GreenLight;
    if (name == "cyber") return Color::Magenta;
    if (name == "gold")  return Color::Yellow1;
    return Color::Cyan3; // default = "cool"
}

// A darker counterpart used as a contrasting backdrop where we
// want the theme to read as a "selection band" rather than text.
ftxui::Color theme_band(const std::string& name) {
    using namespace ftxui;
    if (name == "warm")  return Color::Red;
    if (name == "neon")  return Color::Green;
    if (name == "cyber") return Color::Purple;
    if (name == "gold")  return Color::Yellow;
    return Color::Blue;
}

ftxui::Color active_theme_tint() { return theme_tint(jdb_tui::state().theme); }
ftxui::Color active_theme_band() { return theme_band(jdb_tui::state().theme); }

void render_once(jdb_tui::TuiState& s) {
    using namespace ftxui;
    using namespace std::chrono;
    auto t0 = steady_clock::now();
    enter_alt_screen_once();
    // Drain the root frame's children into the rendered doc, then
    // restore an empty root collector so the NEXT TUI.BEGIN sees
    // a fresh state without needing extra bookkeeping.
    Element body;
    if (s.layout_stack.empty()) {
        body = text("");
    } else {
        body = vbox(std::move(s.layout_stack.front().children));
        s.layout_stack.front().children.clear();
    }
    Element doc;
    if (s.frame_title.empty()) {
        doc = body;
    } else {
        Element title_el = text(" " + s.frame_title + " ")
                          | color(theme_tint(s.theme)) | bold;
        doc = window(title_el, body);
    }
    if (s.modal.captured_body) {
        // Centre the modal over the main doc using dbox + center().
        Element overlay = center(s.modal.captured_body);
        doc = dbox({doc, overlay});
        s.modal.captured_body.reset();
    }
    auto screen = Screen::Create(Dimension::Full(), Dimension::Full());
    Render(screen, doc);
    std::cout << "\033[H";  // home; alt buffer is already clear underneath
    screen.Print();
    std::cout.flush();
    auto t1 = steady_clock::now();
    s.last_render_ms = duration<double, std::milli>(t1 - t0).count();
}

} // namespace

void register_tui_natives(VM& vm) {
    using V = const std::vector<Value>&;

    // Macro for the still-pending stubs (Phases C-H).
    #define TUI_STUB(NAME, MIN, MAX) \
        vm.register_native(NAME, MIN, MAX, [&vm](V) -> Value { \
            static thread_local bool warned = false; \
            if (!warned) { \
                vm.emit(std::string("[TUI] ") + NAME + " not yet implemented\n"); \
                warned = true; \
            } \
            return Value::make_none(); \
        });

    // ── Core loop (Phase B — real impl) ─────────────────────
    vm.register_native("TUI.BEGIN", 0, 1, [&vm](V args) -> Value {
        auto& s = jdb_tui::state();
        if (s.in_frame) {
            vm.emit("[TUI] TUI.BEGIN already active — missing TUI.END?\n");
            return Value::make_none();
        }
        s.in_frame = true;
        s.frame_title = args.empty() ? std::string() : args[0].as_string()->data;
        s.layout_stack.clear();
        jdb_tui::LayoutFrame root;
        root.kind = jdb_tui::LayoutFrame::ROOT;
        s.layout_stack.push_back(std::move(root));
        s.pending_same_line = false;
        s.widget_seen_this_frame = 0;
        s.menu.bar_active = false;
        s.menu.submenus_seen_this_frame = 0;
        s.menu.items_seen_in_open = 0;
        s.menu.bar_row.clear();
        s.menu.popup_rows.clear();
        s.menu.current_submenu_idx = -1;
        return Value::make_none();
    });

    vm.register_native("TUI.END", 0, 0, [&vm](V) -> Value {
        auto& s = jdb_tui::state();
        if (!s.in_frame) {
            vm.emit("[TUI] TUI.END without TUI.BEGIN\n");
            return Value::make_none();
        }
        if (s.layout_stack.size() > 1) {
            vm.emit("[TUI] TUI.END — " + std::to_string(s.layout_stack.size() - 1)
                    + " layout frame(s) left open; auto-closing\n");
            while (s.layout_stack.size() > 1) {
                auto frame = std::move(s.layout_stack.back());
                s.layout_stack.pop_back();
                emit_element(fold_frame(std::move(frame)));
            }
        }
        s.in_frame = false;
        s.widget_count_last_frame = s.widget_seen_this_frame;
        return Value::make_none();
    });

    vm.register_native("TUI.RENDER", 0, 0, [&vm](V) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        if (s.host_screen != nullptr) {
            // REPL is driving the terminal; TUI.* embedding deferred.
            // Phase B contract: refuse politely instead of fighting it.
            static bool warned = false;
            if (!warned) {
                vm.emit("[TUI] TUI.RENDER not yet supported from inside the FTXUI REPL — run the script standalone.\n");
                warned = true;
            }
            return Value::make_none();
        }
        render_once(s);
        drain_keys(s);
        return Value::make_none();
    });

    vm.register_native("TUI.WAIT_EVENT", 0, 1, [&vm](V args) -> Value {
        (void)args;
        auto& s = jdb_tui::state();
        // Spin-poll until a key arrives. Phase G will replace this
        // with a proper blocking read or FTXUI task-receiver path.
        std::string k;
        while (!poll_one_event(k)) {
#ifdef _WIN32
            Sleep(8);
#endif
        }
        s.last_key = k;
        if (k == "C-q") s.quit_requested = true;
        return Value::make_string(k);
    });

    vm.register_native("TUI.QUIT", 0, 0, [&vm](V) -> Value {
        (void)vm;
        return Value::make_bool(jdb_tui::state().quit_requested);
    });

    vm.register_native("TUI.EXIT", 0, 0, [&vm](V) -> Value {
        (void)vm;
        jdb_tui::state().quit_requested = true;
        return Value::make_none();
    });

    // ── TUI.TEXT — push a text Element into the current frame ─
    vm.register_native("TUI.TEXT", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        // args[1] (optional style hint) parsed in Phase G.
        emit_element(text(args[0].as_string()->data));
        return Value::make_none();
    });

    // ── Diagnostics — VERSION$ lands now because it's free ──
    vm.register_native("TUI.VERSION$", 0, 0, [&vm](V) -> Value {
        (void)vm;
        return Value::make_string("TUI.* / FTXUI bridge (Phase G)");
    });

    TUI_STUB("TUI.RENDER_HEADLESS$", 0, 0)

    // ── Layout primitives (Phase C) ─────────────────────────
    vm.register_native("TUI.HBOX_BEGIN", 0, 0, [&vm](V) -> Value {
        (void)vm; push_layout(jdb_tui::LayoutFrame::HBOX); return Value::make_none();
    });
    vm.register_native("TUI.HBOX_END", 0, 0, [&vm](V) -> Value {
        pop_and_attach(jdb_tui::LayoutFrame::HBOX, vm, "TUI.HBOX_END");
        return Value::make_none();
    });
    vm.register_native("TUI.VBOX_BEGIN", 0, 0, [&vm](V) -> Value {
        (void)vm; push_layout(jdb_tui::LayoutFrame::VBOX); return Value::make_none();
    });
    vm.register_native("TUI.VBOX_END", 0, 0, [&vm](V) -> Value {
        pop_and_attach(jdb_tui::LayoutFrame::VBOX, vm, "TUI.VBOX_END");
        return Value::make_none();
    });
    vm.register_native("TUI.GRID_BEGIN", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        int cols = (int)args[0].to_int();
        push_layout(jdb_tui::LayoutFrame::GRID, std::string(), cols);
        return Value::make_none();
    });
    vm.register_native("TUI.GRID_END", 0, 0, [&vm](V) -> Value {
        pop_and_attach(jdb_tui::LayoutFrame::GRID, vm, "TUI.GRID_END");
        return Value::make_none();
    });
    vm.register_native("TUI.BORDER_BEGIN", 0, 1, [&vm](V args) -> Value {
        (void)vm;
        std::string title = args.empty() ? std::string() : args[0].as_string()->data;
        push_layout(jdb_tui::LayoutFrame::BORDER, std::move(title));
        return Value::make_none();
    });
    vm.register_native("TUI.BORDER_END", 0, 0, [&vm](V) -> Value {
        pop_and_attach(jdb_tui::LayoutFrame::BORDER, vm, "TUI.BORDER_END");
        return Value::make_none();
    });
    vm.register_native("TUI.SEPARATOR", 0, 0, [&vm](V) -> Value {
        (void)vm;
        emit_element(ftxui::separator());
        return Value::make_none();
    });
    vm.register_native("TUI.SEPARATOR_TEXT", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        // No native separator-with-label in FTXUI; build it from
        // hbox(separator | flex, " label ", separator | flex).
        std::string lbl = " " + args[0].as_string()->data + " ";
        emit_element(hbox({
            separator() | flex,
            text(lbl),
            separator() | flex
        }));
        return Value::make_none();
    });
    vm.register_native("TUI.SPACER", 0, 0, [&vm](V) -> Value {
        (void)vm;
        emit_element(ftxui::filler());
        return Value::make_none();
    });
    vm.register_native("TUI.SIZE", 2, 2, [&vm](V args) -> Value {
        // Retroactively decorate the last-emitted element with
        // fixed width/height. -1 means "no constraint".
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (s.layout_stack.empty() || s.layout_stack.back().children.empty()) {
            vm.emit("[TUI] TUI.SIZE called with no preceding element\n");
            return Value::make_none();
        }
        int w = (int)args[0].to_int();
        int h = (int)args[1].to_int();
        auto& el = s.layout_stack.back().children.back();
        if (w > 0) el = el | size(WIDTH,  EQUAL, w);
        if (h > 0) el = el | size(HEIGHT, EQUAL, h);
        return Value::make_none();
    });
    vm.register_native("TUI.SAME_LINE", 0, 0, [&vm](V) -> Value {
        (void)vm;
        jdb_tui::state().pending_same_line = true;
        return Value::make_none();
    });

    // ── Text / headings / links (Phase E) ───────────────────
    vm.register_native("TUI.PARAGRAPH", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        emit_element(ftxui::paragraph(args[0].as_string()->data));
        return Value::make_none();
    });
    vm.register_native("TUI.HEADING", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int level = (args.size() >= 2) ? (int)args[1].to_int() : 1;
        Element el = text(args[0].as_string()->data);
        // Level 1 = bold+underline, level 2 = bold, level >=3 = plain bold.
        if (level <= 1)      el = el | bold | underlined;
        else if (level == 2) el = el | bold;
        else                 el = el | dim;
        emit_element(el);
        return Value::make_none();
    });
    vm.register_native("TUI.LINK", 2, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        // OSC 8 hyperlink — Win Terminal + gnome-terminal honour it.
        emit_element(hyperlink(args[1].as_string()->data,
                               text(args[0].as_string()->data) | underlined));
        return Value::make_none();
    });

    // ── Input widgets (Phase D) ─────────────────────────────
    //
    // Contract: value-in / value-out (same shape as GUI.SLIDER).
    // Each widget call returns the (possibly mutated) state; the
    // script reassigns. Activation happens when the widget is the
    // focused one AND the user hit Enter (or Space for checkbox).
    // Focus advances via Tab / Shift+Tab and is steered in
    // drain_keys before the next frame begins.

    // TUI.BUTTON(label$, [hint$]) -> bool
    vm.register_native("TUI.BUTTON", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        bool clicked = focused && consume_action("Enter");
        std::string label = args[0].as_string()->data;
        std::string rendered = " " + label + " ";
        Element el = text(rendered);
        if (focused) el = el | inverted;
        else         el = el | bold;
        emit_element(el);
        return Value::make_bool(clicked);
    });

    // TUI.CHECKBOX(label$, checked) -> int (0/1)
    vm.register_native("TUI.CHECKBOX", 2, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        bool checked = args[1].to_bool();
        if (focused && (consume_action("Enter") || consume_action(" "))) {
            checked = !checked;
        }
        std::string mark = checked ? "[x] " : "[ ] ";
        Element el = text(mark + args[0].as_string()->data);
        if (focused) el = el | inverted;
        emit_element(el);
        return Value::make_i64(checked ? 1 : 0);
    });

    // TUI.RADIO(label$, options[], selected) -> int
    vm.register_native("TUI.RADIO", 3, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        auto* arr = args[1].as_array();
        int n = arr ? (int)arr->elements.size() : 0;
        int sel = (int)args[2].to_int();
        if (focused && n > 0) {
            if (consume_action("Up"))   sel = (sel + n - 1) % n;
            if (consume_action("Down")) sel = (sel + 1) % n;
        }
        std::vector<Element> rows;
        rows.push_back(text(args[0].as_string()->data) | bold);
        ftxui::Color tint = active_theme_tint();
        for (int i = 0; i < n; ++i) {
            std::string s = std::string(i == sel ? "(*) " : "( ) ")
                          + arr->elements[i].as_string()->data;
            Element row = text(s);
            if (i == sel)              row = row | color(tint) | bold;
            if (focused && i == sel)   row = row | inverted;
            rows.push_back(row);
        }
        emit_element(vbox(std::move(rows)));
        return Value::make_i64(sel);
    });

    // TUI.INPUT(label$, text$, [width]) -> string
    vm.register_native("TUI.INPUT", 2, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        std::string buf = args[1].as_string()->data;
        if (focused) {
            std::string c;
            while (consume_printable(c)) buf += c;
            if (consume_action("Backspace")) {
                if (!buf.empty()) buf.pop_back();
            }
        }
        Element label_el = text(args[0].as_string()->data + ": ");
        Element body_el  = text(buf + (focused ? "_" : " "));
        if (focused) body_el = body_el | inverted;
        emit_element(hbox({label_el, body_el}));
        return Value::make_string(buf);
    });

    // TUI.INPUT_INT(label$, value, [width]) -> int
    vm.register_native("TUI.INPUT_INT", 2, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        int64_t v = args[1].to_int();
        if (focused) {
            std::string c;
            while (consume_printable(c)) {
                if (c.size() == 1 && c[0] >= '0' && c[0] <= '9') {
                    v = v * 10 + (c[0] - '0');
                }
            }
            if (consume_action("Backspace")) v /= 10;
            if (consume_action("Up"))   v += 1;
            if (consume_action("Down")) v -= 1;
        }
        Element label_el = text(args[0].as_string()->data + ": ");
        Element body_el  = text(std::to_string(v) + (focused ? "_" : " "));
        if (focused) body_el = body_el | inverted;
        emit_element(hbox({label_el, body_el}));
        return Value::make_i64(v);
    });

    // TUI.INPUT_DOUBLE(label$, value, [width]) -> double
    // Phase D keeps editing arrow-based; full free-text float
    // parsing arrives in Phase G with a real edit-buffer.
    vm.register_native("TUI.INPUT_DOUBLE", 2, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        double v = args[1].to_double();
        if (focused) {
            if (consume_action("Up"))   v += 1.0;
            if (consume_action("Down")) v -= 1.0;
            if (consume_action("Right")) v += 0.1;
            if (consume_action("Left"))  v -= 0.1;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        Element label_el = text(args[0].as_string()->data + ": ");
        Element body_el  = text(std::string(buf) + (focused ? " ◀▶" : "   "));
        if (focused) body_el = body_el | inverted;
        emit_element(hbox({label_el, body_el}));
        return Value::make_f64(v);
    });

    // TUI.SLIDER(label$, value, min, max, [step]) -> double
    vm.register_native("TUI.SLIDER", 4, 5, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        double v  = args[1].to_double();
        double mn = args[2].to_double();
        double mx = args[3].to_double();
        double step = (args.size() >= 5 && args[4].type != ValueType::NONE)
                      ? args[4].to_double()
                      : (mx > mn ? (mx - mn) / 20.0 : 1.0);
        if (focused) {
            if (consume_action("Left"))  v -= step;
            if (consume_action("Right")) v += step;
            if (v < mn) v = mn;
            if (v > mx) v = mx;
        }
        double frac = (mx > mn) ? (v - mn) / (mx - mn) : 0.0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        const int bar_w = 20;
        int filled = (int)(frac * bar_w + 0.5);
        char val_s[32];
        std::snprintf(val_s, sizeof(val_s), " %.3f", v);
        Element row = hbox({
            text(args[0].as_string()->data + " "),
            text("["),
            text(std::string(filled, '=')) | color(active_theme_tint()) | bold,
            text(std::string(bar_w - filled, ' ')),
            text("]"),
            text(val_s)
        });
        if (focused) row = row | inverted;
        emit_element(row);
        return Value::make_f64(v);
    });

    // TUI.MENU(label$, options[], selected) -> int
    // Functionally identical to RADIO but rendered without the
    // (*) / ( ) glyphs — looks more like a side-bar menu.
    vm.register_native("TUI.MENU", 2, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        auto* arr = args[1].as_array();
        int n = arr ? (int)arr->elements.size() : 0;
        int sel = (args.size() >= 3) ? (int)args[2].to_int() : 0;
        if (focused && n > 0) {
            if (consume_action("Up"))   sel = (sel + n - 1) % n;
            if (consume_action("Down")) sel = (sel + 1) % n;
        }
        std::vector<Element> rows;
        rows.push_back(text(args[0].as_string()->data) | bold);
        ftxui::Color tint = active_theme_tint();
        for (int i = 0; i < n; ++i) {
            Element row = text("  " + arr->elements[i].as_string()->data);
            if (i == sel) {
                row = row | color(tint) | bold;
                if (focused) row = row | inverted;
            }
            rows.push_back(row);
        }
        emit_element(vbox(std::move(rows)));
        return Value::make_i64(sel);
    });

    // TUI.DROPDOWN(label$, options[], selected) -> int
    vm.register_native("TUI.DROPDOWN", 3, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        auto* arr = args[1].as_array();
        int n = arr ? (int)arr->elements.size() : 0;
        int sel = (int)args[2].to_int();
        if (focused && n > 0) {
            if (consume_action("Left"))  sel = (sel + n - 1) % n;
            if (consume_action("Right")) sel = (sel + 1) % n;
        }
        std::string txt = (n > 0)
            ? (arr->elements[sel].as_string()->data + "  v")
            : std::string("(empty)");
        Element row = hbox({
            text(args[0].as_string()->data + ": "),
            text(txt) | color(active_theme_tint()) | bold
        });
        if (focused) row = row | inverted;
        emit_element(row);
        return Value::make_i64(sel);
    });

    // TUI.SELECTABLE(label$, selected, [hint$]) -> bool
    // Like a button styled as a row in a list. Returns true on Enter.
    vm.register_native("TUI.SELECTABLE", 2, 3, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        int id = alloc_widget_id();
        bool focused = is_focused(id);
        bool selected = args[1].to_bool();
        bool clicked = focused && consume_action("Enter");
        Element el = text("  " + args[0].as_string()->data);
        if (focused)       el = el | inverted;
        else if (selected) el = el | bold;
        emit_element(el);
        return Value::make_bool(clicked);
    });

    // ── Display widgets (Phase E) ───────────────────────────
    // TUI.PROGRESS(frac, [label$])  bar with optional inline label
    vm.register_native("TUI.PROGRESS", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        float f = (float)args[0].to_double();
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        Element bar = gauge(f) | color(active_theme_tint()) | flex;
        if (args.size() >= 2 && args[1].type == ValueType::STRING) {
            emit_element(hbox({bar, text(" "), text(args[1].as_string()->data)}));
        } else {
            emit_element(bar);
        }
        return Value::make_none();
    });

    // TUI.GAUGE(frac, [label$])  same bar but wrapped in a border
    vm.register_native("TUI.GAUGE", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        float f = (float)args[0].to_double();
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        Element bar = gauge(f) | color(active_theme_tint());
        if (args.size() >= 2 && args[1].type == ValueType::STRING) {
            emit_element(window(text(" " + args[1].as_string()->data + " "), bar));
        } else {
            emit_element(border(bar));
        }
        return Value::make_none();
    });

    // TUI.SPINNER(frame, [variant])  variant 0-21, frame anims it
    vm.register_native("TUI.SPINNER", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        int frame   = (int)args[0].to_int();
        int variant = (args.size() >= 2) ? (int)args[1].to_int() : 7;
        if (variant < 0) variant = 0;
        if (variant > 21) variant = 21;
        if (frame   < 0) frame   = 0;
        emit_element(ftxui::spinner(variant, (size_t)frame));
        return Value::make_none();
    });

    // ── Canvas (Phase E) ────────────────────────────────────
    //
    // BEGIN seeds the ops buffer. LINE/PIXEL append closures that
    // know how to draw themselves. END constructs ONE canvas()
    // Element with a capture-by-move lambda that replays the ops
    // against the FTXUI Canvas at render time.
    vm.register_native("TUI.CANVAS_BEGIN", 2, 2, [&vm](V args) -> Value {
        auto& s = jdb_tui::state();
        if (s.canvas.active) {
            vm.emit("[TUI] TUI.CANVAS_BEGIN already active — missing CANVAS_END?\n");
            return Value::make_none();
        }
        s.canvas.active = true;
        s.canvas.w = (int)args[0].to_int();
        s.canvas.h = (int)args[1].to_int();
        s.canvas.ops.clear();
        return Value::make_none();
    });
    vm.register_native("TUI.CANVAS_END", 0, 0, [&vm](V) -> Value {
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (!s.canvas.active) {
            vm.emit("[TUI] TUI.CANVAS_END without TUI.CANVAS_BEGIN\n");
            return Value::make_none();
        }
        int w = s.canvas.w, h = s.canvas.h;
        auto ops = std::move(s.canvas.ops);
        s.canvas.active = false;
        s.canvas.ops.clear();
        emit_element(canvas(w, h, [ops](Canvas& c) {
            for (auto& op : ops) op(c);
        }));
        return Value::make_none();
    });
    // Map small int → Color::Palette16 entry.
    auto pick_color = [](int i) -> ftxui::Color {
        if (i < 0) i = 0;
        if (i > 15) i = 15;
        return ftxui::Color(static_cast<ftxui::Color::Palette16>(i));
    };
    vm.register_native("TUI.LINE", 5, 5, [&vm, pick_color](V args) -> Value {
        auto& s = jdb_tui::state();
        if (!s.canvas.active) {
            vm.emit("[TUI] TUI.LINE called outside TUI.CANVAS_BEGIN/END\n");
            return Value::make_none();
        }
        int x1 = (int)args[0].to_int();
        int y1 = (int)args[1].to_int();
        int x2 = (int)args[2].to_int();
        int y2 = (int)args[3].to_int();
        ftxui::Color col = pick_color((int)args[4].to_int());
        s.canvas.ops.push_back([x1,y1,x2,y2,col](ftxui::Canvas& c){
            c.DrawPointLine(x1, y1, x2, y2, col);
        });
        return Value::make_none();
    });
    vm.register_native("TUI.PIXEL", 3, 3, [&vm, pick_color](V args) -> Value {
        auto& s = jdb_tui::state();
        if (!s.canvas.active) {
            vm.emit("[TUI] TUI.PIXEL called outside TUI.CANVAS_BEGIN/END\n");
            return Value::make_none();
        }
        int x = (int)args[0].to_int();
        int y = (int)args[1].to_int();
        ftxui::Color col = pick_color((int)args[2].to_int());
        s.canvas.ops.push_back([x,y,col](ftxui::Canvas& c){
            c.DrawPoint(x, y, true, col);
        });
        return Value::make_none();
    });

    // ── Tables (Phase E) ────────────────────────────────────
    vm.register_native("TUI.TABLE_BEGIN", 1, 1, [&vm](V args) -> Value {
        auto& s = jdb_tui::state();
        if (s.table.active) {
            vm.emit("[TUI] TUI.TABLE_BEGIN already active — missing TABLE_END?\n");
            return Value::make_none();
        }
        s.table.active = true;
        s.table.rows.clear();
        std::vector<std::string> header;
        if (auto* arr = args[0].as_array()) {
            for (auto& v : arr->elements) header.push_back(v.as_string()->data);
        }
        s.table.rows.push_back(std::move(header));
        return Value::make_none();
    });
    vm.register_native("TUI.TABLE_ROW", 1, 1, [&vm](V args) -> Value {
        auto& s = jdb_tui::state();
        if (!s.table.active) {
            vm.emit("[TUI] TUI.TABLE_ROW outside TUI.TABLE_BEGIN/END\n");
            return Value::make_none();
        }
        std::vector<std::string> row;
        if (auto* arr = args[0].as_array()) {
            for (auto& v : arr->elements) {
                // Cells coerce to string — numbers print via to_string.
                if (v.type == ValueType::STRING) row.push_back(v.as_string()->data);
                else                              row.push_back(std::to_string(v.to_double()));
            }
        }
        s.table.rows.push_back(std::move(row));
        return Value::make_none();
    });
    vm.register_native("TUI.TABLE_END", 0, 0, [&vm](V) -> Value {
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (!s.table.active) {
            vm.emit("[TUI] TUI.TABLE_END without TUI.TABLE_BEGIN\n");
            return Value::make_none();
        }
        // FTXUI Table wants rectangular data — pad short rows.
        size_t ncols = 0;
        for (auto& r : s.table.rows) if (r.size() > ncols) ncols = r.size();
        for (auto& r : s.table.rows) while (r.size() < ncols) r.push_back("");
        Table tbl(std::move(s.table.rows));
        s.table.active = false;
        s.table.rows.clear();
        tbl.SelectAll().Border(ftxui::LIGHT);
        tbl.SelectRow(0).Decorate(bold);
        tbl.SelectRow(0).SeparatorVertical(ftxui::LIGHT);
        tbl.SelectRow(0).Border(ftxui::DOUBLE);
        emit_element(tbl.Render());
        return Value::make_none();
    });

    // ── Modal (Phase F) ─────────────────────────────────────
    vm.register_native("TUI.MODAL_OPEN", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        jdb_tui::state().modal.active_id = args[0].as_string()->data;
        return Value::make_none();
    });
    vm.register_native("TUI.MODAL_BEGIN", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        auto& s = jdb_tui::state();
        std::string id = args[0].as_string()->data;
        std::string title = (args.size() >= 2) ? args[1].as_string()->data : std::string();
        bool active = (s.modal.active_id == id);
        // Only push a frame when active — the jdBasic idiom is
        // IF TUI.MODAL_BEGIN(...) THEN ... TUI.MODAL_END ENDIF, so
        // MODAL_END is not called on the inactive branch.
        if (active) {
            jdb_tui::LayoutFrame f;
            f.kind = jdb_tui::LayoutFrame::MODAL;
            f.title = title;
            s.layout_stack.push_back(std::move(f));
        }
        return Value::make_bool(active);
    });
    vm.register_native("TUI.MODAL_END", 0, 0, [&vm](V) -> Value {
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (s.layout_stack.size() <= 1 ||
            s.layout_stack.back().kind != jdb_tui::LayoutFrame::MODAL) {
            vm.emit("[TUI] TUI.MODAL_END without matching MODAL_BEGIN\n");
            return Value::make_none();
        }
        auto frame = std::move(s.layout_stack.back());
        s.layout_stack.pop_back();
        Element body = vbox(std::move(frame.children));
        Element framed = frame.title.empty()
            ? (body | border)
            : window(text(" " + frame.title + " "), body);
        // Don't attach to parent — render_once overlays it.
        s.modal.captured_body = framed;
        return Value::make_none();
    });
    vm.register_native("TUI.MODAL_CLOSE", 0, 0, [&vm](V) -> Value {
        (void)vm;
        jdb_tui::state().modal.active_id.clear();
        return Value::make_none();
    });

    // ── Menu bar (Phase F) ──────────────────────────────────
    vm.register_native("TUI.MENUBAR_BEGIN", 0, 0, [&vm](V) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        s.menu.bar_active = true;
        s.menu.submenus_seen_this_frame = 0;
        s.menu.items_seen_in_open = 0;
        s.menu.bar_row.clear();
        s.menu.popup_rows.clear();
        return Value::make_none();
    });
    vm.register_native("TUI.MENUBAR_END", 0, 0, [&vm](V) -> Value {
        (void)vm;
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (!s.menu.bar_active) {
            vm.emit("[TUI] TUI.MENUBAR_END without MENUBAR_BEGIN\n");
            return Value::make_none();
        }
        // Update item-count clamp from the open submenu's traversal.
        s.menu.item_count_last_frame = s.menu.items_seen_in_open;
        // Build the menubar row (with separators between entries).
        std::vector<Element> row;
        for (size_t i = 0; i < s.menu.bar_row.size(); ++i) {
            if (i) row.push_back(text(" "));
            row.push_back(s.menu.bar_row[i]);
        }
        row.push_back(filler());
        if (s.menu.open_index >= 0)
            row.push_back(text(" (Esc to close)") | dim);
        Element bar = hbox(std::move(row));
        if (!s.menu.popup_rows.empty()) {
            Element popup = vbox(std::move(s.menu.popup_rows)) | border;
            emit_element(vbox({bar, hbox({popup, filler()})}));
        } else {
            emit_element(bar);
        }
        s.menu.bar_active = false;
        s.menu.bar_row.clear();
        s.menu.popup_rows.clear();
        // Reset Enter-pending so it doesn't fire next frame too.
        s.menu.item_enter_pending = false;
        return Value::make_none();
    });
    vm.register_native("TUI.SUBMENU_BEGIN", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (!s.menu.bar_active) {
            vm.emit("[TUI] TUI.SUBMENU_BEGIN outside MENUBAR\n");
            return Value::make_bool(false);
        }
        int idx = s.menu.submenus_seen_this_frame++;
        s.menu.current_submenu_idx = idx;
        bool is_open = (idx == s.menu.open_index);
        std::string label = args[0].as_string()->data;
        char fkey[8];
        std::snprintf(fkey, sizeof(fkey), "F%d", idx + 1);
        Element lbl = text(label + " (" + fkey + ")");
        if (is_open) lbl = lbl | inverted;
        else         lbl = lbl | bold;
        s.menu.bar_row.push_back(lbl);
        // Reset item counter on entering the open submenu.
        if (is_open) s.menu.items_seen_in_open = 0;
        return Value::make_bool(is_open);
    });
    vm.register_native("TUI.SUBMENU_END", 0, 0, [&vm](V) -> Value {
        (void)vm;
        jdb_tui::state().menu.current_submenu_idx = -1;
        return Value::make_none();
    });
    vm.register_native("TUI.MENUITEM", 1, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        auto& s = jdb_tui::state();
        if (s.menu.current_submenu_idx != s.menu.open_index) {
            // Item belongs to a non-open submenu — render nothing, never fires.
            return Value::make_bool(false);
        }
        int row_idx = s.menu.items_seen_in_open++;
        bool focused = (row_idx == s.menu.item_focus);
        bool clicked = focused && s.menu.item_enter_pending;
        if (clicked) {
            s.menu.item_enter_pending = false;
            s.menu.open_index = -1; // selecting an item closes the menu
        }
        std::string label = args[0].as_string()->data;
        std::string hint  = (args.size() >= 2) ? args[1].as_string()->data : std::string();
        std::string text_s = "  " + label;
        if (!hint.empty()) text_s += "   " + hint;
        Element el = text(text_s);
        if (focused) el = el | inverted;
        s.menu.popup_rows.push_back(el);
        return Value::make_bool(clicked);
    });

    // ── Tab bar (Phase F) ───────────────────────────────────
    vm.register_native("TUI.TAB_BAR_BEGIN", 2, 2, [&vm](V args) -> Value {
        (void)vm;
        using namespace ftxui;
        auto& s = jdb_tui::state();
        s.tab_bar.active = true;
        s.tab_bar.labels.clear();
        if (auto* arr = args[0].as_array()) {
            for (auto& v : arr->elements)
                s.tab_bar.labels.push_back(v.as_string()->data);
        }
        s.tab_bar.active_idx = (int)args[1].to_int();
        if (s.tab_bar.active_idx < 0) s.tab_bar.active_idx = 0;
        if (!s.tab_bar.labels.empty() &&
            s.tab_bar.active_idx >= (int)s.tab_bar.labels.size())
            s.tab_bar.active_idx = (int)s.tab_bar.labels.size() - 1;
        // Render the tab strip immediately. The active tab is
        // painted with the theme's band colour so the strip moves
        // with TUI.THEME — same logic as the title-bar tint.
        Color band = active_theme_band();
        Color tint = active_theme_tint();
        std::vector<Element> tabs;
        for (size_t i = 0; i < s.tab_bar.labels.size(); ++i) {
            std::string lbl = " " + s.tab_bar.labels[i] + " ";
            Element e = text(lbl);
            if ((int)i == s.tab_bar.active_idx)
                e = e | bgcolor(band) | color(Color::White) | bold;
            else
                e = e | color(tint) | dim;
            tabs.push_back(e);
            tabs.push_back(text(" "));
        }
        tabs.push_back(filler());
        emit_element(hbox(std::move(tabs)) | bgcolor(Color::GrayDark));
        return Value::make_none();
    });
    vm.register_native("TUI.TAB_BAR_END", 0, 0, [&vm](V) -> Value {
        (void)vm;
        jdb_tui::state().tab_bar.active = false;
        return Value::make_none();
    });
    vm.register_native("TUI.TAB_BEGIN", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        std::string label = args[0].as_string()->data;
        bool is_active = (!s.tab_bar.labels.empty()
                          && s.tab_bar.active_idx >= 0
                          && s.tab_bar.active_idx < (int)s.tab_bar.labels.size()
                          && s.tab_bar.labels[s.tab_bar.active_idx] == label);
        // Only push when active — IF-gated idiom skips TAB_END otherwise.
        if (is_active) {
            jdb_tui::LayoutFrame f;
            f.kind = jdb_tui::LayoutFrame::TAB;
            s.layout_stack.push_back(std::move(f));
        }
        return Value::make_bool(is_active);
    });
    vm.register_native("TUI.TAB_END", 0, 0, [&vm](V) -> Value {
        pop_and_attach(jdb_tui::LayoutFrame::TAB, vm, "TUI.TAB_END");
        return Value::make_none();
    });

    // ── Colour + theme + style (Phase G) ────────────────────
    vm.register_native("TUI.COLOR", 3, 3, [&vm](V args) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        jdb_tui::TuiState::StyleEntry e;
        if (!s.style_stack.empty()) e = s.style_stack.back();
        e.fg = ftxui::Color::RGB(
            (uint8_t)args[0].to_int(),
            (uint8_t)args[1].to_int(),
            (uint8_t)args[2].to_int());
        e.has_fg = true;
        s.style_stack.push_back(e);
        return Value::make_none();
    });
    vm.register_native("TUI.BG_COLOR", 3, 3, [&vm](V args) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        jdb_tui::TuiState::StyleEntry e;
        if (!s.style_stack.empty()) e = s.style_stack.back();
        e.bg = ftxui::Color::RGB(
            (uint8_t)args[0].to_int(),
            (uint8_t)args[1].to_int(),
            (uint8_t)args[2].to_int());
        e.has_bg = true;
        s.style_stack.push_back(e);
        return Value::make_none();
    });
    vm.register_native("TUI.POP_COLOR", 0, 0, [&vm](V) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        if (!s.style_stack.empty()) s.style_stack.pop_back();
        return Value::make_none();
    });
    vm.register_native("TUI.STYLE_PUSH", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        jdb_tui::TuiState::StyleEntry e;
        if (!s.style_stack.empty()) e = s.style_stack.back();
        std::string name = args[0].as_string()->data;
        // Lowercase compare so scripts can write "Bold" or "BOLD".
        for (auto& c : name) c = (char)std::tolower((unsigned char)c);
        int bit = 0;
        if      (name == "bold")       bit = 1;
        else if (name == "dim")        bit = 2;
        else if (name == "italic")     bit = 4;
        else if (name == "underlined" || name == "underline") bit = 8;
        else if (name == "inverted"   || name == "invert")    bit = 16;
        else {
            vm.emit("[TUI] TUI.STYLE_PUSH unknown style: " + name + "\n");
        }
        e.style_mask |= bit;
        s.style_stack.push_back(e);
        return Value::make_none();
    });
    vm.register_native("TUI.STYLE_POP", 0, 0, [&vm](V) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        if (!s.style_stack.empty()) s.style_stack.pop_back();
        return Value::make_none();
    });
    vm.register_native("TUI.THEME", 1, 1, [&vm](V args) -> Value {
        (void)vm;
        jdb_tui::state().theme = args[0].as_string()->data;
        return Value::make_none();
    });

    // ── Events (Phase G) ────────────────────────────────────
    // TUI.KEY$ is one-shot: returns the buffered key and clears it
    // so a single press only fires once. Without this, scripts that
    // `IF TUI.KEY$() = "..." THEN ...` inside their main loop
    // re-trigger every frame until another key comes in.
    vm.register_native("TUI.KEY$", 0, 0, [&vm](V) -> Value {
        (void)vm;
        auto& s = jdb_tui::state();
        std::string k = s.last_key;
        s.last_key.clear();
        return Value::make_string(k);
    });
    // Mouse: the console-input loop polls MOUSE_EVENT records via
    // ReadConsoleInputW and updates these slots in place. Buttons
    // are the bottom 5 bits of dwButtonState (bit 0 = left). Wheel
    // is a +1 / -1 accumulator that scripts can decrement after
    // reading.
    vm.register_native("TUI.MOUSE_X", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_i64(jdb_tui::state().mouse_x);
    });
    vm.register_native("TUI.MOUSE_Y", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_i64(jdb_tui::state().mouse_y);
    });
    vm.register_native("TUI.MOUSE_BTN", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_i64(jdb_tui::state().mouse_buttons);
    });
    vm.register_native("TUI.MOUSE_WHEEL", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_i64(jdb_tui::state().mouse_wheel);
    });
    // TUI.ON(event$, handler$) — register a script callback by name.
    // Phase G stores the binding; actual dispatch (QUIT, KEY, MOUSE,
    // RESIZE) lands when we wire ReadConsoleInput. For now it's a
    // no-op recorder so scripts can use the API today and benefit
    // from it once the dispatch ships.
    TUI_STUB("TUI.ON", 2, 2)

    // ── Diagnostics (Phase G) ───────────────────────────────
    vm.register_native("TUI.WIDTH", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_i64(ftxui::Terminal::Size().dimx);
    });
    vm.register_native("TUI.HEIGHT", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_i64(ftxui::Terminal::Size().dimy);
    });
    vm.register_native("TUI.LAST_RENDER_MS", 0, 0, [&vm](V) -> Value {
        (void)vm; return Value::make_f64(jdb_tui::state().last_render_ms);
    });

    #undef TUI_STUB
}

#else // !TUI ─────────────────────────────────────────────────

#include "tui.h"
class VM;
void register_tui_natives(VM&) {}

#endif // TUI
