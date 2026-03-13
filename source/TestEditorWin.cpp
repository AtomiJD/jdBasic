#ifdef _WIN32
#include "NeReLaBasic.hpp"
#include "TextEditor.hpp"
#include "KeywordRepository.hpp"
#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <iostream>

class TextEditorWinImpl {
public:
    TextEditorWinImpl(std::vector<std::string>& lines, const std::string& fname)
        : lines_ref(lines), filename(fname) {
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(hOut, &csbi);
        screen_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top;
        screen_rows -= 2; // leave space for status

        if (lines_ref.empty()) lines_ref.push_back("");

#ifdef PYTHON
        // Detect Python extension
        if (filename.length() >= 3 && filename.substr(filename.length() - 3) == ".py") {
            is_python = true;
        }
#endif

    }

    void run();

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int screen_cols, screen_rows;
    int cx = 0, cy = 0, top_row = 0, left_col = 0;;
    int visual_cx = 0;
    bool file_modified = false;
    bool overwrite_mode = false;
    std::wstring last_search_query;

#ifdef PYTHON
    bool is_python = false;
#endif

    void draw_screen();
    void draw_line(const std::string& line, int row, int line_index);
    void move_cursor(int dx, int dy);
    void save_file();
    void find_text();
    void go_to_line();
    void paste_from_clipboard();
    std::wstring prompt(const std::wstring& msg);

    void clear_screen();
    int calculate_visual_cx(int line_idx, int char_pos);
    void set_cursor(int row, int col);
    void write_status(const std::wstring& msg);

    std::wstring string_to_wstring(const std::string& str) {
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstrTo[0], size_needed);
        wstrTo.pop_back();
        return wstrTo;
    }

    std::string wstring_to_string(const std::wstring& wstr) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &strTo[0], size_needed, NULL, NULL);
        strTo.pop_back();
        return strTo;
    }
    struct EditorState {
        std::vector<std::string> lines;
        int cx, cy;
    };

    std::vector<EditorState> undo_stack;
    std::vector<EditorState> redo_stack;

    void save_state();
    void undo();
    void redo();

    bool is_selecting = false;
    int sel_cx = 0, sel_cy = 0;

    bool has_selection() const;
    void get_normalized_selection(int& start_x, int& start_y, int& end_x, int& end_y) const;
    void delete_selection();
    void copy_to_clipboard();
};

int TextEditorWinImpl::calculate_visual_cx(int line_idx, int char_pos) {
    if (line_idx >= lines_ref.size()) return 0;
    int visual_pos = 0;
    std::wstring wline = string_to_wstring(lines_ref[line_idx]);
    for (int i = 0; i < char_pos && i < wline.length(); ++i) {
        if (wline[i] == L'\t') {
            visual_pos += 4 - (visual_pos % 4);
        }
        else {
            visual_pos++;
        }
    }
    return visual_pos;
}

// Helper to insert clipboard text as a block
void TextEditorWinImpl::paste_from_clipboard() {
    // 1. Get Text from Clipboard
    if (!OpenClipboard(NULL)) return;
    save_state();
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL) { CloseClipboard(); return; }

    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText == NULL) { CloseClipboard(); return; }
    std::wstring wtext(pszText);
    GlobalUnlock(hData);
    CloseClipboard();

    // 2. Prepare text (Remove \r)
    std::string text = wstring_to_string(wtext);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    if (text.empty()) return;

    // 3. Split into lines
    std::vector<std::string> new_lines;
    std::stringstream ss(text);
    std::string segment;
    while (std::getline(ss, segment, '\n')) {
        new_lines.push_back(segment);
    }
    if (!text.empty() && text.back() == '\n') {
        new_lines.push_back("");
    }
    if (new_lines.empty()) return;

    // 4. Safety Checks
    if (lines_ref.empty()) lines_ref.push_back("");
    if (cy >= lines_ref.size()) cy = (int)lines_ref.size() - 1;
    if (cy < 0) cy = 0;

    // Use wstring for boundary check
    std::wstring current_wline = string_to_wstring(lines_ref[cy]);
    if (cx > current_wline.length()) cx = (int)current_wline.length();

    // 5. Split current line using wide characters
    std::wstring wprefix = current_wline.substr(0, cx);
    std::wstring wsuffix = current_wline.substr(cx);

    std::string prefix = wstring_to_string(wprefix);
    std::string suffix = wstring_to_string(wsuffix);

    // 6. Perform the Paste
    if (new_lines.size() == 1) {
        lines_ref[cy] = prefix + new_lines[0] + suffix;
        cx += (int)new_lines[0].length();
    }
    else {
        lines_ref[cy] = prefix + new_lines[0];

        lines_ref.insert(
            lines_ref.begin() + cy + 1,
            new_lines.begin() + 1,
            new_lines.end()
        );

        int last_inserted_line_idx = cy + (int)new_lines.size() - 1;
        lines_ref[last_inserted_line_idx] += suffix;

        cy = last_inserted_line_idx;
        cx = (int)new_lines.back().length();
    }

    // 7. Scroll the view if the cursor moved off screen
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;

    file_modified = true;

}

void TextEditorWinImpl::run() {
    DWORD original_mode;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &original_mode);

    clear_screen();
    draw_screen();
    visual_cx = calculate_visual_cx(cy, cx);         
    set_cursor(cy - top_row, visual_cx - left_col);  

    //SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

    while (true) {
        INPUT_RECORD input;
        DWORD count;
        ReadConsoleInput(hIn, &input, 1, &count);

        if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown) {
            auto key = input.Event.KeyEvent;

            const bool shift_pressed = key.dwControlKeyState & SHIFT_PRESSED;
            const bool ctrl_pressed = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
            const bool alt_pressed = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);

            bool is_movement_key = (key.wVirtualKeyCode == VK_LEFT || key.wVirtualKeyCode == VK_RIGHT ||
                key.wVirtualKeyCode == VK_UP || key.wVirtualKeyCode == VK_DOWN ||
                key.wVirtualKeyCode == VK_HOME || key.wVirtualKeyCode == VK_END ||
                key.wVirtualKeyCode == VK_PRIOR || key.wVirtualKeyCode == VK_NEXT);

            if (is_movement_key) {
                if (shift_pressed) {
                    if (!is_selecting) {
                        is_selecting = true;
                        sel_cx = cx;
                        sel_cy = cy;
                    }
                }
                else {
                    is_selecting = false;
                }
            }

            if (ctrl_pressed && shift_pressed && !alt_pressed) {
                switch (key.wVirtualKeyCode) {
                case 'C': 
                    copy_to_clipboard(); 
                    break;
                case 'X': 
                    copy_to_clipboard(); 
                    delete_selection(); 
                    break;
                case 'V':
                    if (has_selection()) delete_selection();
                    paste_from_clipboard();
                    break;
                }
            }
            else if (ctrl_pressed && !alt_pressed) {
                switch (key.wVirtualKeyCode) {
                case 'Q':
                    if (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) goto dreckig;
                    break;
                case 'S':
                    if (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) save_file();
                    break;
                case 'F':
                    if (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) find_text();
                    break;
                case 'G':
                    if (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) go_to_line();
                    break;
                case 'C':
                    copy_to_clipboard();
                    break;
                case 'X':
                    copy_to_clipboard();
                    delete_selection();
                    break;
                case 'V':
                    if (has_selection()) delete_selection();
                    paste_from_clipboard();
                    break;
                case 'P': // Handle Paste
                    if (has_selection()) delete_selection();
                    paste_from_clipboard();
                    break;
                case 'Z':
                    undo();
                    break;
                case 'Y':
                    redo();
                    break;
                case 37: { // handle ctrl+left
                    std::wstring wline = string_to_wstring(lines_ref[cy]);
                    while (cx > 0 && iswspace(wline[cx - 1])) move_cursor(-1, 0);
                    while (cx > 0 && !iswspace(wline[cx - 1])) move_cursor(-1, 0);
                    break;
                }
                case 39: { // handle ctrl+right
                    std::wstring wline = string_to_wstring(lines_ref[cy]);
                    int len = (int)wline.length();
                    while (cx < len && !iswspace(wline[cx])) move_cursor(1, 0);
                    while (cx < len && iswspace(wline[cx])) move_cursor(1, 0);
                    break;
                }
                }
            }
            else {
                switch (key.wVirtualKeyCode) {
                case VK_LEFT: move_cursor(-1, 0); break;
                case VK_RIGHT: move_cursor(1, 0); break;
                case VK_UP: move_cursor(0, -1); break;
                case VK_DOWN: move_cursor(0, 1); break;
                case VK_HOME: // Pos1 key
                    cx = 0;
                    break;
                case VK_END: // End key
                    cx = (int)string_to_wstring(lines_ref[cy]).length();
                    break;
                case VK_PRIOR: // PageUp key
                    cy = std::max(0, cy - screen_rows);
                    top_row = std::max(0, top_row - screen_rows);
                    break;
                case VK_NEXT: // PageDown key
                    cy = std::min((int)lines_ref.size() - 1, cy + screen_rows);
                    top_row = std::min((int)lines_ref.size() - screen_rows, top_row + screen_rows);
                    if (top_row < 0) top_row = 0;
                    break;
                case VK_INSERT: // Ins key
                    overwrite_mode = !overwrite_mode;
                    break;
                case VK_DELETE: // Del key
                    save_state();
                    if (has_selection()) {
                        delete_selection();
                    }
                    else {
                        std::wstring wline = string_to_wstring(lines_ref[cy]);
                        if (cx < wline.length()) {
                            wline.erase(cx, 1);
                            lines_ref[cy] = wstring_to_string(wline);
                            file_modified = true;
                        }
                        else if (cy < lines_ref.size() - 1) {
                            lines_ref[cy] += lines_ref[cy + 1];
                            lines_ref.erase(lines_ref.begin() + cy + 1);
                            file_modified = true;
                        }
                    }
                    break;
                case VK_RETURN: {
                    if (has_selection()) delete_selection();
                    save_state();
                    std::wstring wline = string_to_wstring(lines_ref[cy]);
                    std::wstring wremain = wline.substr(cx);
                    std::wstring windent;
                    for (wchar_t c : wline) {
                        if (c == L' ' || c == L'\t') windent += c;
                        else break;
                    }
                    wline = wline.substr(0, cx);
                    lines_ref[cy] = wstring_to_string(wline);
                    lines_ref.insert(lines_ref.begin() + cy + 1, wstring_to_string(windent + wremain));
                    cy++; cx = (int)windent.length();
                    file_modified = true;
                    break;
                }
                case VK_BACK: {
                    save_state();
                    if (has_selection()) {
                        delete_selection();
                    }
                    else {
                        if (cx > 0) {
                            std::wstring wline = string_to_wstring(lines_ref[cy]);
                            wline.erase(cx - 1, 1);
                            lines_ref[cy] = wstring_to_string(wline);
                            cx--; file_modified = true;
                        }
                        else if (cy > 0) {
                            std::wstring wprev = string_to_wstring(lines_ref[cy - 1]);
                            std::wstring wcurr = string_to_wstring(lines_ref[cy]);
                            cx = (int)wprev.length();
                            wprev += wcurr;
                            lines_ref[cy - 1] = wstring_to_string(wprev);
                            lines_ref.erase(lines_ref.begin() + cy);
                            cy--; file_modified = true;
                        }
                    }
                    break;
                }
                case VK_TAB: {
                    if (has_selection()) delete_selection();
                    save_state();
                    std::wstring wline = string_to_wstring(lines_ref[cy]);
                    wline.insert(cx, 1, L'\t');
                    lines_ref[cy] = wstring_to_string(wline);
                    cx++;
                    file_modified = true;
                    break;
                }
                default: {
                    save_state();
                    wchar_t ch = key.uChar.UnicodeChar;
                    if (iswprint(ch)) {
                        if (has_selection()) delete_selection();
                        std::wstring wline = string_to_wstring(lines_ref[cy]);
                        if (overwrite_mode && cx < wline.length()) {
                            // Overwrite mode: replace character
                            wline[cx] = ch;
                        }
                        else {
                            // Insert mode: insert character
                            wline.insert(cx, 1, ch);
                        }
                        lines_ref[cy] = wstring_to_string(wline);
                        cx++;
                        file_modified = true;
                    }
                }
                }
            }
            // Ensure cursor doesn't go past the end of the line after a move
            std::wstring current_wline = string_to_wstring(lines_ref[cy]);
            if (cx > current_wline.length()) {
                cx = (int)current_wline.length();
            }

            visual_cx = calculate_visual_cx(cy, cx); // Calculate visual_cx first

            // Adjust top_row to keep the cursor visible vertically 
            if (cy < top_row) {
                top_row = cy;
            }
            if (cy >= top_row + screen_rows) {
                top_row = cy - screen_rows + 1;
            }

            // Adjust left_col to keep the cursor visible horizontally
            if (visual_cx < left_col) {
                left_col = visual_cx;
            }
            if (visual_cx >= left_col + screen_cols) {
                left_col = visual_cx - screen_cols + 1;
            }

            draw_screen();
            set_cursor(cy - top_row, visual_cx - left_col);
        }
    }
dreckig:
    SetConsoleMode(hIn, original_mode);
    clear_screen();
}

void TextEditorWinImpl::clear_screen() {
    COORD topLeft = { 0, 0 };
    DWORD written;
    FillConsoleOutputCharacter(hOut, ' ', screen_cols * (screen_rows + 2), topLeft, &written);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, screen_cols * (screen_rows + 2), topLeft, &written);
}

void TextEditorWinImpl::set_cursor(int row, int col) {
    COORD pos = { (SHORT)col, (SHORT)row };
    SetConsoleCursorPosition(hOut, pos);
}

void TextEditorWinImpl::write_status(const std::wstring& msg) {
    set_cursor(screen_rows, 0);
    DWORD written;
    std::wstring pad_msg = msg + std::wstring(screen_cols - msg.size(), L' ');
    WriteConsoleW(hOut, pad_msg.c_str(), (DWORD)pad_msg.length(), &written, nullptr);
    set_cursor(screen_rows + 1, 0);
    std::wstring blank(screen_cols, L' ');
    WriteConsoleW(hOut, blank.c_str(), (DWORD)blank.length(), &written, nullptr);
}

void TextEditorWinImpl::draw_line(const std::string& line, int row, int line_index) {
    std::wstring wline = string_to_wstring(line);

    // We parse characters up to the edge of the scrolled view
    int max_visual_cols = left_col + screen_cols;
    std::vector<CHAR_INFO> full_buffer(max_visual_cols);

    // Pre-fill with spaces
    for (auto& c : full_buffer) {
        c.Char.UnicodeChar = L' ';
        c.Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }

    COORD bufferSize = { (SHORT)screen_cols, 1 };
    COORD bufferCoord = { 0, 0 };
    SMALL_RECT writeRegion = { 0, (SHORT)row, (SHORT)(screen_cols - 1), (SHORT)row };

    int sx = 0, sy = 0, ex = 0, ey = 0;
    bool has_sel = has_selection();
    if (has_sel) get_normalized_selection(sx, sy, ex, ey);

    auto apply_selection = [&](size_t char_idx, WORD& attr) {
        if (has_sel) {
            if (line_index > sy && line_index < ey) attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            else if (line_index == sy && line_index == ey && char_idx >= sx && char_idx < ex) attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            else if (line_index == sy && line_index != ey && char_idx >= sx) attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            else if (line_index == ey && line_index != sy && char_idx < ex) attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        }
        };

    int col = 0;
    size_t i = 0;
    // NOTE: All loop limits below use max_visual_cols instead of screen_cols
    while (i < wline.size() && col < max_visual_cols) {
        if (wline[i] == L'\t') {
            int spaces = 4 - (col % 4);
            for (int s = 0; s < spaces && col < max_visual_cols; ++s, ++col) {
                full_buffer[col].Char.UnicodeChar = L' ';
                WORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                apply_selection(i, attr);
                full_buffer[col].Attributes = attr;
            }
            ++i;
            continue;
        }

        wchar_t ch = wline[i];
        WORD base_attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

        if (ch == L'"') {
            const WORD string_attr = FOREGROUND_RED | FOREGROUND_GREEN;

            WORD attr = string_attr;
            apply_selection(i, attr);
            full_buffer[col].Char.UnicodeChar = wline[i];
            full_buffer[col].Attributes = attr;
            i++; col++;

            while (i < wline.size() && col < max_visual_cols) {
                WORD inner_attr = string_attr;
                apply_selection(i, inner_attr);
                full_buffer[col].Char.UnicodeChar = wline[i];
                full_buffer[col].Attributes = inner_attr;

                if (wline[i] == L'"') {
                    i++; col++;
                    break;
                }
                i++; col++;
            }
            continue;
        }
#ifdef PYTHON
        if (is_python && wline[i] == L'#') {
            while (i < wline.size() && col < max_visual_cols) {
                WORD attr = FOREGROUND_GREEN;
                apply_selection(i, attr);
                full_buffer[col].Char.UnicodeChar = wline[i++];
                full_buffer[col++].Attributes = attr;
            }
            break;
        }
#endif
        if (ch == L'\'') {
            while (i < wline.size() && col < max_visual_cols) {
                WORD attr = FOREGROUND_GREEN;
                apply_selection(i, attr);
                full_buffer[col].Char.UnicodeChar = wline[i++];
                full_buffer[col++].Attributes = attr;
            }
            break;
        }

        if (iswdigit(ch)) {
            size_t start = i;
            while (i < wline.size() && (iswdigit(wline[i]) || wline[i] == L'.')) ++i;
            for (size_t j = start; j < i && col < max_visual_cols; ++j, ++col) {
                WORD attr = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                apply_selection(j, attr);
                full_buffer[col].Char.UnicodeChar = wline[j];
                full_buffer[col].Attributes = attr;
            }
            continue;
        }

        if (iswalpha(ch) || ch == L'_') {
            size_t start = i;
            while (i < wline.size() && (iswalnum(wline[i]) || wline[i] == L'$' || wline[i] == L'_')) ++i;
            std::wstring word = wline.substr(start, i - start);
            std::string ascii_word = wstring_to_string(word);

            WORD kwAttr = base_attr;
#ifdef PYTHON
            if (is_python) {
                static const std::unordered_set<std::string> py_keywords = {
                    "False", "None", "True", "and", "as", "assert", "async", "await",
                    "break", "class", "continue", "def", "del", "elif", "else",
                    "except", "finally", "for", "from", "global", "if", "import",
                    "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
                    "return", "try", "while", "with", "yield", "print"
                };
                if (py_keywords.count(ascii_word)) {
                    kwAttr = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                }
            }
            else
#endif
            {
                std::transform(ascii_word.begin(), ascii_word.end(), ascii_word.begin(), ::toupper);
                if (KeywordRepository::is_keyword(ascii_word)) {
                    kwAttr = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
                }
            }

            for (size_t j = 0; j < word.length() && col < max_visual_cols; ++j, ++col) {
                WORD attr = kwAttr;
                apply_selection(start + j, attr);
                full_buffer[col].Char.UnicodeChar = word[j];
                full_buffer[col].Attributes = attr;
            }
            continue;
        }

        WORD final_attr = base_attr;
        apply_selection(i, final_attr);
        full_buffer[col].Char.UnicodeChar = ch;
        full_buffer[col].Attributes = final_attr;
        ++i;
        ++col;
    }

    // Extract the exact portion of the line based on our horizontal scroll position
    std::vector<CHAR_INFO> screen_buffer(screen_cols);
    for (int c = 0; c < screen_cols; ++c) {
        screen_buffer[c] = full_buffer[left_col + c];
    }

    WriteConsoleOutputW(hOut, screen_buffer.data(), bufferSize, bufferCoord, &writeRegion);
}

void TextEditorWinImpl::draw_screen() {
    for (int row = 0; row < screen_rows; ++row) {
        int line_index = top_row + row;
        if (line_index < lines_ref.size()) {
            draw_line(lines_ref[line_index], row, line_index);
        }
        else {
            set_cursor(row, 0);
            DWORD written;
            std::wstring empty_line = L"~" + std::wstring(screen_cols - 1, L' ');
            WriteConsoleW(hOut, empty_line.c_str(), (DWORD)empty_line.length(), &written, nullptr);
        }
    }
    std::wstring status = L" " + string_to_wstring(filename);
    if (file_modified) status += L" *";
    status += L" | Line: " + std::to_wstring(cy + 1) + L" Col: " + std::to_wstring(cx + 1);

    // Indicator for overwrite mode
    if (overwrite_mode) {
        status += L" | OVR";
    }
    else {
        status += L" | INS";
    }

    status += L" | ^S:Save ^F:Find ^G:GoTo ^Q:Exit";
    write_status(status);
}

void TextEditorWinImpl::move_cursor(int dx, int dy) {
    cy = std::clamp(cy + dy, 0, (int)lines_ref.size() - 1);
    std::wstring wline = string_to_wstring(lines_ref[cy]);
    cx = std::clamp(cx + dx, 0, (int)wline.length());
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
}

void TextEditorWinImpl::save_file() {
    if (filename.empty()) {
        std::wstring wname = prompt(L"Save as: ");
        if (wname.empty()) {
            write_status(L"Save aborted.");
            return;
        }
        filename = wstring_to_string(wname);
    }
    std::ofstream out(filename);
    if (!out) {
        write_status(L"Error writing to file!");
        return;
    }
    for (const auto& line : lines_ref) out << line << "";
        file_modified = false;
    write_status(L"File saved successfully.");
}

void TextEditorWinImpl::find_text() {
    std::wstring query = prompt(L"Find: ");
    if (query.empty()) return;
    std::string q = wstring_to_string(query);
    for (size_t i = 0; i < lines_ref.size(); ++i) {
        size_t line_idx = (cy + i) % lines_ref.size();
        size_t found = lines_ref[line_idx].find(q, (line_idx == cy ? cx + 1 : 0));
        if (found != std::string::npos) {
            cy = line_idx;
            cx = (int)found;
            top_row = std::max(0, cy - screen_rows / 2);
            return;
        }
    }
    write_status(L"Not found: " + query);
}

void TextEditorWinImpl::go_to_line() {
    std::wstring input = prompt(L"Go to line: ");
    if (input.empty()) return;
    try {
        int line = std::stoi(input);
        if (line >= 1 && line <= (int)lines_ref.size()) {
            cy = line - 1;
            cx = 0;
            top_row = std::max(0, cy - screen_rows / 2);
        }
        else {
            write_status(L"Line number out of range.");
        }
    }
    catch (...) {
        write_status(L"Invalid input.");
    }
}

void TextEditorWinImpl::save_state() {
    undo_stack.push_back({ lines_ref, cx, cy });
    redo_stack.clear(); // Any new action invalidates the redo history

    // Optional: Limit stack size to prevent excessive memory usage
    if (undo_stack.size() > 100) {
        undo_stack.erase(undo_stack.begin());
    }
}

void TextEditorWinImpl::undo() {
    if (!undo_stack.empty()) {
        // Save current state to redo stack
        redo_stack.push_back({ lines_ref, cx, cy });

        // Restore last state from undo stack
        EditorState state = undo_stack.back();
        undo_stack.pop_back();

        lines_ref = state.lines;
        cx = state.cx;
        cy = state.cy;
        file_modified = true;

        // Ensure cursor remains visible
        if (cy < top_row) top_row = cy;
        if (cy >= top_row + screen_rows) top_row = std::max(0, cy - screen_rows + 1);
    }
}

void TextEditorWinImpl::redo() {
    if (!redo_stack.empty()) {
        // Save current state to undo stack
        undo_stack.push_back({ lines_ref, cx, cy });

        // Restore next state from redo stack
        EditorState state = redo_stack.back();
        redo_stack.pop_back();

        lines_ref = state.lines;
        cx = state.cx;
        cy = state.cy;
        file_modified = true;

        // Ensure cursor remains visible
        if (cy < top_row) top_row = cy;
        if (cy >= top_row + screen_rows) top_row = std::max(0, cy - screen_rows + 1);
    }
}

bool TextEditorWinImpl::has_selection() const {
    return is_selecting && (sel_cy != cy || sel_cx != cx);
}

void TextEditorWinImpl::get_normalized_selection(int& start_x, int& start_y, int& end_x, int& end_y) const {
    if (sel_cy < cy || (sel_cy == cy && sel_cx < cx)) {
        start_x = sel_cx; start_y = sel_cy;
        end_x = cx; end_y = cy;
    }
    else {
        start_x = cx; start_y = cy;
        end_x = sel_cx; end_y = sel_cy;
    }
}

void TextEditorWinImpl::delete_selection() {
    if (!has_selection()) return;
    save_state();
    int sx, sy, ex, ey;
    get_normalized_selection(sx, sy, ex, ey);

    // Perform exact character cuts via wstring
    std::wstring wey = string_to_wstring(lines_ref[ey]);
    std::wstring wremain = wey.substr(ex);

    std::wstring wsy = string_to_wstring(lines_ref[sy]);
    wsy = wsy.substr(0, sx) + wremain;
    lines_ref[sy] = wstring_to_string(wsy);

    if (ey > sy) {
        lines_ref.erase(lines_ref.begin() + sy + 1, lines_ref.begin() + ey + 1);
    }

    cx = sx;
    cy = sy;
    is_selecting = false;
    file_modified = true;
}

void TextEditorWinImpl::copy_to_clipboard() {
    if (!has_selection()) return;
    int sx, sy, ex, ey;
    get_normalized_selection(sx, sy, ex, ey);

    // Build the selection buffer entirely in wstring
    std::wstring wselected_text;
    for (int i = sy; i <= ey; ++i) {
        std::wstring wline = string_to_wstring(lines_ref[i]);
        int line_start = (i == sy) ? sx : 0;
        int line_end = (i == ey) ? ex : (int)wline.length();
        wselected_text += wline.substr(line_start, line_end - line_start);
        if (i < ey) wselected_text += L"\r\n";
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wselected_text.length() + 1) * sizeof(wchar_t));
        if (hMem) {
            memcpy(GlobalLock(hMem), wselected_text.c_str(), (wselected_text.length() + 1) * sizeof(wchar_t));
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

std::wstring TextEditorWinImpl::prompt(const std::wstring& msg) {
    write_status(msg);
    std::wstring input;
    set_cursor(screen_rows + 1, (SHORT)msg.length());
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hIn, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    wchar_t buffer[256];
    DWORD read;
    ReadConsoleW(hIn, buffer, 255, &read, nullptr);
    buffer[read] = 0;
    input = buffer;
    input.erase(std::remove(input.begin(), input.end(), L'\r'), input.end());
    input.erase(std::remove(input.begin(), input.end(), L'\n'), input.end());
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_PROCESSED_INPUT);
    return input;
}

void TextEditor::run() {
    TextEditorWinImpl impl(lines_ref, filename);
    impl.run();
}

TextEditor::TextEditor(std::vector<std::string>& lines, const std::string& fname)
    : lines_ref(lines), filename(fname) {
}

#endif // _WIN32
