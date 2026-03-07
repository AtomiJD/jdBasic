#include "AppConfig.hpp"
#ifdef  JDREPL
#include "jdConsole.hpp"
#include "NeReLaBasic.hpp"
#include "TextIO.hpp"
#include "StringUtils.hpp"
#include "Compiler.hpp"
#include "Error.hpp"
#include <iostream>
#include "KeywordRepository.hpp"
#include <set>
#include <fstream>

#if defined(_WIN32)
#include <conio.h>
#include <windows.h>
#include <queue> // ADD THIS
#ifdef JDCOM
#include <comdef.h> 
#endif
static DWORD g_original_console_mode = 0;
static std::queue<int> g_utf8_buffer; // ADD THIS: Buffers multi-byte characters
static WCHAR g_high_surrogate = 0;    // ADD THIS: Tracks the first half of an emoji
#else
#include <termios.h>
#include <unistd.h>
#endif

jdConsole::jdConsole(NeReLaBasic& vm_ref) : vm(vm_ref) {
    for (int i = 0; i < MAX_WORKSPACES; ++i) {
        workspaces[i].main_function_table = vm.main_function_table;
    }
    load_state();
    enable_raw_mode();
}

jdConsole::~jdConsole() {
    save_state();
    disable_raw_mode();
}

void jdConsole::enable_raw_mode() {
#if defined(_WIN32)
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(hStdin, &g_original_console_mode)) {
        DWORD mode = g_original_console_mode;

        mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

        mode |= ENABLE_EXTENDED_FLAGS;
        mode |= ENABLE_WINDOW_INPUT; // <--- ADD THIS LINE
        mode &= ~ENABLE_QUICK_EDIT_MODE;

        SetConsoleMode(hStdin, mode);
    }
    DWORD out_mode = 0;
    if (GetConsoleMode(hStdout, &out_mode)) {
        out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hStdout, out_mode);
    }
#else
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

void jdConsole::disable_raw_mode() {
#if defined(_WIN32)
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hStdin, g_original_console_mode);
#else
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag |= (ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

int jdConsole::read_raw_key() {
#if defined(_WIN32)
    // 1. Drain any waiting UTF-8 bytes from multi-byte characters
    if (!g_utf8_buffer.empty()) {
        int c = g_utf8_buffer.front();
        g_utf8_buffer.pop();
        return c;
    }

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD ir;
    DWORD read;

    // 2. Poll for console input events
    if (PeekConsoleInputW(hStdin, &ir, 1, &read) && read > 0) {
        ReadConsoleInputW(hStdin, &ir, 1, &read); // Actually consume the event

        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            auto& ke = ir.Event.KeyEvent;

            // Check for Ctrl modifiers
            bool ctrl = (ke.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            if (ctrl) {
                switch (ke.wVirtualKeyCode) {
                case 'C': return KEY_CTRL_C;
                case 'V': return KEY_CTRL_V;
                case VK_LEFT: return KEY_CTRL_LEFT;
                case VK_RIGHT: return KEY_CTRL_RIGHT;
                }
            }

            // Map standard Virtual Keys to your REPL's KEY_* constants
            switch (ke.wVirtualKeyCode) {
            case VK_UP: return KEY_UP;
            case VK_DOWN: return KEY_DOWN;
            case VK_LEFT: return KEY_LEFT;
            case VK_RIGHT: return KEY_RIGHT;
            case VK_HOME: return KEY_HOME;
            case VK_END: return KEY_END;
            case VK_DELETE: return KEY_DELETE;
            case VK_BACK: return KEY_BACKSPACE;
            case VK_RETURN: return KEY_ENTER;
            case VK_TAB: return KEY_TAB;
            case VK_F1: return KEY_F1;
            case VK_F2: return KEY_F2;
            case VK_F3: return KEY_F3;
            case VK_F4: return KEY_F4;
            case VK_F5: return KEY_F5;
            case VK_F7: return KEY_F7;
            case VK_F8: return KEY_F8;
            case VK_ESCAPE: return KEY_ESC;
            }

            // 3. Handle Unicode Characters (UTF-16 -> UTF-8)
            WCHAR wch = ke.uChar.UnicodeChar;
            if (wch != 0) {
                std::wstring utf16_str;

                if (wch >= 0xD800 && wch <= 0xDBFF) {
                    // It's the first half of an emoji (High Surrogate). Wait for the next event.
                    g_high_surrogate = wch;
                    return 0;
                }
                else if (wch >= 0xDC00 && wch <= 0xDFFF) {
                    // It's the second half of an emoji (Low Surrogate). Combine them!
                    if (g_high_surrogate != 0) {
                        utf16_str += g_high_surrogate;
                        utf16_str += wch;
                        g_high_surrogate = 0;
                    }
                    else {
                        return 0; // Orphaned low surrogate, ignore.
                    }
                }
                else {
                    // Normal single-WCHAR character (ASCII, ÄÖÜ, etc)
                    utf16_str += wch;
                    g_high_surrogate = 0;
                }

                // Convert the assembled UTF-16 string into pure UTF-8 bytes
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16_str.c_str(), (int)utf16_str.length(), NULL, 0, NULL, NULL);
                std::string utf8_str(size_needed, 0);
                WideCharToMultiByte(CP_UTF8, 0, utf16_str.c_str(), (int)utf16_str.length(), &utf8_str[0], size_needed, NULL, NULL);

                // Push bytes into the buffer
                for (char c : utf8_str) {
                    g_utf8_buffer.push(static_cast<unsigned char>(c));
                }

                // Pop the very first byte to return immediately
                if (!g_utf8_buffer.empty()) {
                    int c = g_utf8_buffer.front();
                    g_utf8_buffer.pop();
                    return c;
                }
            }
        }
    }
    return 0; // Nothing to process on this tick
#else
    // POSIX escape sequence parsing (Simplified for brevity)
    char ch;
    if (read(STDIN_FILENO, &ch, 1) != 1) return 0;
    if (ch == 27) {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESC;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESC;

        if (seq[0] == '[') {
            switch (seq[1]) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            }
        }
        return KEY_ESC;
    }
    // Handle Enter/Backspace maps
    if (ch == 10 || ch == 13) return KEY_ENTER;
    if (ch == 127) return KEY_BACKSPACE;
    return ch;
#endif
}

void jdConsole::run() {
    // TextIO::print("jdBasic Advanced REPL Active. F1-F4 to switch workspaces.\n");
    render_prompt();

#if defined(_WIN32)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
#endif

    while (is_running) {
        bool original_nopause = vm.nopause_active;
        vm.nopause_active = true;
#if defined(_WIN32)
        GetConsoleScreenBufferInfo(hConsole, &csbi);
#else
        TextIO::print("\033[s"); // POSIX save cursor
#endif
        vm.process_system_events();
#if defined(_WIN32)
        SetConsoleCursorPosition(hConsole, csbi.dwCursorPosition);
#else
        TextIO::print("\033[u"); // POSIX restore cursor
#endif
        vm.nopause_active = original_nopause;

        int key = read_raw_key();
        if (key > 0) {
            process_key(key);
        }
    }
}

void jdConsole::process_key(int key) {
    ConsoleWorkspace& ws = workspaces[active_ws];

    if (key != KEY_F8) {
        ws.is_f8_searching = false;
    }
    if (key >= KEY_F1 && key <= KEY_F4) {
        int target_ws = key - KEY_F1;
        if (target_ws < MAX_WORKSPACES) {
            switch_workspace(target_ws);
        }
    }
    else if (key == KEY_F5) {
        // Clear whatever they were typing, stash it in history, and RUN
        if (!ws.current_input.empty()) {
            ws.history.push_front(ws.current_input);
        }
        ws.current_input = "RUN";
        ws.cursor_pos = 3;
        render_prompt();
        execute_current_line();
    }
    else if (key == KEY_F7) {
        show_history_f7();
    }
    else if (key == KEY_F8) {            
        search_history_f8();
    }
    else if (key == KEY_CTRL_C) {
        // Copy the current command line buffer
        copy_to_clipboard(ws.current_input);
    }
    else if (key == KEY_CTRL_V) {
        // Paste at the current cursor position
        std::string clip = get_from_clipboard();

        // Strip carriage returns and newlines so it stays on one line
        clip.erase(std::remove(clip.begin(), clip.end(), '\r'), clip.end());
        clip.erase(std::remove(clip.begin(), clip.end(), '\n'), clip.end());

        ws.current_input.insert(ws.cursor_pos, clip);
        ws.cursor_pos += clip.length();
        render_prompt();
    }
    else if (key == KEY_TAB) {           // <-- Catch TAB here
        handle_autocomplete();
    }
    else if (key == KEY_ENTER) {
        execute_current_line();
    }
    else if (key == KEY_UP) {
        navigate_history(1);  // Go to older commands
    }
    else if (key == KEY_DOWN) {
        navigate_history(-1); // Go to newer commands
    }
    else if (key == KEY_BACKSPACE) {
        if (ws.cursor_pos > 0) {
            int start = ws.cursor_pos - 1;
            // Step back over UTF-8 continuation bytes (10xxxxxx -> 0x80 to 0xBF)
            while (start > 0 && (static_cast<unsigned char>(ws.current_input[start]) & 0xC0) == 0x80) {
                start--;
            }
            ws.current_input.erase(start, ws.cursor_pos - start);
            ws.cursor_pos = start;
            render_prompt();
        }
    }
    else if (key == KEY_LEFT) {
        if (ws.cursor_pos > 0) {
            ws.cursor_pos--;
            // Step back over UTF-8 continuation bytes
            while (ws.cursor_pos > 0 && (static_cast<unsigned char>(ws.current_input[ws.cursor_pos]) & 0xC0) == 0x80) {
                ws.cursor_pos--;
            }
            render_prompt();
        }
    }
    else if (key == KEY_RIGHT) {
        if (ws.cursor_pos < ws.current_input.length()) {
            ws.cursor_pos++;
            // Step forward over UTF-8 continuation bytes
            while (ws.cursor_pos < ws.current_input.length() && (static_cast<unsigned char>(ws.current_input[ws.cursor_pos]) & 0xC0) == 0x80) {
                ws.cursor_pos++;
            }
            render_prompt();
        }
    }
    else if (key == KEY_HOME) {
        ws.cursor_pos = 0;
        render_prompt();
    }
    else if (key == KEY_END) {
        ws.cursor_pos = ws.current_input.length();
        render_prompt();
    }
    else if (key == KEY_DELETE) {
        if (ws.cursor_pos < ws.current_input.length()) {
            int end = ws.cursor_pos + 1;
            // Step forward over UTF-8 continuation bytes
            while (end < ws.current_input.length() && (static_cast<unsigned char>(ws.current_input[end]) & 0xC0) == 0x80) {
                end++;
            }
            ws.current_input.erase(ws.cursor_pos, end - ws.cursor_pos);
            render_prompt();
        }
    }
    else if (key == KEY_CTRL_LEFT) {
        auto move_left = [&]() {
            if (ws.cursor_pos > 0) {
                ws.cursor_pos--;
                while (ws.cursor_pos > 0 && (static_cast<unsigned char>(ws.current_input[ws.cursor_pos]) & 0xC0) == 0x80) ws.cursor_pos--;
            }
            };
        // Skip spaces backwards
        while (ws.cursor_pos > 0 && std::isspace((unsigned char)ws.current_input[ws.cursor_pos - 1])) move_left();
        // Skip characters backwards
        while (ws.cursor_pos > 0 && !std::isspace((unsigned char)ws.current_input[ws.cursor_pos - 1])) move_left();
        render_prompt();
        }
    else if (key == KEY_CTRL_RIGHT) {
            auto move_right = [&]() {
                if (ws.cursor_pos < ws.current_input.length()) {
                    ws.cursor_pos++;
                    while (ws.cursor_pos < ws.current_input.length() && (static_cast<unsigned char>(ws.current_input[ws.cursor_pos]) & 0xC0) == 0x80) ws.cursor_pos++;
                }
                };
            // Skip characters forwards
            while (ws.cursor_pos < ws.current_input.length() && !std::isspace((unsigned char)ws.current_input[ws.cursor_pos])) move_right();
            // Skip spaces forwards
            while (ws.cursor_pos < ws.current_input.length() && std::isspace((unsigned char)ws.current_input[ws.cursor_pos])) move_right();
            render_prompt();
        }
    else if (key >= 32 && key <= 255) { // Allow extended ASCII and UTF-8 byte segments
        ws.current_input.insert(ws.cursor_pos++, 1, static_cast<char>(key));
#if defined(_WIN32)
        if (g_utf8_buffer.empty()) render_prompt();
#else
        render_prompt();
#endif
    }
}

void jdConsole::switch_workspace(int index) {
    if (active_ws == index) return; // Do nothing if pressing the same F-key

#if defined(_WIN32)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    // 1. SNAPSHOT the current workspace's screen
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        ConsoleWorkspace& old_ws = workspaces[active_ws];

        old_ws.screen_size = csbi.dwSize;
        old_ws.win32_cursor_pos = csbi.dwCursorPosition;
        old_ws.color_attrs = csbi.wAttributes;

        // Resize the vector to hold the exact width * height of the console memory
        old_ws.screen_buffer.resize(csbi.dwSize.X * csbi.dwSize.Y);

        COORD bufferCoord = { 0, 0 };
        SMALL_RECT readRegion = { 0, 0, (SHORT)(csbi.dwSize.X - 1), (SHORT)(csbi.dwSize.Y - 1) };

        // Read the character and color data directly from the video buffer
        ReadConsoleOutput(hConsole, old_ws.screen_buffer.data(), csbi.dwSize, bufferCoord, &readRegion);
    }
#endif

    // 1. Save the current VM state into the OUTGOING workspace
    workspaces[active_ws].variables = vm.variables;
    workspaces[active_ws].source_lines = vm.source_lines;
    workspaces[active_ws].program_p_code = vm.program_p_code;
    workspaces[active_ws].main_function_table = vm.main_function_table;
    workspaces[active_ws].user_defined_types = vm.user_defined_types;

    // 2. Switch the active ID
    active_ws = index;
    vm.current_workspace = active_ws;
    ConsoleWorkspace& new_ws = workspaces[active_ws];

    // 3. RESTORE the new workspace's screen
#if defined(_WIN32)
    if (new_ws.screen_buffer.empty()) {
        // If it's empty, this workspace has never been rendered. Just clear the screen.
        vm.init_screen();
        //TextIO::clearScreen();
    }
    else {
        COORD bufferCoord = { 0, 0 };
        SMALL_RECT writeRegion = { 0, 0, (SHORT)(new_ws.screen_size.X - 1), (SHORT)(new_ws.screen_size.Y - 1) };

        // Blast the saved character and color data back onto the screen
        WriteConsoleOutput(hConsole, new_ws.screen_buffer.data(), new_ws.screen_size, bufferCoord, &writeRegion);

        // Restore the physical cursor and colors exactly where they were
        SetConsoleCursorPosition(hConsole, new_ws.win32_cursor_pos);
        SetConsoleTextAttribute(hConsole, new_ws.color_attrs);
    }
#else
    // POSIX Fallback: Simply clear the screen since cross-platform buffer reading is not natively supported
    TextIO::clearScreen();
#endif

    // 3. Inject the INCOMING workspace's state back into the VM
    vm.variables = workspaces[active_ws].variables;
    vm.source_lines = workspaces[active_ws].source_lines;
    vm.program_p_code = workspaces[active_ws].program_p_code;
    vm.main_function_table = workspaces[active_ws].main_function_table;
    vm.user_defined_types = workspaces[active_ws].user_defined_types;

    // Reset history scrolling
    workspaces[active_ws].history_idx = -1;

    //TextIO::nl();
    //TextIO::print("--- Switched to Workspace " + std::to_string(active_ws + 1) + " ---");
    //TextIO::nl();
    render_prompt();
}

void jdConsole::navigate_history(int direction) {
    ConsoleWorkspace& ws = workspaces[active_ws];
    if (ws.history.empty()) return;

    ws.history_idx += direction;

    // Clamp the index
    if (ws.history_idx < 0) {
        ws.history_idx = -1;
        ws.current_input = ""; // Blank line when returning to the bottom
    }
    else if (ws.history_idx >= (int)ws.history.size()) {
        ws.history_idx = (int)ws.history.size() - 1;
    }
    else {
        ws.current_input = ws.history[ws.history_idx];
    }

    ws.cursor_pos = ws.current_input.length();
    render_prompt();
}

void jdConsole::show_history_f7() {
    ConsoleWorkspace& ws = workspaces[active_ws];
    if (ws.history.empty()) return;

    // Capture original console colors
#if defined(_WIN32)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD original_colors = 7;
    if (GetConsoleScreenBufferInfo(hConsole, &consoleInfo)) {
        original_colors = consoleInfo.wAttributes;
    }
#endif

    int selected_idx = 0;
    bool selecting = true;
    int display_count = std::min(20, (int)ws.history.size());

    // NEW: Flag to track when we actually need to update the screen
    bool needs_redraw = true;

    while (selecting) {
        // Yield to background tasks so RECUR keeps ticking while the menu is open!
        bool original_nopause = vm.nopause_active;
        vm.nopause_active = true;
        vm.process_system_events();
        vm.nopause_active = original_nopause;

        // Only draw if the selection changed
        if (needs_redraw) {
            TextIO::clearScreen();
            TextIO::setColor(0, 7);
            TextIO::print(" === Workspace " + std::to_string(active_ws + 1) + " History (F7) === ");
            TextIO::setColor(7, 0);
            TextIO::nl();
            TextIO::print(" [UP/DOWN]: Select | [ENTER]: Confirm | [ESC]: Cancel");
            TextIO::nl(); TextIO::nl();

            for (int i = 0; i < display_count; ++i) {
                if (i == selected_idx) {
                    TextIO::setColor(10, 0); // Bright green
                    TextIO::print(" > " + ws.history[i]);
                    TextIO::setColor(7, 0);
                }
                else {
                    TextIO::print("   " + ws.history[i]);
                }
                TextIO::nl();
            }
            needs_redraw = false; // Reset the flag
        }

        // Check for keys (non-blocking)
        int key = read_raw_key();

        if (key > 0) {
            if (key == KEY_UP && selected_idx > 0) {
                selected_idx--;
                needs_redraw = true; // Selection changed, trigger a redraw!
            }
            else if (key == KEY_DOWN && selected_idx < display_count - 1) {
                selected_idx++;
                needs_redraw = true; // Selection changed, trigger a redraw!
            }
            else if (key == KEY_ENTER) {
                ws.current_input = ws.history[selected_idx];
                ws.cursor_pos = ws.current_input.length();
                selecting = false;
            }
            else if (key == KEY_ESC) {
                selecting = false;
            }
        }
        else {
            // Sleep briefly to prevent high CPU usage while waiting for input
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    TextIO::clearScreen();

    // Restore original console colors
#if defined(_WIN32)
    SetConsoleTextAttribute(hConsole, original_colors);
#else
    std::cout << "\033[0m";
#endif

    render_prompt();
}

void jdConsole::search_history_f8() {
    ConsoleWorkspace& ws = workspaces[active_ws];
    if (ws.history.empty()) return;

    // 1. If we just started searching, lock in the current input as the prefix
    if (!ws.is_f8_searching) {
        ws.history_search_prefix = ws.current_input;
        ws.is_f8_searching = true;
    }

    // 2. Search backwards from the current history position
    int start_idx = ws.history_idx + 1;
    bool found = false;

    for (int i = start_idx; i < (int)ws.history.size(); ++i) {
        // If prefix is empty, or if the history string starts with the prefix
        if (ws.history_search_prefix.empty() ||
            ws.history[i].rfind(ws.history_search_prefix, 0) == 0) {

            ws.history_idx = i;
            ws.current_input = ws.history[i];
            ws.cursor_pos = ws.current_input.length();
            found = true;
            break;
        }
    }

    // 3. If we hit the end of the history and didn't find it, wrap around to the beginning!
    if (!found) {
        ws.history_idx = -1; // Reset to start
        for (int i = 0; i < start_idx && i < (int)ws.history.size(); ++i) {
            if (ws.history_search_prefix.empty() ||
                ws.history[i].rfind(ws.history_search_prefix, 0) == 0) {

                ws.history_idx = i;
                ws.current_input = ws.history[i];
                ws.cursor_pos = ws.current_input.length();
                found = true;
                break;
            }
        }
    }

    // 4. If nothing matches at all, keep the prefix on the screen
    if (!found) {
        ws.current_input = ws.history_search_prefix;
        ws.cursor_pos = ws.current_input.length();
    }

    render_prompt();
}

void jdConsole::save_state() {
    std::ofstream out("history.jsws"); // Renamed to clarify it only holds history
    if (!out.is_open()) return;

    for (int i = 0; i < MAX_WORKSPACES; ++i) {
        // Save History (Iterating backwards to keep chronological order in the file)
        out << "[WS" << i << "_HISTORY]\n";
        for (auto it = workspaces[i].history.rbegin(); it != workspaces[i].history.rend(); ++it) {
            out << *it << "\n";
        }
    }
    out.close();
}

void jdConsole::load_state() {
    std::ifstream in("history.jsws");
    if (!in.is_open()) return;

    std::string line;
    int current_ws = -1;

    while (std::getline(in, line)) {
        // Check for section headers
        if (line.rfind("[WS", 0) == 0) {
            size_t underscore = line.find('_');
            if (underscore != std::string::npos) {
                current_ws = std::stoi(line.substr(3, underscore - 3));
            }
            continue;
        }

        // Add history item to the correct workspace
        if (current_ws >= 0 && current_ws < MAX_WORKSPACES && !line.empty()) {
            workspaces[current_ws].history.push_front(line);
        }
    }
    in.close();
}

void jdConsole::handle_autocomplete() {
    ConsoleWorkspace& ws = workspaces[active_ws];
    if (ws.current_input.empty() || ws.cursor_pos == 0) return;

    // 1. Extract the word behind the cursor. 
    // NEW: Allow '{' and '"' to be part of the token for Map syntax!
    int start = ws.cursor_pos - 1;
    while (start >= 0 && (std::isalnum((unsigned char)ws.current_input[start]) ||
        ws.current_input[start] == '.' ||
        ws.current_input[start] == '_' ||
        ws.current_input[start] == '{' ||
        ws.current_input[start] == '"')) {
        start--;
    }
    start++;

    std::string token = ws.current_input.substr(start, ws.cursor_pos - start);
    if (token.empty()) return;

    std::set<std::string> matches;
    std::string prefix_to_replace = token;
    bool is_map_completion = false; // Track this so we can auto-close the bracket later

    // 2. Determine Context: Map vs COM vs General Syntax
    size_t brace_pos = token.find("{\"");
    size_t dot_pos = token.find_last_of('.');

    if (brace_pos != std::string::npos) {
        // --- CONTEXT A: jdBasic Map Completion ---
        is_map_completion = true;
        std::string obj_name = StringUtils::to_upper(token.substr(0, brace_pos));
        std::string prop_prefix = token.substr(brace_pos + 2); // Extract everything after {"

        // If the user already typed the closing quote, ignore it for the search
        if (!prop_prefix.empty() && prop_prefix.back() == '"') {
            prop_prefix.pop_back();
        }

        prefix_to_replace = prop_prefix;

        auto it = vm.variables.find(obj_name);
        if (it != vm.variables.end() && std::holds_alternative<std::shared_ptr<Map>>(it->second)) {
            auto map_ptr = std::get<std::shared_ptr<Map>>(it->second);
            if (map_ptr) {
                for (const auto& pair : map_ptr->data) {
                    // Match the key (case-insensitive for convenience)
                    if (prop_prefix.empty() || StringUtils::to_upper(pair.first).rfind(StringUtils::to_upper(prop_prefix), 0) == 0) {
                        matches.insert(pair.first);
                    }
                }
            }
        }
    }
    else if (dot_pos != std::string::npos) {
        // --- CONTEXT B: COM Object Completion ---
        std::string obj_name = StringUtils::to_upper(token.substr(0, dot_pos));
        std::string prop_prefix = StringUtils::to_upper(token.substr(dot_pos + 1));
        prefix_to_replace = token.substr(dot_pos + 1);

        auto it = vm.variables.find(obj_name);
#ifdef JDCOM
        if (it != vm.variables.end() && std::holds_alternative<ComObject>(it->second)) {
            IDispatch* pDisp = std::get<ComObject>(it->second).ptr;
            if (pDisp) {
                ITypeInfo* pTypeInfo = nullptr;
                if (SUCCEEDED(pDisp->GetTypeInfo(0, LOCALE_USER_DEFAULT, &pTypeInfo))) {
                    TYPEATTR* pTypeAttr = nullptr;
                    if (SUCCEEDED(pTypeInfo->GetTypeAttr(&pTypeAttr))) {

                        for (int i = 0; i < pTypeAttr->cFuncs; ++i) {
                            FUNCDESC* pFuncDesc = nullptr;
                            if (SUCCEEDED(pTypeInfo->GetFuncDesc(i, &pFuncDesc))) {
                                BSTR bstrName = nullptr;
                                UINT cNames = 0;
                                if (SUCCEEDED(pTypeInfo->GetNames(pFuncDesc->memid, &bstrName, 1, &cNames)) && bstrName) {
                                    std::string name = (const char*)_bstr_t(bstrName);
                                    SysFreeString(bstrName);
                                    if (prop_prefix.empty() || StringUtils::to_upper(name).rfind(prop_prefix, 0) == 0) {
                                        matches.insert(name);
                                    }
                                }
                                pTypeInfo->ReleaseFuncDesc(pFuncDesc);
                            }
                        }

                        for (int i = 0; i < pTypeAttr->cVars; ++i) {
                            VARDESC* pVarDesc = nullptr;
                            if (SUCCEEDED(pTypeInfo->GetVarDesc(i, &pVarDesc))) {
                                BSTR bstrName = nullptr;
                                UINT cNames = 0;
                                if (SUCCEEDED(pTypeInfo->GetNames(pVarDesc->memid, &bstrName, 1, &cNames)) && bstrName) {
                                    std::string name = (const char*)_bstr_t(bstrName);
                                    SysFreeString(bstrName);
                                    if (prop_prefix.empty() || StringUtils::to_upper(name).rfind(prop_prefix, 0) == 0) {
                                        matches.insert(name);
                                    }
                                }
                                pTypeInfo->ReleaseVarDesc(pVarDesc);
                            }
                        }
                        pTypeInfo->ReleaseTypeAttr(pTypeAttr);
                    }
                    pTypeInfo->Release();
                }
            }
        }
#endif
    }
    else {
        // --- CONTEXT C: General Syntax Completion ---
        std::string upper_token = StringUtils::to_upper(token);

        for (const auto& pair : vm.variables) {
            if (StringUtils::to_upper(pair.first).rfind(upper_token, 0) == 0) {
                matches.insert(pair.first);
            }
        }

        for (const auto& pair : vm.main_function_table) {
            if (StringUtils::to_upper(pair.first).rfind(upper_token, 0) == 0) {
                matches.insert(pair.first);
            }
        }

        for (const auto& kw : KeywordRepository::get_all_keywords()) {
            if (kw.rfind(upper_token, 0) == 0) {
                matches.insert(kw);
            }
        }
    }

    // 3. Handle the Results
    if (matches.empty()) {
        return;
    }
    else if (matches.size() == 1) {
        std::string match_str = *matches.begin();
        std::string completion = match_str.substr(prefix_to_replace.length());

        // NEW: If it's a Map, automatically close the string and bracket!
        if (is_map_completion) {
            completion += "\"}";
        }

        ws.current_input.insert(ws.cursor_pos, completion);
        ws.cursor_pos += completion.length();
        render_prompt();
    }
    else {
        // Bounded Multiple Match Output
        TextIO::nl();
        TextIO::setColor(14, 0);

        int count = 0;
        const int MAX_DISPLAY = 40;
        int total_matches = matches.size();

        for (const auto& m : matches) {
            if (count >= MAX_DISPLAY) break;
            TextIO::print(m + "  ");
            if (++count % 5 == 0) TextIO::nl();
        }

        if (total_matches > MAX_DISPLAY) {
            if (count % 5 != 0) TextIO::nl();
            TextIO::setColor(8, 0);
            TextIO::print("... and " + std::to_string(total_matches - MAX_DISPLAY) + " more. Type more letters to filter.");
        }

        TextIO::setColor(7, 0);
        if (total_matches <= MAX_DISPLAY && count % 5 != 0) {
            TextIO::nl();
        }
        else if (total_matches > MAX_DISPLAY) {
            TextIO::nl();
        }

        render_prompt();
    }
}

void jdConsole::copy_to_clipboard(const std::string& text) {
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

std::string jdConsole::get_from_clipboard() {
    std::string result = "";
#if defined(_WIN32)
    if (OpenClipboard(NULL)) {
        HANDLE hg = GetClipboardData(CF_TEXT);
        if (hg) {
            char* str = static_cast<char*>(GlobalLock(hg));
            if (str) {
                result = str;
                GlobalUnlock(hg);
            }
        }
        CloseClipboard();
    }
#endif
    return result;
}

void jdConsole::render_prompt() {
    std::cout.clear(); // Clear any fail states caused by console flushing
    ConsoleWorkspace& ws = workspaces[active_ws];
    static int last_drawn_len = 0;

    std::string prompt = "WS" + std::to_string(active_ws + 1) + "> ";
    std::string full_line = prompt + ws.current_input;

#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi_start;
    GetConsoleScreenBufferInfo(hOut, &csbi_start);

    // Move physical cursor to column 0 (replaces \r)
    COORD startPos = { 0, csbi_start.dwCursorPosition.Y };
    SetConsoleCursorPosition(hOut, startPos);
#else
    TextIO::print("\r");
#endif

    // 1. Draw Prompt
    TextIO::setColor(10, 0);
    TextIO::print(prompt);

    // 2. Draw Input Text (Mini-Lexer)
    std::string input = ws.current_input;
    for (size_t i = 0; i < input.length(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c == '"') {
            std::string token(1, c);
            i++;
            while (i < input.length() && input[i] != '"') token += input[i++];
            if (i < input.length()) token += input[i++];
            TextIO::setColor(3, 0);
            TextIO::print(token);
        }
        else if (std::isdigit(c)) {
            std::string token;
            while (i < input.length() && (std::isdigit((unsigned char)input[i]) || input[i] == '.')) token += input[i++];
            TextIO::setColor(6, 0);
            TextIO::print(token);
        }
        else if (std::isalpha(c) || c == '_' || c >= 128) {
            std::string token;
            while (i < input.length() && (std::isalnum((unsigned char)input[i]) || input[i] == '_' || input[i] == '.' || static_cast<unsigned char>(input[i]) >= 128)) {
                token += input[i++];
            }
            if (KeywordRepository::is_keyword(token)) TextIO::setColor(5, 0);
            else TextIO::setColor(2, 0);
            TextIO::print(token);
        }
        else {
            std::string s(1, c);
            if (std::isspace(c)) TextIO::setColor(7, 0);
            else TextIO::setColor(2, 0);
            TextIO::print(s);
            i++;
        }
    }

    TextIO::setColor(2, 0);

    // 3. Erase trailing characters safely
    int current_visual_len = 0;
    for (char c : full_line) {
        if (static_cast<unsigned char>(c) <= 0x7F || static_cast<unsigned char>(c) >= 0xC0) {
            current_visual_len++;
        }
    }

    if (last_drawn_len > current_visual_len) {
        int diff = last_drawn_len - current_visual_len;
        TextIO::print(std::string(diff, ' '));

#if !defined(_WIN32)
        // POSIX fallback needs to step back over the spaces manually
        TextIO::print(std::string(diff, '\b'));
#endif
    }
    last_drawn_len = current_visual_len;

    // 4. Move cursor to actual visual position
#if defined(_WIN32)
    // Calculate visual characters strictly from start of input to the cursor index
    int visual_offset = 0;
    for (size_t i = 0; i < ws.cursor_pos && i < ws.current_input.length(); i++) {
        unsigned char c = ws.current_input[i];
        if (c <= 0x7F || c >= 0xC0) visual_offset++;
    }

    int new_x = startPos.X + prompt.length() + visual_offset;
    int new_y = startPos.Y;

    CONSOLE_SCREEN_BUFFER_INFO csbi_current;
    GetConsoleScreenBufferInfo(hOut, &csbi_current);

    // Handle line wrapping in the console
    while (new_x >= csbi_current.dwSize.X) {
        new_x -= csbi_current.dwSize.X;
        new_y++;
    }

    COORD newPos = { (SHORT)new_x, (SHORT)new_y };
    SetConsoleCursorPosition(hOut, newPos);
#else
    // POSIX fallback using \b
    if (ws.cursor_pos < ws.current_input.length()) {
        int move_back = 0;
        for (size_t i = ws.cursor_pos; i < ws.current_input.length(); i++) {
            unsigned char c = ws.current_input[i];
            if (c <= 0x7F || c >= 0xC0) move_back++;
        }
        TextIO::print(std::string(move_back, '\b'));
    }
#endif
}

void jdConsole::execute_current_line() {
    //is_running = false;
    ConsoleWorkspace& ws = workspaces[active_ws];
    std::string cmd = ws.current_input;
    StringUtils::trim(cmd);

    TextIO::nl();

    if (!cmd.empty()) {
        if (StringUtils::to_upper(cmd) == "EXIT") {
            is_running = false;
            return;
        }

        // --- Ported RESUME logic from the old REPL ---
        if (StringUtils::to_upper(cmd) == "RESUME") {
            if (vm.is_stopped) {
                TextIO::print("Resuming..."); TextIO::nl();
                vm.is_stopped = false;
                disable_raw_mode();
                vm.execute_main_program(vm.program_p_code, true);
                enable_raw_mode();
                if (Error::get() != 0) Error::print();
            }
            else {
                TextIO::print("?Nothing to resume."); TextIO::nl();
            }
            ws.current_input.clear();
            ws.cursor_pos = 0;
            render_prompt();
            return;
        }

        ws.history.push_front(cmd);

        // --- Setup VM state exactly like the old REPL ---
        extern NeReLaBasic* g_vm_instance_ptr;
        g_vm_instance_ptr = &vm;
        Error::clear();
        vm.direct_p_code.clear();
        vm.linenr = 0;
        vm.active_function_table = &vm.main_function_table;

        // Pass to the VM for compilation and execution
        if (vm.compiler->tokenize(vm, cmd, 0, vm.direct_p_code, *vm.active_function_table, false, true) == 0) {
            disable_raw_mode();
            vm.execute_synchronous_block(vm.direct_p_code);
            enable_raw_mode();
            if (vm.program_ended) {
                Error::clear();
                vm.program_ended = false;
            }
        }

        if (Error::get() != 0) {
            Error::print();
        }
    }

    ws.current_input.clear();
    ws.cursor_pos = 0;
    ws.history_idx = -1;
    //is_running = true;
    render_prompt();
}
#endif //  JDREPL