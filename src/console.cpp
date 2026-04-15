// token.h MUST come before windows.h to avoid macro conflicts
#include "token.h"
#include "natives_list.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <conio.h>

#include "console.h"
#include "vm.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <chrono>

// ── Constructor / Destructor ─────────────────────────────────

Console::Console() {
    // Create a VM for each workspace
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        workspaces[i].vm = std::make_unique<VM>();
        // Wire VM output to the workspace's screen buffer
        auto* screen = &workspaces[i].screen;
        workspaces[i].vm->on_output = [this, i, screen](const std::string& text) {
            screen->write(text);
            // If this workspace is active, flush to real console immediately
            if (i == active_ws) {
                std::cout << text;
                std::cout.flush();
            }
        };
    }
    load_history();
    enable_raw_mode();
}

Console::~Console() {
    save_history();
    disable_raw_mode();
}

VM& Console::active_vm() { return *workspaces[active_ws].vm; }
ConsoleState& Console::active_state() { return workspaces[active_ws].console; }

// ── Raw mode ─────────────────────────────────────────────────

void Console::enable_raw_mode() {
#if defined(_WIN32)
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD console_mode = 0;
    if (GetConsoleMode(hStdin, &console_mode)) {
        original_console_mode = console_mode;
        DWORD mode = console_mode;
        mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        mode |= ENABLE_EXTENDED_FLAGS;
        mode |= ENABLE_WINDOW_INPUT;
        mode |= ENABLE_QUICK_EDIT_MODE;
        SetConsoleMode(hStdin, mode);
    }
    DWORD out_mode = 0;
    if (GetConsoleMode(hStdout, &out_mode)) {
        out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hStdout, out_mode);
    }
#endif
}

void Console::disable_raw_mode() {
#if defined(_WIN32)
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hStdin, (DWORD)original_console_mode);
#endif
}

// ── Key reading ──────────────────────────────────────────────

int Console::read_raw_key() {
#if defined(_WIN32)
    if (!utf8_buffer.empty()) {
        int c = utf8_buffer.front();
        utf8_buffer.pop();
        return c;
    }

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD ir;
    DWORD read;

    if (PeekConsoleInputW(hStdin, &ir, 1, &read) && read > 0) {
        ReadConsoleInputW(hStdin, &ir, 1, &read);

        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            auto& ke = ir.Event.KeyEvent;

            bool ctrl = (ke.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            bool alt = (ke.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
            // AltGr sends Ctrl+Alt — don't treat as Ctrl combo
            if (ctrl && !alt) {
                switch (ke.wVirtualKeyCode) {
                    case 'C':      return KEY_CTRL_C;
                    case 'V':      return KEY_CTRL_V;
                    case VK_LEFT:  return KEY_CTRL_LEFT;
                    case VK_RIGHT: return KEY_CTRL_RIGHT;
                }
            }

            switch (ke.wVirtualKeyCode) {
                case VK_UP:     return KEY_UP;
                case VK_DOWN:   return KEY_DOWN;
                case VK_LEFT:   return KEY_LEFT;
                case VK_RIGHT:  return KEY_RIGHT;
                case VK_HOME:   return KEY_HOME;
                case VK_END:    return KEY_END;
                case VK_DELETE: return KEY_DELETE;
                case VK_BACK:   return KEY_BACKSPACE;
                case VK_RETURN: return KEY_ENTER;
                case VK_TAB:    return KEY_TAB;
                case VK_F1:     return KEY_F1;
                case VK_F2:     return KEY_F2;
                case VK_F3:     return KEY_F3;
                case VK_F4:     return KEY_F4;
                case VK_F5:     return KEY_F5;
                case VK_F7:     return KEY_F7;
                case VK_F8:     return KEY_F8;
                case VK_ESCAPE: return KEY_ESC;
            }

            WCHAR wch = ke.uChar.UnicodeChar;
            if (wch != 0) {
                std::wstring utf16_str;
                if (wch >= 0xD800 && wch <= 0xDBFF) {
                    high_surrogate = wch;
                    return 0;
                } else if (wch >= 0xDC00 && wch <= 0xDFFF) {
                    if (high_surrogate != 0) {
                        utf16_str += high_surrogate;
                        utf16_str += wch;
                        high_surrogate = 0;
                    } else { return 0; }
                } else {
                    utf16_str += wch;
                    high_surrogate = 0;
                }

                int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16_str.c_str(),
                    (int)utf16_str.length(), NULL, 0, NULL, NULL);
                std::string utf8_str(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, utf16_str.c_str(),
                    (int)utf16_str.length(), &utf8_str[0], size_needed, NULL, NULL);

                for (char c : utf8_str) utf8_buffer.push(static_cast<unsigned char>(c));
                if (!utf8_buffer.empty()) {
                    int c = utf8_buffer.front();
                    utf8_buffer.pop();
                    return c;
                }
            }
        }
    }
    return 0;
#else
    return 0;
#endif
}

// ── Main loop ────────────────────────────────────────────────

void Console::run() {
    // Banner is printed by main.cpp before Console::run()
    render_prompt();

    while (is_running) {
        // Process RECUR tasks
        process_recur_tasks();
        if (!pending_executions.empty()) {
            std::vector<std::string> to_run;
            { std::lock_guard<std::mutex> lock(recur_mutex);
              to_run.swap(pending_executions); }
            for (auto& code : to_run) {
                if (executor) {
                    auto& ws = workspaces[active_ws];
                    if (!ws.executing) {
                        disable_raw_mode();
                        executor(code, *ws.vm, ws.console.program_buffer);
                        enable_raw_mode();
                    }
                }
            }
            render_prompt();
        }

        // Check if active workspace's worker thread has finished
        auto& aws = workspaces[active_ws];
        if (aws.worker.joinable() && !aws.executing) {
            aws.worker.join();
            // Re-enable raw mode and re-render prompt
            enable_raw_mode();
#if defined(_WIN32)
            FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
            while (!utf8_buffer.empty()) utf8_buffer.pop();
            high_surrogate = 0;
#endif
            render_prompt();
        }

        if (workspaces[active_ws].executing) {
            // Program is running in worker thread.
            if (workspaces[active_ws].vm->is_waiting_input) {
                // VM is blocking on INPUT/std::cin — do NOT touch console buffer
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } else {
                // VM is running normally (INKEY$ polls non-blocking) —
                // peek for F1-F4 workspace switch, leave everything else
#if defined(_WIN32)
                HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
                INPUT_RECORD ir;
                DWORD peeked;
                if (PeekConsoleInputW(hStdin, &ir, 1, &peeked) && peeked > 0) {
                    if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                        WORD vk = ir.Event.KeyEvent.wVirtualKeyCode;
                        if (vk >= VK_F1 && vk <= VK_F4) {
                            ReadConsoleInputW(hStdin, &ir, 1, &peeked);
                            switch_workspace(vk - VK_F1);
                        }
                        // Other keys: leave for INKEY$/WAITKEY$
                    } else {
                        // Non-key events (mouse, focus): discard
                        ReadConsoleInputW(hStdin, &ir, 1, &peeked);
                    }
                }
#endif
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            int key = read_raw_key();
            if (key > 0) {
                process_key(key);
                for (int i = 0; i < 4096; i++) {
                    int k2 = read_raw_key();
                    if (k2 <= 0) break;
                    process_key(k2);
                }
                if (dirty_prompt) {
                    render_prompt();
                    dirty_prompt = false;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    // Join any running worker threads on exit
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (workspaces[i].worker.joinable()) {
            workspaces[i].vm->is_halted = true;  // signal stop
            workspaces[i].worker.join();
        }
    }
}

// ── Workspace switching ──────────────────────────────────────

void Console::switch_workspace(int target) {
    if (target == active_ws) return;

    // Each workspace has its own VM — no state swap needed
    active_ws = target;

    // Clear screen and replay target workspace's output buffer
    std::cout << "\033[2J\033[H";
    std::cout << workspaces[active_ws].screen.replay();
    std::cout.flush();

    set_color(14, 0);
    print("--- Workspace " + std::to_string(active_ws + 1) + " ---");
    set_color(7, 0);
    print("\r\n");
    render_prompt();
}

// ── Key processing ───────────────────────────────────────────

void Console::process_key(int key) {
    auto& state = active_state();

    if (key != KEY_F8) {
        state.is_f8_searching = false;
    }

    // F1-F4: workspace switching
    if (key >= KEY_F1 && key <= KEY_F4) {
        switch_workspace(key - KEY_F1);
        return;
    }

    if (key == KEY_ESC) {
        auto& state = active_state();
        state.current_input.clear();
        state.cursor_pos = 0;
        state.history_idx = -1;
        dirty_prompt = true;
    }
    else if (key == KEY_F7) {
        show_history_f7();
    }
    else if (key == KEY_F8) {
        search_history_f8();
    }
    else if (key == KEY_CTRL_C) {
        copy_to_clipboard(state.current_input);
    }
    else if (key == KEY_CTRL_V) {
        std::string clip = get_from_clipboard();
        clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
        clip.erase(std::remove(clip.begin(), clip.end(), '\n'), clip.end());
        state.current_input.insert(state.cursor_pos, clip);
        state.cursor_pos += (int)clip.length();
        dirty_prompt = true;
    }
    else if (key == KEY_TAB) {
        state.current_input.insert(state.cursor_pos, "    ");
        state.cursor_pos += 4;
        dirty_prompt = true;
    }
    else if (key == KEY_ENTER) {
        execute_current_line();
        dirty_prompt = false; // execute_current_line already rendered
    }
    else if (key == KEY_UP) {
        navigate_history(1);
    }
    else if (key == KEY_DOWN) {
        navigate_history(-1);
    }
    else if (key == KEY_BACKSPACE) {
        if (state.cursor_pos > 0) {
            int start = state.cursor_pos - 1;
            while (start > 0 && (static_cast<unsigned char>(state.current_input[start]) & 0xC0) == 0x80)
                start--;
            state.current_input.erase(start, state.cursor_pos - start);
            state.cursor_pos = start;
            dirty_prompt = true;
        }
    }
    else if (key == KEY_LEFT) {
        if (state.cursor_pos > 0) {
            state.cursor_pos--;
            while (state.cursor_pos > 0 &&
                   (static_cast<unsigned char>(state.current_input[state.cursor_pos]) & 0xC0) == 0x80)
                state.cursor_pos--;
            dirty_prompt = true;
        }
    }
    else if (key == KEY_RIGHT) {
        if (state.cursor_pos < (int)state.current_input.length()) {
            state.cursor_pos++;
            while (state.cursor_pos < (int)state.current_input.length() &&
                   (static_cast<unsigned char>(state.current_input[state.cursor_pos]) & 0xC0) == 0x80)
                state.cursor_pos++;
            dirty_prompt = true;
        }
    }
    else if (key == KEY_HOME) {
        state.cursor_pos = 0;
        dirty_prompt = true;
    }
    else if (key == KEY_END) {
        state.cursor_pos = (int)state.current_input.length();
        dirty_prompt = true;
    }
    else if (key == KEY_DELETE) {
        if (state.cursor_pos < (int)state.current_input.length()) {
            int end = state.cursor_pos + 1;
            while (end < (int)state.current_input.length() &&
                   (static_cast<unsigned char>(state.current_input[end]) & 0xC0) == 0x80)
                end++;
            state.current_input.erase(state.cursor_pos, end - state.cursor_pos);
            dirty_prompt = true;
        }
    }
    else if (key == KEY_CTRL_LEFT) {
        auto move_left = [&]() {
            if (state.cursor_pos > 0) {
                state.cursor_pos--;
                while (state.cursor_pos > 0 &&
                       (static_cast<unsigned char>(state.current_input[state.cursor_pos]) & 0xC0) == 0x80)
                    state.cursor_pos--;
            }
        };
        while (state.cursor_pos > 0 && std::isspace((unsigned char)state.current_input[state.cursor_pos - 1])) move_left();
        while (state.cursor_pos > 0 && !std::isspace((unsigned char)state.current_input[state.cursor_pos - 1])) move_left();
        dirty_prompt = true;
    }
    else if (key == KEY_CTRL_RIGHT) {
        auto move_right = [&]() {
            if (state.cursor_pos < (int)state.current_input.length()) {
                state.cursor_pos++;
                while (state.cursor_pos < (int)state.current_input.length() &&
                       (static_cast<unsigned char>(state.current_input[state.cursor_pos]) & 0xC0) == 0x80)
                    state.cursor_pos++;
            }
        };
        while (state.cursor_pos < (int)state.current_input.length() && !std::isspace((unsigned char)state.current_input[state.cursor_pos])) move_right();
        while (state.cursor_pos < (int)state.current_input.length() && std::isspace((unsigned char)state.current_input[state.cursor_pos])) move_right();
        dirty_prompt = true;
    }
    else if (key >= 32 && key <= 255) {
        state.current_input.insert(state.cursor_pos++, 1, static_cast<char>(key));
        dirty_prompt = true;
    }
}

// ── Execute ──────────────────────────────────────────────────

// Check if a command should run asynchronously in a worker thread
static bool is_async_command(const std::string& upper) {
    return (upper == "RUN" ||
            upper.substr(0, 4) == "RUN " ||
            upper == "RESUME");
}

void Console::execute_current_line() {
    auto& state = active_state();
    auto& ws = workspaces[active_ws];
    std::string cmd = state.current_input;

    // Trim
    size_t s = cmd.find_first_not_of(" \t");
    size_t e = cmd.find_last_not_of(" \t");
    if (s != std::string::npos) cmd = cmd.substr(s, e - s + 1);
    else cmd.clear();

    print("\r\n");

    if (!cmd.empty()) {
        std::string upper = cmd;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        if (upper == "EXIT" || upper == "QUIT") {
            is_running = false;
            return;
        }

        // If this workspace is already executing, reject new commands
        if (ws.executing) {
            print("Program running. Press F1-F4 to switch workspace.\r\n");
            state.current_input.clear();
            state.cursor_pos = 0;
            state.history_idx = -1;
            render_prompt();
            return;
        }

        state.history.push_front(cmd);
        if (state.history.size() > 500) state.history.pop_back();

        if (executor) {
            if (is_async_command(upper)) {
                // Run asynchronously in worker thread
                // Join any previous worker first
                if (ws.worker.joinable()) ws.worker.join();

                int ws_idx = active_ws;
                ws.executing = true;
                // Disable raw mode so _kbhit()/_getch() in INKEY$/WAITKEY$ work
                disable_raw_mode();
                ws.worker = std::thread([this, cmd, ws_idx]() {
                    auto& w = workspaces[ws_idx];
                    executor(cmd, *w.vm, w.console.program_buffer);
                    w.executing = false;
                });

                // Don't render prompt — it will be rendered when worker finishes
                state.current_input.clear();
                state.cursor_pos = 0;
                state.history_idx = -1;
                return;
            } else {
                // Run synchronously (simple commands: PRINT x, x = 5, LOAD, etc.)
                disable_raw_mode();
                executor(cmd, *ws.vm, state.program_buffer);
                enable_raw_mode();
#if defined(_WIN32)
                HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                CONSOLE_SCREEN_BUFFER_INFO cInfo;
                if (GetConsoleScreenBufferInfo(hOut, &cInfo) && cInfo.dwCursorPosition.X > 0) {
                    print("\r\n");
                }
                FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
                while (!utf8_buffer.empty()) utf8_buffer.pop();
                high_surrogate = 0;
#endif
            }
        }
    }

    state.current_input.clear();
    state.cursor_pos = 0;
    state.history_idx = -1;
    render_prompt();
}

// ── History ──────────────────────────────────────────────────

void Console::navigate_history(int direction) {
    auto& state = active_state();
    if (state.history.empty()) return;

    state.history_idx += direction;
    if (state.history_idx < 0) {
        state.history_idx = -1;
        state.current_input = "";
    } else if (state.history_idx >= (int)state.history.size()) {
        state.history_idx = (int)state.history.size() - 1;
    } else {
        state.current_input = state.history[state.history_idx];
    }

    state.cursor_pos = (int)state.current_input.length();
    render_prompt();
}

void Console::show_history_f7() {
    auto& state = active_state();
    if (state.history.empty()) return;

#if defined(_WIN32)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD original_colors = 7;
    if (GetConsoleScreenBufferInfo(hConsole, &consoleInfo))
        original_colors = consoleInfo.wAttributes;
#endif

    int selected_idx = 0;
    bool selecting = true;
    int display_count = std::min(20, (int)state.history.size());
    bool needs_redraw = true;

    while (selecting) {
        if (needs_redraw) {
            print("\033[2J\033[H");
            set_color(0, 7);
            print(" === WS" + std::to_string(active_ws + 1) + " History (F7) === ");
            set_color(7, 0);
            print("\r\n [UP/DOWN]: Select | [ENTER]: Confirm | [ESC]: Cancel\r\n\r\n");

            for (int i = 0; i < display_count; ++i) {
                if (i == selected_idx) {
                    set_color(10, 0);
                    print(" > " + state.history[i]);
                    set_color(7, 0);
                } else {
                    print("   " + state.history[i]);
                }
                print("\r\n");
            }
            needs_redraw = false;
        }

        int key = read_raw_key();
        if (key > 0) {
            if (key == KEY_UP && selected_idx > 0) { selected_idx--; needs_redraw = true; }
            else if (key == KEY_DOWN && selected_idx < display_count - 1) { selected_idx++; needs_redraw = true; }
            else if (key == KEY_ENTER) {
                state.current_input = state.history[selected_idx];
                state.cursor_pos = (int)state.current_input.length();
                selecting = false;
            }
            else if (key == KEY_ESC) { selecting = false; }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    print("\033[2J\033[H");
#if defined(_WIN32)
    SetConsoleTextAttribute(hConsole, original_colors);
#endif
    render_prompt();
}

void Console::search_history_f8() {
    auto& state = active_state();
    if (state.history.empty()) return;

    if (!state.is_f8_searching) {
        state.history_search_prefix = state.current_input;
        state.is_f8_searching = true;
    }

    int start_idx = state.history_idx + 1;
    bool found = false;

    for (int i = start_idx; i < (int)state.history.size(); ++i) {
        if (state.history_search_prefix.empty() ||
            state.history[i].rfind(state.history_search_prefix, 0) == 0) {
            state.history_idx = i;
            state.current_input = state.history[i];
            state.cursor_pos = (int)state.current_input.length();
            found = true;
            break;
        }
    }
    if (!found) {
        state.history_idx = -1;
        for (int i = 0; i < start_idx && i < (int)state.history.size(); ++i) {
            if (state.history_search_prefix.empty() ||
                state.history[i].rfind(state.history_search_prefix, 0) == 0) {
                state.history_idx = i;
                state.current_input = state.history[i];
                state.cursor_pos = (int)state.current_input.length();
                found = true;
                break;
            }
        }
    }
    if (!found) {
        state.current_input = state.history_search_prefix;
        state.cursor_pos = (int)state.current_input.length();
    }
    render_prompt();
}

// ── Clipboard ────────────────────────────────────────────────

void Console::copy_to_clipboard(const std::string& text) {
#if defined(_WIN32)
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
        if (hg) {
            memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
#endif
}

std::string Console::get_from_clipboard() {
    std::string result;
#if defined(_WIN32)
    if (OpenClipboard(NULL)) {
        HANDLE hg = GetClipboardData(CF_TEXT);
        if (hg) {
            char* str = static_cast<char*>(GlobalLock(hg));
            if (str) { result = str; GlobalUnlock(hg); }
        }
        CloseClipboard();
    }
#endif
    return result;
}

// ── Rendering ────────────────────────────────────────────────

void Console::set_color(int fg, int bg) {
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hOut, (WORD)(fg | (bg << 4)));
#endif
}

void Console::print(const std::string& text) {
    std::cout << text;
    std::cout.flush();
}

void Console::println(const std::string& text) {
    std::cout << text << "\r\n";
    std::cout.flush();
}

bool Console::is_keyword(const std::string& word) const {
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto& kw = keywords();
    return kw.find(upper) != kw.end();
}

bool Console::is_native(const std::string& word) const {
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto& ns = native_names();
    return ns.find(upper) != ns.end();
}

void Console::render_prompt() {
    auto& state = active_state();
    std::cout.clear();
    static int last_drawn_total_visual_len = 0;

    std::string prompt_str = "WS" + std::to_string(active_ws + 1) + "> ";
    int prompt_len = (int)prompt_str.length();

#if defined(_WIN32)
    // ── Flicker-free rendering via CHAR_INFO back-buffer ─────
    // We assemble the full prompt + input into a grid of CHAR_INFO cells
    // and hand it to WriteConsoleOutputW in a single call. WriteConsoleOutputW
    // is atomic: no intermediate "cleared" state is visible, so there is no
    // flicker even on fast typing or paste.

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return;

    const int cols = csbi.dwSize.X;
    if (cols <= 0) return;

    // Colour attributes matching the old set_color palette
    const WORD ATTR_DEFAULT = csbi.wAttributes;
    const WORD ATTR_PROMPT  = FOREGROUND_GREEN | FOREGROUND_INTENSITY;      // 10
    const WORD ATTR_STRING  = FOREGROUND_BLUE  | FOREGROUND_GREEN;          // 3
    const WORD ATTR_NUMBER  = FOREGROUND_RED   | FOREGROUND_GREEN;          // 6 (dark yellow)
    const WORD ATTR_COMMENT = FOREGROUND_INTENSITY;                         // 8 (gray)
    const WORD ATTR_IDENT   = FOREGROUND_GREEN;                             // 2
    const WORD ATTR_KEYWORD = FOREGROUND_RED   | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // 5
    const WORD ATTR_NATIVE  = FOREGROUND_BLUE  | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // 11
    const WORD ATTR_PUNCT   = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE;      // 7

    const std::string& input = state.current_input;

    // Pass 1: assign a colour to each UTF-8 *codepoint* of the input.
    // We track visual positions so we can compute wrapped row/col later.
    struct TaggedCell { wchar_t ch; WORD attr; };
    std::vector<TaggedCell> cells;
    cells.reserve(prompt_len + input.size() + 16);

    for (char c : prompt_str) cells.push_back({(wchar_t)c, ATTR_PROMPT});

    size_t i = 0;
    while (i < input.length()) {
        unsigned char c = (unsigned char)input[i];

        // Classify the upcoming token
        WORD token_attr = ATTR_PUNCT;
        size_t token_start = i;
        size_t token_end   = i;

        if (c == '"') {
            // String literal: include opening, content, closing quote
            token_end = i + 1;
            while (token_end < input.length() && input[token_end] != '"') token_end++;
            if (token_end < input.length()) token_end++;
            token_attr = ATTR_STRING;
        }
        else if (c == '\'') {
            // Comment: rest of line
            token_end = input.length();
            token_attr = ATTR_COMMENT;
        }
        else if (std::isdigit(c)) {
            token_end = i;
            while (token_end < input.length() &&
                   (std::isdigit((unsigned char)input[token_end]) || input[token_end] == '.')) token_end++;
            token_attr = ATTR_NUMBER;
        }
        else if (std::isalpha(c) || c == '_' || c >= 128) {
            token_end = i;
            while (token_end < input.length() &&
                   (std::isalnum((unsigned char)input[token_end]) || input[token_end] == '_' ||
                    (unsigned char)input[token_end] >= 128)) token_end++;
            std::string word = input.substr(token_start, token_end - token_start);
            if (is_keyword(word))      token_attr = ATTR_KEYWORD;
            else if (is_native(word))  token_attr = ATTR_NATIVE;
            else                       token_attr = ATTR_IDENT;
        }
        else {
            token_end = i + 1;
            token_attr = ATTR_PUNCT;
        }

        // Convert the token bytes into wide codepoints and emit them
        int wlen = MultiByteToWideChar(CP_UTF8, 0, input.data() + token_start,
                                       (int)(token_end - token_start), nullptr, 0);
        if (wlen > 0) {
            std::wstring wtok(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, input.data() + token_start,
                                (int)(token_end - token_start), &wtok[0], wlen);
            for (wchar_t wc : wtok) cells.push_back({wc, token_attr});
        }
        i = token_end > token_start ? token_end : token_start + 1;
    }

    // Compute cursor visual offset in codepoints
    int cursor_cp = 0;
    for (int k = 0; k < state.cursor_pos && k < (int)input.length(); k++) {
        unsigned char ch = (unsigned char)input[k];
        if (ch <= 0x7F || ch >= 0xC0) cursor_cp++;
    }
    int cursor_visual = prompt_len + cursor_cp;

    int total_visual = (int)cells.size();
    int new_rows = (total_visual + cols - 1) / cols;
    if (new_rows < 1) new_rows = 1;
    int last_rows = (last_drawn_total_visual_len + cols - 1) / cols;
    if (last_rows < 1) last_rows = 1;
    int rows = std::max(new_rows, last_rows);
    if (rows < 1) rows = 1;

    // Anchor the redraw area. The cursor after the previous render sits at the
    // row offset `last_drawn / cols` below the start (integer division!). This
    // is different from `last_rows - 1` when last_drawn is an exact multiple of
    // cols — in that case the cursor wrapped to the start of a fresh row and
    // is one row further down.
    int cursor_row_offset = 0;
    if (last_drawn_total_visual_len > 0)
        cursor_row_offset = last_drawn_total_visual_len / cols;
    COORD startPos = { 0, (SHORT)(csbi.dwCursorPosition.Y - cursor_row_offset) };
    if (startPos.Y < 0) startPos.Y = 0;
    // Clip the vertical region to the visible buffer
    int max_rows_from_start = csbi.dwSize.Y - startPos.Y;
    if (max_rows_from_start < 1) max_rows_from_start = 1;
    if (rows > max_rows_from_start) rows = max_rows_from_start;

    // Build CHAR_INFO buffer filled with spaces
    std::vector<CHAR_INFO> buf((size_t)rows * (size_t)cols);
    for (auto& ci : buf) { ci.Char.UnicodeChar = L' '; ci.Attributes = ATTR_DEFAULT; }

    // Blit the tagged cells
    for (int k = 0; k < (int)cells.size() && k < rows * cols; k++) {
        buf[k].Char.UnicodeChar = cells[k].ch;
        buf[k].Attributes = cells[k].attr;
    }

    COORD bufSize  = { (SHORT)cols, (SHORT)rows };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT writeRegion = {
        0,
        startPos.Y,
        (SHORT)(cols - 1),
        (SHORT)(startPos.Y + rows - 1)
    };
    WriteConsoleOutputW(hOut, buf.data(), bufSize, bufCoord, &writeRegion);

    // Position the cursor
    COORD finalPos = {
        (SHORT)(cursor_visual % cols),
        (SHORT)(startPos.Y + (cursor_visual / cols))
    };
    if (finalPos.Y >= csbi.dwSize.Y) finalPos.Y = (SHORT)(csbi.dwSize.Y - 1);
    SetConsoleCursorPosition(hOut, finalPos);

    last_drawn_total_visual_len = total_visual;
    return;
#else
    // ── Linux / fallback: existing terminal-escape path ──────
    (void)last_drawn_total_visual_len;
    set_color(10, 0);
    print(prompt_str);

    const std::string& input = state.current_input;
    size_t i = 0;
    while (i < input.length()) {
        unsigned char c = (unsigned char)input[i];
        if (c == '"') {
            std::string token(1, c); i++;
            while (i < input.length() && input[i] != '"') { token += input[i++]; }
            if (i < input.length()) { token += input[i++]; }
            set_color(3, 0); print(token);
        } else if (std::isdigit(c)) {
            std::string token;
            while (i < input.length() && (std::isdigit((unsigned char)input[i]) || input[i] == '.')) { token += input[i++]; }
            set_color(6, 0); print(token);
        } else if (c == '\'') {
            std::string token;
            while (i < input.length()) { token += input[i++]; }
            set_color(8, 0); print(token);
        } else if (std::isalpha(c) || c == '_' || c >= 128) {
            std::string token;
            while (i < input.length() && (std::isalnum((unsigned char)input[i]) || input[i] == '_' || (unsigned char)input[i] >= 128)) { token += input[i++]; }
            int color_code = 2;
            if (is_keyword(token)) color_code = 5;
            else if (is_native(token)) color_code = 11;
            set_color(color_code, 0);
            print(token);
        } else {
            set_color(7, 0); print(std::string(1, (char)c)); i++;
        }
    }
    set_color(7, 0);
#endif
}

// ── RECUR task system ─────────────────────────────────────────

int Console::add_recur_task(int interval_ms, const std::string& code) {
    std::lock_guard<std::mutex> lock(recur_mutex);
    auto task = std::make_shared<RecurTask>();
    task->id = next_task_id++;
    task->interval_ms = interval_ms;
    task->code = code;
    task->next_run = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
    recur_tasks.push_back(task);
    return task->id;
}

void Console::clear_recur_task(int task_id) {
    std::lock_guard<std::mutex> lock(recur_mutex);
    recur_tasks.erase(
        std::remove_if(recur_tasks.begin(), recur_tasks.end(),
            [task_id](auto& t) { return t->id == task_id; }),
        recur_tasks.end());
}

void Console::list_recur_tasks() {
    std::lock_guard<std::mutex> lock(recur_mutex);
    if (recur_tasks.empty()) {
        std::cout << "No active RECUR tasks." << std::endl;
        return;
    }
    std::cout << "ID    Interval    Code" << std::endl;
    std::cout << "----  ----------  ----------------------------------------" << std::endl;
    for (auto& t : recur_tasks) {
        printf("%-4d  %7d ms  %s\n", t->id, t->interval_ms, t->code.c_str());
    }
}

void Console::process_recur_tasks() {
    std::lock_guard<std::mutex> lock(recur_mutex);
    auto now = std::chrono::steady_clock::now();
    for (auto& t : recur_tasks) {
        if (t->active && now >= t->next_run) {
            pending_executions.push_back(t->code);
            t->next_run = now + std::chrono::milliseconds(t->interval_ms);
        }
    }
}

// ── History persistence (per workspace) ──────────────────────

void Console::save_history() {
    std::ofstream out("jdbasic_history.txt");
    if (!out.is_open()) return;

    for (int w = 0; w < MAX_WORKSPACES; ++w) {
        out << "[WS" << w << "]\n";
        auto& hist = workspaces[w].console.history;
        for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
            out << *it << "\n";
        }
    }
    out.close();
}

void Console::load_history() {
    std::ifstream in("jdbasic_history.txt");
    if (!in.is_open()) return;

    std::string line;
    int current_ws = -1;

    while (std::getline(in, line)) {
        if (line.rfind("[WS", 0) == 0 && line.size() >= 5) {
            current_ws = line[3] - '0';
            if (current_ws < 0 || current_ws >= MAX_WORKSPACES) current_ws = -1;
            continue;
        }
        if (current_ws >= 0 && !line.empty()) {
            workspaces[current_ws].console.history.push_front(line);
        }
    }
    in.close();
}
