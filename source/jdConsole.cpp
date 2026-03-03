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
#ifdef JDCOM
#include <comdef.h> // Required for _bstr_t string conversions in COM
#endif
static DWORD g_original_console_mode = 0;
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
    if (GetConsoleMode(hStdin, &g_original_console_mode)) {
        DWORD mode = g_original_console_mode;

        // DISABLE:
        // - PROCESSED_INPUT: Stops Windows from hijacking Ctrl+C and Ctrl+V
        // - QUICK_EDIT_MODE: Stops mouse clicks from pausing the console
        // - LINE_INPUT & ECHO_INPUT: Standard raw mode
        mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

        // QuickEdit requires ENABLE_EXTENDED_FLAGS to be active to modify it
        mode |= ENABLE_EXTENDED_FLAGS;
        mode &= ~ENABLE_QUICK_EDIT_MODE;

        SetConsoleMode(hStdin, mode);
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
    if (!_kbhit()) {
        return 0;
    }
    int ch = _getch();
    if (ch == 0 || ch == 224) { // Extended keys
        int ext = _getch();
        switch (ext) {
        case 72: return KEY_UP;
        case 80: return KEY_DOWN;
        case 75: return KEY_LEFT;
        case 77: return KEY_RIGHT;
        case 71: return KEY_HOME;       // Pos1
        case 79: return KEY_END;        
        case 83: return KEY_DELETE;     
        case 115: return KEY_CTRL_LEFT; 
        case 116: return KEY_CTRL_RIGHT;
        case 59: return KEY_F1;
        case 60: return KEY_F2;
        case 61: return KEY_F3;
        case 62: return KEY_F4;
        case 63: return KEY_F5;
        case 65: return KEY_F7;
        case 66: return KEY_F8;
        default: return 0;
        }
    }
    return ch;
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
                // Add F-keys parsing depending on terminal emulator
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
            ws.current_input.erase(--ws.cursor_pos, 1);
            render_prompt();
        }
    }
    else if (key == KEY_LEFT) {
        if (ws.cursor_pos > 0) { ws.cursor_pos--; render_prompt(); }
    }
    else if (key == KEY_RIGHT) {
        if (ws.cursor_pos < ws.current_input.length()) { ws.cursor_pos++; render_prompt(); }
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
            ws.current_input.erase(ws.cursor_pos, 1);
            render_prompt();
        }
    }
    else if (key == KEY_CTRL_LEFT) {
        // Skip spaces backwards
        while (ws.cursor_pos > 0 && std::isspace(ws.current_input[ws.cursor_pos - 1])) {
            ws.cursor_pos--;
        }
        // Skip characters backwards until we hit a space
        while (ws.cursor_pos > 0 && !std::isspace(ws.current_input[ws.cursor_pos - 1])) {
            ws.cursor_pos--;
        }
        render_prompt();
    }
    else if (key == KEY_CTRL_RIGHT) {
        int len = ws.current_input.length();
        // Skip characters forwards until we hit a space
        while (ws.cursor_pos < len && !std::isspace(ws.current_input[ws.cursor_pos])) {
            ws.cursor_pos++;
        }
        // Skip spaces forwards
        while (ws.cursor_pos < len && std::isspace(ws.current_input[ws.cursor_pos])) {
            ws.cursor_pos++;
        }
        render_prompt();
    }
    else if (key >= 32 && key <= 126) { // Printable characters
        ws.current_input.insert(ws.cursor_pos++, 1, static_cast<char>(key));
        render_prompt();
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
    while (start >= 0 && (std::isalnum(ws.current_input[start]) ||
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
    ConsoleWorkspace& ws = workspaces[active_ws];
    static int last_drawn_len = 0;

    std::string prompt = "WS" + std::to_string(active_ws + 1) + "> ";
    std::string full_line = prompt + ws.current_input;

    // 1. Return to start of line
    TextIO::print("\r");

    // 2. Draw Prompt in Green
    TextIO::setColor(10, 0);
    TextIO::print(prompt);

    // 3. Mini-Lexer for Real-Time Syntax Highlighting
    std::string input = ws.current_input;
    for (size_t i = 0; i < input.length(); ) {
        char c = input[i];

        if (c == '"') {
            // --- STRINGS (Yellow) ---
            std::string token(1, c);
            i++;
            while (i < input.length() && input[i] != '"') {
                token += input[i++];
            }
            if (i < input.length()) token += input[i++]; // Include closing quote

            TextIO::setColor(3, 0);
            TextIO::print(token);
        }
        else if (std::isdigit(c)) {
            // --- NUMBERS (Magenta) ---
            std::string token;
            while (i < input.length() && (std::isdigit(input[i]) || input[i] == '.')) {
                token += input[i++];
            }

            TextIO::setColor(6, 0);
            TextIO::print(token);
        }
        else if (std::isalpha(c) || c == '_') {
            // --- WORDS (Keywords & Variables) ---
            std::string token;
            // Allow dots for modular keywords like "SPRITE.LOAD"
            while (i < input.length() && (std::isalnum(input[i]) || input[i] == '_' || input[i] == '.')) {
                token += input[i++];
            }

            // Check against your Keyword Repository
            if (KeywordRepository::is_keyword(token)) {
                TextIO::setColor(5, 0); // Cyan for Keywords
            }
            else {
                TextIO::setColor(2, 0);  // Standard Gray/White for Variables
            }
            TextIO::print(token);
        }
        else {
            // --- SYMBOLS & WHITESPACE (Dark Gray / Default) ---
            std::string s(1, c);
            if (std::isspace(c)) {
                TextIO::setColor(7, 0);
            }
            else {
                TextIO::setColor(2, 0); // Dark Gray for operators like =, +, ( )
            }
            TextIO::print(s);
            i++;
        }
    }

    // Reset to default color
    TextIO::setColor(2, 0);

    // 4. Erase trailing leftover characters ONLY if the line shrank (Backspace handling)
    int current_len = full_line.length();
    if (last_drawn_len > current_len) {
        int diff = last_drawn_len - current_len;
        TextIO::print(std::string(diff, ' '));
        TextIO::print(std::string(diff, '\b'));
    }
    last_drawn_len = current_len;

    // 5. Move cursor left to the correct logical editing position
    if (ws.cursor_pos < ws.current_input.length()) {
        int move_back = ws.current_input.length() - ws.cursor_pos;
        TextIO::print(std::string(move_back, '\b'));
    }
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
                vm.execute_main_program(vm.program_p_code, true);
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
            vm.execute_synchronous_block(vm.direct_p_code);

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