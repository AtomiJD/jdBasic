// FTXUI-based REPL - Phase 2b (workspaces):
//
//   * Four workspaces, each with its own VM, output buffer, history,
//     and live input draft. F1-F4 switches; the active tab is shown
//     in the top tab row + repeated in the status line.
//   * Borderless layout: tab row → output → separator → input →
//     separator → status.
//   * Default Bar cursor at write position; an inverted-space overlay
//     replaces the FTXUI 0-width anchor when the input is empty so the
//     cursor cell is always visible.
//   * PgUp/PgDn + mouse wheel scroll the active workspace's output
//     with auto-follow on new content / typing.
//   * ESC / Ctrl+Q / `:quit` exits.
//
// Multi-line input + Ctrl+C clipboard remain deferred (terminal-key
// disambiguation + SIGINT routing). Side-Panel + Cmd-Palette land in
// the next iteration.

#ifdef FTXUI

#include "repl_ftxui.h"
#include "vm.h"
#include "version.h"
#include "ftxui_theme.h"
#include "editor.h"

#include <fstream>
#include <sstream>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

extern void run_on_vm(VM& vm, const std::string& source);
// console_execute handles LOAD / SAVE / RUN / NEW / LINT / etc. and
// falls through to run_on_vm for plain code. Per-workspace
// program_buffer keeps each tab's loaded source independent.
extern void console_execute(const std::string& cmd, VM& vm,
                            std::string& program_buffer);

namespace {

constexpr int N_WS = 4;
const std::array<const char*, N_WS> kWsNames = {
    "main", "sandbox", "tests", "notes"
};

struct Outbox {
    std::mutex m;
    std::deque<std::string> lines;
    std::string pending;
    static constexpr size_t kMax = 5000;

    void push(const std::string& s) {
        std::lock_guard<std::mutex> g(m);
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '\033' && i + 1 < s.size() && s[i+1] == '[') {
                size_t j = i + 2;
                while (j < s.size() && !((s[j] >= 'A' && s[j] <= 'Z') ||
                                         (s[j] >= 'a' && s[j] <= 'z'))) {
                    j++;
                }
                std::string seq = (j < s.size()) ? s.substr(i, j - i + 1) : s.substr(i);
                if (seq.find("\033[2J") != std::string::npos) {
                    lines.clear();
                    pending.clear();
                }
                i = (j < s.size()) ? j : s.size() - 1;
                continue;
            }
            if (c == '\r') continue;
            if (c == '\n') {
                lines.push_back(pending);
                pending.clear();
            } else {
                pending += c;
            }
        }
        while (lines.size() > kMax) lines.pop_front();
    }
};

struct Workspace {
    std::unique_ptr<VM> vm;
    std::shared_ptr<Outbox> outbox;
    std::deque<std::string> history;
    int history_idx = -1;
    std::string draft;             // saved input text on tab-switch
    std::string program_buffer;    // LOAD/SAVE/RUN target per workspace
    // Boot-set snapshot - captured right after setup so the side-panel
    // can show only the vars / funcs the user added this session.
    std::unordered_set<std::string> boot_vars;
    std::unordered_set<std::string> boot_funcs;
};

// Command palette entries - what shows up in the Ctrl+P modal.
// `template_` is what gets injected into the input field; for commands
// that need an argument (load, save) we leave the trailing space so the
// user just types the path. `desc` is a one-line description shown
// dimmed beside the label.
struct CmdEntry {
    std::string label;
    std::string desc;
    std::string template_;
};

const std::vector<CmdEntry> kCommands = {
    {"run",       "execute the current buffer",         "run"},
    {"new",       "clear the current buffer",           "new"},
    {"list",      "list buffer contents",               "list"},
    {"vars",      "show user variables",                "vars"},
    {"funcs",     "show user functions",                "funcs"},
    {"help",      "show jdBasic help",                  "help"},
    {"load",      "load a .jdb file into buffer",       "load "},
    {"edit",      "open a file in the FTXUI editor",    "edit "},
    {"save",      "save buffer to file",                "save "},
    {"loadws",    "load a workspace (.jsws)",           "loadws "},
    {"savews",    "save a workspace (.jsws)",           "savews "},
    {"cls",       "clear the output buffer",            "cls"},
    {"trace on",  "enable trace logging",               "tron"},
    {"trace off", "disable trace logging",              "troff"},
    {":exit",     "exit the FTXUI REPL",                ":exit"},
};

// Build the right-side panel listing user vars + funcs of the active
// workspace. Filters out boot-set names so VM-internal globals don't
// drown out the interesting state. Read-only snapshot per render.
//
// Important: rows are HARD-CAPPED. FTXUI's hbox/yframe combo propagates
// the child's min_y up the tree, so a 200-line panel would push the
// input row off the bottom of the screen. We cap to a small fixed
// budget split between vars + funcs; overflow shows "...more".
ftxui::Element render_side_panel(const Workspace& w) {
    using namespace ftxui;
    constexpr int kMaxVars  = 18;
    constexpr int kMaxFuncs = 12;
    Elements rows;

    rows.push_back(text(" VARIABLES ") | bold | color(jdb_theme::accent));
    int var_count = 0, var_seen = 0;
    auto& names = w.vm->get_global_names();
    auto& globals = w.vm->get_globals();
    for (auto& [name, slot] : names) {
        if (slot >= globals.size()) continue;
        if (w.boot_vars.count(name)) continue;
        if (name.size() >= 2 && name[0] == '_' && name[1] == '_') continue;
        if (name.find('.') != std::string::npos) continue;
        var_seen++;
        if (var_count >= kMaxVars) continue;
        std::string val = globals[slot].to_string();
        if (val.size() > 28) val = val.substr(0, 25) + "...";
        rows.push_back(hbox({
            text(" " + name) | color(jdb_theme::fg_primary),
            text(" = "),
            text(val) | color(jdb_theme::var_value),
        }));
        var_count++;
    }
    if (var_seen == 0) rows.push_back(text("  (none yet)") | dim);
    else if (var_seen > var_count) {
        rows.push_back(text("  ...+" + std::to_string(var_seen - var_count) +
                            " more (PgUp/Dn TBD)") | dim);
    }

    rows.push_back(text(""));
    rows.push_back(text(" FUNCTIONS ") | bold | color(jdb_theme::accent));
    int fn_count = 0, fn_seen = 0;
    for (const auto& f : w.vm->get_funcs()) {
        if (w.boot_funcs.count(f.name)) continue;
        if (f.name.size() >= 2 && f.name[0] == '_' && f.name[1] == '_') continue;
        fn_seen++;
        if (fn_count >= kMaxFuncs) continue;
        std::string sig = f.is_sub ? "SUB " : (f.is_async ? "ASYNC " : "FUNC ");
        sig += f.name + "(";
        for (size_t i = 0; i < f.param_names.size(); i++) {
            if (i) sig += ", ";
            sig += f.param_names[i];
        }
        sig += ")";
        if (sig.size() > 36) sig = sig.substr(0, 33) + "...";
        rows.push_back(text(" " + sig) | color(jdb_theme::func_sig));
        fn_count++;
    }
    if (fn_seen == 0) rows.push_back(text("  (none yet)") | dim);
    else if (fn_seen > fn_count) {
        rows.push_back(text("  ...+" + std::to_string(fn_seen - fn_count) +
                            " more") | dim);
    }

    return vbox(std::move(rows)) | size(WIDTH, EQUAL, 38);
}

ftxui::Element render_outbox_line(const std::string& line) {
    using namespace ftxui;
    if (line.size() >= 2 && line[0] == '>' && line[1] == ' ') {
        return hbox({
            text("> ") | color(jdb_theme::accent),
            text(line.substr(2)) | color(jdb_theme::fg_primary),
        });
    }
    if (line.rfind("error:", 0) == 0) {
        return text(line) | color(jdb_theme::error);
    }
    return text(line);
}

} // namespace

int run_repl_ftxui(std::vector<std::unique_ptr<VM>> vms) {
    using namespace ftxui;

    if (vms.size() != N_WS) {
        // Caller should give us exactly four VMs - anything else is
        // a setup bug.
        return 1;
    }

    auto workspaces = std::make_shared<std::array<Workspace, N_WS>>();
    for (int i = 0; i < N_WS; i++) {
        auto& w = (*workspaces)[i];
        w.vm = std::move(vms[i]);
        w.outbox = std::make_shared<Outbox>();
        auto box = w.outbox;
        w.vm->on_output = [box](const std::string& s) { box->push(s); };
        // Snapshot the VM's "boot-time" vars + funcs so the side-panel
        // can later filter to user-defined items only (mirrors how the
        // MCP server's jdb_vars / jdb_funcs do it).
        for (auto& [name, _slot] : w.vm->get_global_names()) w.boot_vars.insert(name);
        for (auto& f : w.vm->get_funcs()) w.boot_funcs.insert(f.name);
    }
    auto show_panel   = std::make_shared<bool>(false);
    // Command-palette state. When show_palette is true the Modal
    // overlay covers the screen; palette_filter narrows the list and
    // palette_idx drives the highlighted row.
    auto show_palette   = std::make_shared<bool>(false);
    auto palette_filter = std::make_shared<std::string>();
    auto palette_idx    = std::make_shared<int>(0);
    auto active     = std::make_shared<int>(0);
    auto auto_follow   = std::make_shared<bool>(true);
    auto scroll_anchor = std::make_shared<int>(0);

    auto screen = ScreenInteractive::Fullscreen();
    bool exit_requested = false;
    std::string input_text;
    // Shared cursor position for the input row - exposed via Ref so
    // the palette can land the cursor right after the injected text.
    auto input_cursor = std::make_shared<int>(0);

    // Tab-switch helper - saves the current input as the active ws's
    // draft and restores the target ws's draft so each tab feels like
    // it kept its own typing state.
    auto switch_to = [&](int target) {
        if (target == *active || target < 0 || target >= N_WS) return;
        (*workspaces)[*active].draft = input_text;
        *active = target;
        input_text = (*workspaces)[*active].draft;
        *auto_follow = true;
        *scroll_anchor = 0;
    };

    InputOption opt;
    opt.placeholder = "type a jdBasic statement, Enter runs, :exit to leave";
    opt.multiline = false;
    opt.insert = true;
    opt.transform = [](InputState state) {
        if (state.is_placeholder) {
            return hbox({
                text(" ") | inverted,
                state.element | dim
            });
        }
        return state.element | color(jdb_theme::input_text);
    };
    opt.on_change = [&] { *auto_follow = true; };
    // Bind cursor_position via Ref<int>(int*) so palette injections can
    // place the cursor at the end of the template string.
    opt.cursor_position = input_cursor.get();
    opt.on_enter = [&] {
        std::string code = input_text;
        input_text.clear();
        *input_cursor = 0;
        auto& w = (*workspaces)[*active];
        w.history_idx = -1;
        w.draft.clear();
        *auto_follow = true;
        if (code.empty()) return;
        // `:exit` mirrors the legacy console's EXIT command. `:quit`
        // kept as a friendly synonym for muscle memory.
        if (code == ":exit" || code == ":quit" || code == ":q") {
            exit_requested = true;
            screen.ExitLoopClosure()();
            return;
        }
        w.history.push_back(code);
        if (w.history.size() > 200) w.history.pop_front();
        w.outbox->push("> " + code + "\n");

        // `edit` / `edit <file>` - port of the legacy console_execute
        // EDIT handler. The Editor class uses raw termios (POSIX) /
        // Win32 console API directly, so it owns the terminal once
        // we suspend FTXUI via WithRestoredIO. After it exits we
        // mirror the lines back into the workspace's program_buffer
        // and, if F5 was pressed, run it on the workspace's VM -
        // same UX as the legacy REPL.
        {
            std::string upper = code;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if (upper == "EDIT" || upper.substr(0, 5) == "EDIT ") {
                std::string edit_file;
                if (upper.size() > 5) {
                    edit_file = code.substr(5);
                    while (!edit_file.empty() && (edit_file.front() == ' ' || edit_file.front() == '\t')) edit_file.erase(0, 1);
                    while (!edit_file.empty() && (edit_file.back()  == ' ' || edit_file.back()  == '\t')) edit_file.pop_back();
                    if (!edit_file.empty() && edit_file.find('.') == std::string::npos) edit_file += ".jdb";
                }
                std::vector<std::string> lines;
                if (!edit_file.empty()) {
                    std::ifstream in(edit_file);
                    if (in.is_open()) {
                        std::string l;
                        while (std::getline(in, l)) lines.push_back(l);
                    }
                } else if (!w.program_buffer.empty()) {
                    std::istringstream ss(w.program_buffer);
                    std::string l;
                    while (std::getline(ss, l)) lines.push_back(l);
                }
                if (lines.empty()) lines.push_back("");
                Editor editor(lines, edit_file);
                bool wants_run = false;
                auto saved = w.vm->on_output;
                w.vm->on_output = nullptr;
                screen.WithRestoredIO([&] {
                    editor.run();
                    wants_run = editor.wants_run();
                })();
                w.vm->on_output = saved;
                // Mirror buffer back
                std::string new_buf;
                for (size_t li = 0; li < lines.size(); li++) {
                    new_buf += lines[li];
                    if (li + 1 < lines.size()) new_buf += "\n";
                }
                w.program_buffer = new_buf;
                w.outbox->push("[edit done — " + std::to_string(lines.size()) + " line(s)"
                               + (wants_run ? ", running...]" : "]") + "\n");
                if (wants_run) {
                    // F5 in editor → compile + run the buffer (no save).
                    // Mirror legacy console_execute behaviour.
                    auto saved2 = w.vm->on_output;
                    w.vm->on_output = nullptr;
                    screen.WithRestoredIO([&] {
                        try { run_on_vm(*w.vm, w.program_buffer); }
                        catch (const std::exception& e) {
                            w.outbox->push(std::string("error: ") + e.what() + "\n");
                        } catch (...) {}
                        w.vm->is_halted = false;
                    })();
                    w.vm->on_output = saved2;
                }
                return;
            }
        }

        // RUN runs the program_buffer. Simple scripts (PRINT loops,
        // computation) should keep their output in the outbox - that's
        // the natural REPL view. Console-mode interactive programs
        // (Snake-style: CLS + ON "KEYDOWN" + INKEY$ + LOCATE) need the
        // raw terminal so VM event_poll's _kbhit branch can see keys
        // and ANSI screen-positioning escapes hit a real cursor.
        //
        // Heuristic: scan program_buffer for stdin-blocking primitives
        // and ON-KEY handlers. If any are present → use WithRestoredIO
        // and let emit() fall through to std::cout. Otherwise → keep
        // on_output bound to the outbox.
        auto wants_terminal_io = [&]() -> bool {
            std::string upper = code;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            bool is_run_cmd =
                (upper == "RUN" || upper.substr(0, 4) == "RUN ");
            if (!is_run_cmd) return false;
            std::string buf = w.program_buffer;
            std::transform(buf.begin(), buf.end(), buf.begin(), ::toupper);
            return buf.find("ON \"KEYDOWN\"") != std::string::npos
                || buf.find("ON \"KEYUP\"")   != std::string::npos
                || buf.find("INKEY$")          != std::string::npos
                || buf.find("WAITKEY")         != std::string::npos;
        };

        auto run_it = [&] {
            try {
                console_execute(code, *w.vm, w.program_buffer);
            } catch (const std::exception& e) {
                w.outbox->push(std::string("error: ") + e.what() + "\n");
            } catch (...) {
                w.outbox->push("error: unknown\n");
            }
        };

        if (wants_terminal_io()) {
            auto saved = w.vm->on_output;
            w.vm->on_output = nullptr;     // emit() falls through to std::cout
            screen.WithRestoredIO(run_it)();
            w.vm->on_output = saved;
        } else {
            run_it();
        }
    };
    auto input_box = Input(&input_text, opt);

    auto input_with_history = CatchEvent(input_box, [&](Event e) {
        auto& w = (*workspaces)[*active];
        if (!w.history.empty()) {
            if (e == Event::ArrowUp) {
                if (w.history_idx == -1) {
                    w.draft = input_text;
                    w.history_idx = (int)w.history.size() - 1;
                } else if (w.history_idx > 0) {
                    w.history_idx -= 1;
                }
                input_text = w.history[w.history_idx];
                *input_cursor = (int)input_text.size();
                return true;
            }
            if (e == Event::ArrowDown) {
                if (w.history_idx == -1) return false;
                w.history_idx += 1;
                if (w.history_idx >= (int)w.history.size()) {
                    w.history_idx = -1;
                    input_text = w.draft;
                } else {
                    input_text = w.history[w.history_idx];
                }
                *input_cursor = (int)input_text.size();
                return true;
            }
        }
        auto active_outbox = (*workspaces)[*active].outbox;
        auto scroll_up = [&](int n) {
            *auto_follow = false;
            int last = std::max(0, (int)active_outbox->lines.size() - 1);
            int cur = *scroll_anchor;
            if (cur <= 0 || cur > last) cur = last;
            *scroll_anchor = std::max(0, cur - n);
        };
        auto scroll_down = [&](int n) {
            int last = std::max(0, (int)active_outbox->lines.size() - 1);
            int cur = *scroll_anchor;
            if (cur <= 0 || cur > last) cur = last;
            cur = std::min(last, cur + n);
            *scroll_anchor = cur;
            if (cur >= last) *auto_follow = true;
        };
        if (e == Event::PageUp)   { scroll_up(10);   return true; }
        if (e == Event::PageDown) { scroll_down(10); return true; }
        if (e.is_mouse()) {
            const auto& m = e.mouse();
            if (m.button == Mouse::WheelUp)   { scroll_up(3);   return true; }
            if (m.button == Mouse::WheelDown) { scroll_down(3); return true; }
        }
        return false;
    });

    // ── Command palette modal ──────────────────────────────────────
    InputOption pal_opt;
    pal_opt.placeholder = "filter (run, vars, load, ...)";
    pal_opt.multiline = false;
    auto palette_input = Input(palette_filter.get(), pal_opt);

    // Re-filter on every render based on current palette_filter.
    auto get_filtered = [palette_filter]() {
        std::vector<CmdEntry> out;
        std::string f = *palette_filter;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        for (const auto& e : kCommands) {
            std::string l = e.label;
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            if (f.empty() || l.find(f) != std::string::npos) {
                out.push_back(e);
            }
        }
        return out;
    };

    auto palette_renderer = Renderer(palette_input, [&] {
        auto filtered = get_filtered();
        if (filtered.empty()) {
            return vbox({
                text(" Command Palette ") | bold | color(jdb_theme::accent) | center,
                separator(),
                hbox({ text("> "),
                       palette_input->Render() | flex }),
                separator(),
                text("(no match)") | dim | center,
            }) | border | size(WIDTH, EQUAL, 60) | size(HEIGHT, EQUAL, 18);
        }
        if (*palette_idx >= (int)filtered.size()) *palette_idx = 0;
        if (*palette_idx < 0) *palette_idx = 0;
        Elements rows;
        for (size_t i = 0; i < filtered.size(); i++) {
            Element row = hbox({
                text("  " + filtered[i].label) | size(WIDTH, EQUAL, 14),
                text("  ") | dim,
                text(filtered[i].desc) | dim,
            });
            if ((int)i == *palette_idx) {
                // bgcolor for the visual highlight; focus tells the
                // surrounding yframe to scroll this row into view when
                // the selection moves past the visible window.
                row = row | bgcolor(jdb_theme::accent_bg) | color(jdb_theme::fg_primary) | focus;
            }
            rows.push_back(row);
        }
        return vbox({
            text(" Command Palette ") | bold | color(jdb_theme::accent) | center,
            separator(),
            hbox({ text("> ") | color(jdb_theme::accent),
                   palette_input->Render() | flex }),
            separator(),
            vbox(std::move(rows)) | yframe | yflex,
            separator(),
            text(" Up/Dn navigate · Enter inject · Esc cancel ") | dim | center,
        }) | border | size(WIDTH, EQUAL, 60) | size(HEIGHT, EQUAL, 18);
    });

    auto palette_with_keys = CatchEvent(palette_renderer, [&](Event e) {
        auto filtered = get_filtered();
        if (e == Event::Escape) {
            *show_palette = false;
            return true;
        }
        if (filtered.empty()) return false;
        if (e == Event::ArrowDown) {
            *palette_idx = std::min((int)filtered.size() - 1, *palette_idx + 1);
            return true;
        }
        if (e == Event::ArrowUp) {
            *palette_idx = std::max(0, *palette_idx - 1);
            return true;
        }
        if (e == Event::Return) {
            const auto& sel = filtered[*palette_idx];
            input_text = sel.template_;
            // Place the cursor exactly one position after the injected
            // text. For "load " (trailing space) that's right after the
            // space, ready for the user to type a filename. For "run"
            // it's at the end of the word, ready for Enter.
            *input_cursor = (int)input_text.size();
            *show_palette = false;
            *palette_filter = "";
            *palette_idx = 0;
            input_with_history->TakeFocus();
            return true;
        }
        return false;
    });

    auto layout = Container::Vertical({ input_with_history });
    layout->SetActiveChild(input_with_history.get());
    input_with_history->TakeFocus();

    auto renderer = Renderer(layout, [&] {
        // Tab row - one segment per workspace, active in cyan-bold.
        Elements tabs;
        for (int i = 0; i < N_WS; i++) {
            std::string label = " F" + std::to_string(i + 1) + " " +
                                kWsNames[i] + " ";
            Element seg = text(label);
            if (i == *active) {
                // Active tab: cyan text, bold, plus an underline so it
                // reads as "selected" without the heavy inverted-bg
                // contrast that the previous version had.
                seg = seg | bold | color(jdb_theme::accent) | underlined;
            } else {
                seg = seg | color(jdb_theme::fg_muted);
            }
            tabs.push_back(seg);
            if (i + 1 < N_WS) tabs.push_back(text(" "));
        }

        // Output rows for the active workspace.
        Elements rows;
        size_t total = 0;
        auto& w = (*workspaces)[*active];
        {
            std::lock_guard<std::mutex> g(w.outbox->m);
            total = w.outbox->lines.size();
            for (const auto& ln : w.outbox->lines) {
                rows.push_back(render_outbox_line(ln));
            }
            if (!w.outbox->pending.empty()) {
                rows.push_back(render_outbox_line(w.outbox->pending));
            }
        }
        if (rows.empty()) {
            rows.push_back(text("(no output yet — try `PRINT 1+1`)") | dim);
        }

        int last = (int)rows.size() - 1;
        int focus_idx = *auto_follow ? last
                                     : std::clamp(*scroll_anchor, 0, last);
        rows[focus_idx] = rows[focus_idx] | focus;

        std::string scroll_hint = *auto_follow
            ? std::string(" follow ")
            : (" line " + std::to_string(focus_idx + 1) + "/" +
               std::to_string(rows.size()) + " ");
        std::string panel_hint = *show_panel ? "panel:on" : "panel:off";
        std::string status =
            " jdBasic v" JDBASIC_VERSION " · build " JDBASIC_BUILD_NUM
            " · ws=" + std::string(kWsNames[*active]) +
            " ·" + scroll_hint + "· " + std::to_string(total) +
            " lines · F1-F4 · Ctrl+B " + panel_hint +
            " · Ctrl+P palette · :exit ";

        // Output area; if the side panel is on, lay it horizontally
        // beside the output column. Two important wrappers when the
        // panel is on:
        //   1. The hbox itself gets `yflex` so the OUTER vbox still
        //      knows to give it the leftover y space (without this,
        //      the outer vbox shrinks the hbox to its min_y and the
        //      input row falls off the bottom of the screen).
        //   2. The panel column gets `yframe | flex_shrink` so its
        //      intrinsic min_y (sum of all rendered rows) doesn't
        //      bubble up and force the hbox to be tall.
        Element output_area = vbox(std::move(rows)) | yframe | yflex;
        if (*show_panel) {
            output_area = hbox({
                output_area | flex,
                separator(),
                render_side_panel((*workspaces)[*active])
                    | yframe | flex_shrink,
            }) | yflex;
        }

        return vbox({
            hbox(std::move(tabs)),
            separator(),
            output_area,
            separator(),
            hbox({ text("> ") | color(jdb_theme::accent),
                   input_with_history->Render() | flex }),
            separator(),
            text(status) | color(jdb_theme::fg_dim),
        });
    });

    // Modal-wrap so the palette overlays the main view when active.
    auto root = Modal(renderer, palette_with_keys, show_palette.get());

    auto with_keys = CatchEvent(root, [&](Event e) {
        // Exit only via the explicit `:exit` command - no panic-keys.
        // Esc closes the palette if it's open; otherwise it clears the
        // current input line (so the user can abort a half-typed
        // statement without backspace-spamming).
        if (e == Event::Escape) {
            if (*show_palette) {
                *show_palette = false;
                input_with_history->TakeFocus();
                return true;
            }
            input_text.clear();
            *input_cursor = 0;
            return true;
        }
        if (e == Event::CtrlP) {
            *show_palette = !*show_palette;
            *palette_filter = "";
            *palette_idx = 0;
            if (*show_palette) palette_input->TakeFocus();
            else               input_with_history->TakeFocus();
            return true;
        }
        // F-keys + Ctrl+B only when palette is closed; otherwise typing
        // would fight with the filter Input.
        if (!*show_palette) {
            if (e == Event::F1) { switch_to(0); return true; }
            if (e == Event::F2) { switch_to(1); return true; }
            if (e == Event::F3) { switch_to(2); return true; }
            if (e == Event::F4) { switch_to(3); return true; }
            if (e == Event::CtrlB) {
                *show_panel = !*show_panel;
                return true;
            }
        }
        return false;
    });

    screen.Loop(with_keys);

    // Restore original on_output before returning so any subsequent
    // VM use (unlikely after main exits, but cheap to be tidy) doesn't
    // dangle into our destroyed outboxes.
    for (auto& w : *workspaces) {
        w.vm->on_output = nullptr;
    }
    return exit_requested ? 0 : 0;
}

#endif // FTXUI
