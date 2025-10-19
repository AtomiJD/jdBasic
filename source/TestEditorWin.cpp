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

    }

    void run();

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int screen_cols, screen_rows;
    int cx = 0, cy = 0, top_row = 0;
    int visual_cx = 0;
    bool file_modified = false;
    bool overwrite_mode = false;
    std::wstring last_search_query;

    void draw_screen();
    void draw_line(const std::string& line, int row);
    void move_cursor(int dx, int dy);
    void save_file();
    void find_text();
    void go_to_line();
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
};

int TextEditorWinImpl::calculate_visual_cx(int line_idx, int char_pos) {
    if (line_idx >= lines_ref.size()) return 0;
    int visual_pos = 0;
    const std::string& line = lines_ref[line_idx];
    for (int i = 0; i < char_pos; ++i) {
        if (line[i] == '\t') {
            visual_pos += 4 - (visual_pos % 4);
        }
        else {
            visual_pos++;
        }
    }
    return visual_pos;
}

void TextEditorWinImpl::run() {
    DWORD original_mode;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &original_mode);

    clear_screen();
    draw_screen();
    set_cursor(cy - top_row, visual_cx);

    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_PROCESSED_INPUT);

    while (true) {
        INPUT_RECORD input;
        DWORD count;
        ReadConsoleInput(hIn, &input, 1, &count);

        if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown) {
            auto key = input.Event.KeyEvent;

            const bool ctrl_pressed = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
            const bool alt_pressed = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);

            if (ctrl_pressed && !alt_pressed) {
                switch (key.wVirtualKeyCode) {
                case 'X':
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
                    cx = (int)lines_ref[cy].length();
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
                    if (cx < lines_ref[cy].length()) {
                        lines_ref[cy].erase(cx, 1);
                        file_modified = true;
                    }
                    else if (cy < lines_ref.size() - 1) {
                        // If at the end of the line, merge with the next line
                        lines_ref[cy] += lines_ref[cy + 1];
                        lines_ref.erase(lines_ref.begin() + cy + 1);
                        file_modified = true;
                    }
                    break;
                case VK_RETURN: {
                    std::string remain = lines_ref[cy].substr(cx);
                    std::string indent;
                    for (char c : lines_ref[cy]) {
                        if (c == ' ' || c == '\t') indent += c;
                        else break;
                    }
                    lines_ref[cy] = lines_ref[cy].substr(0, cx);
                    lines_ref.insert(lines_ref.begin() + cy + 1, indent + remain);
                    cy++; cx = (int)indent.length();
                    file_modified = true;
                    break;
                }
                case VK_BACK: {
                    if (cx > 0) {
                        lines_ref[cy].erase(cx - 1, 1);
                        cx--; file_modified = true;
                    }
                    else if (cy > 0) {
                        cx = (int)lines_ref[cy - 1].length();
                        lines_ref[cy - 1] += lines_ref[cy];
                        lines_ref.erase(lines_ref.begin() + cy);
                        cy--; file_modified = true;
                    }
                    break;
                }
                case VK_TAB: {
                    std::wstring wline = string_to_wstring(lines_ref[cy]);
                    wline.insert(cx, 1, L'\t');
                    lines_ref[cy] = wstring_to_string(wline);
                    cx++;
                    file_modified = true;
                    break;
                }
                default: {
                    wchar_t ch = key.uChar.UnicodeChar;
                    if (iswprint(ch)) {
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
            if (cx > lines_ref[cy].length()) {
                cx = (int)lines_ref[cy].length();
            }
            draw_screen();
            visual_cx = calculate_visual_cx(cy, cx);
            set_cursor(cy - top_row, visual_cx);
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

void TextEditorWinImpl::draw_line(const std::string& line, int row) {
    //visual_cx = 0;
    std::wstring wline = string_to_wstring(line);
    std::vector<CHAR_INFO> buffer(screen_cols);
    COORD bufferSize = { (SHORT)screen_cols, 1 };
    COORD bufferCoord = { 0, 0 };
    SMALL_RECT writeRegion = { 0, (SHORT)row, (SHORT)(screen_cols - 1), (SHORT)row };

    int col = 0;
    size_t i = 0;
    while (i < wline.size() && col < screen_cols) {
        if (wline[i] == L'\t') {
            int spaces = 4 - (col % 4);
            for (int s = 0; s < spaces && col < screen_cols; ++s, ++col) {
                buffer[col].Char.UnicodeChar = L' ';
                buffer[col].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                //visual_cx++;
            }
            ++i;
            continue;
        }
        wchar_t ch = wline[i];
        WORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

        if (ch == L'"') {
            // This is the corrected block
            const WORD string_attr = FOREGROUND_RED | FOREGROUND_GREEN;

            // Color the opening quote
            buffer[col].Char.UnicodeChar = wline[i];
            buffer[col].Attributes = string_attr;
            i++; col++;

            // Loop until we find the closing quote or the end of the line
            while (i < wline.size() && col < screen_cols) {
                buffer[col].Char.UnicodeChar = wline[i];
                buffer[col].Attributes = string_attr;

                // If this character was the closing quote, break out
                if (wline[i] == L'"') {
                    i++; col++;
                    break;
                }
                i++; col++;
            }
            continue; // Continue the main parsing loop
        }

        if (ch == L'\'') {
            while (i < wline.size() && col < screen_cols) {
                buffer[col].Char.UnicodeChar = wline[i++];
                buffer[col++].Attributes = FOREGROUND_GREEN;
                //visual_cx++;
            }
            break;
        }

        if (iswdigit(ch)) {
            size_t start = i;
            while (i < wline.size() && (iswdigit(wline[i]) || wline[i] == L'.')) ++i;
            for (size_t j = start; j < i && col < screen_cols; ++j, ++col) {
                buffer[col].Char.UnicodeChar = wline[j];
                buffer[col].Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                //visual_cx++;
            }
            continue;
        }

        if (iswalpha(ch)) {
            size_t start = i;
            while (i < wline.size() && (iswalnum(wline[i]) || wline[i] == L'$')) ++i;
            std::wstring word = wline.substr(start, i - start);
            std::string ascii_word = wstring_to_string(word);
            std::transform(ascii_word.begin(), ascii_word.end(), ascii_word.begin(), ::toupper);
            WORD kwAttr = KeywordRepository::is_keyword(ascii_word) ? (FOREGROUND_RED | FOREGROUND_INTENSITY) : attr;
            for (size_t j = 0; j < word.length() && col < screen_cols; ++j, ++col) {
                buffer[col].Char.UnicodeChar = word[j];
                buffer[col].Attributes = kwAttr;
                //visual_cx++;
            }
            continue;
        }

        buffer[col].Char.UnicodeChar = ch;
        buffer[col].Attributes = attr;
        ++i;
        ++col;
        //visual_cx++;
    }

    for (; col < screen_cols; ++col) {
        buffer[col].Char.UnicodeChar = L' ';
        buffer[col].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }

    WriteConsoleOutputW(hOut, buffer.data(), bufferSize, bufferCoord, &writeRegion);
}


void TextEditorWinImpl::draw_screen() {
    for (int row = 0; row < screen_rows; ++row) {
        int line_index = top_row + row;
        if (line_index < lines_ref.size()) {
            draw_line(lines_ref[line_index], row);
        }
        else {
            set_cursor(row, 0);
            DWORD written;
            WriteConsoleW(hOut, L"~", 1, &written, nullptr);
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

    status += L" | ^S:Save ^F:Find ^G:GoTo ^X:Exit";
    write_status(status);
}

void TextEditorWinImpl::move_cursor(int dx, int dy) {
    cy = std::clamp(cy + dy, 0, (int)lines_ref.size() - 1);
    cx = std::clamp(cx + dx, 0, (int)lines_ref[cy].length());
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
