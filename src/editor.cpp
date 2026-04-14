// token.h MUST come before windows.h to avoid macro conflicts
#include "token.h"
#include "natives_list.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "editor.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <functional>

class EditorImpl {
public:
    EditorImpl(std::vector<std::string>& lines, const std::string& fname)
        : lines_ref(lines), filename(fname) {
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleScreenBufferInfo(hOut, &csbi);
        screen_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top;
        screen_rows -= 2; // leave space for status bar
        if (lines_ref.empty()) lines_ref.push_back("");
    }

    void run();

private:
    std::vector<std::string>& lines_ref;
    std::string filename;
    HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int screen_cols, screen_rows;
    int cx = 0, cy = 0, top_row = 0, left_col = 0;
    int visual_cx = 0;
    bool file_modified = false;
    bool overwrite_mode = false;

    // Selection
    bool is_selecting = false;
    int sel_cx = 0, sel_cy = 0;

    // Undo/Redo
    struct EditorState {
        std::vector<std::string> lines;
        int cx, cy;
    };
    std::vector<EditorState> undo_stack;
    std::vector<EditorState> redo_stack;

    // String conversion helpers
    std::wstring to_wide(const std::string& str) {
        if (str.empty()) return L"";
        int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring w(sz, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &w[0], sz);
        w.pop_back();
        return w;
    }

    std::string to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        std::string s(sz, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], sz, NULL, NULL);
        s.pop_back();
        return s;
    }

    int calc_visual_x(int line_idx, int char_pos) {
        if (line_idx >= (int)lines_ref.size()) return 0;
        std::wstring wl = to_wide(lines_ref[line_idx]);
        int vp = 0;
        for (int i = 0; i < char_pos && i < (int)wl.length(); ++i) {
            if (wl[i] == L'\t') vp += 4 - (vp % 4);
            else vp++;
        }
        return vp;
    }

    // Drawing
    void draw_screen();
    void draw_line(const std::string& line, int row, int line_index);
    void clear_screen();
    void set_cursor(int row, int col);
    void write_status(const std::wstring& msg);

    // Navigation
    void move_cursor(int dx, int dy);

    // Editing
    void save_file();
    void find_text();
    void go_to_line();
    void paste_from_clipboard();

    // Undo/Redo
    void save_state();
    void undo();
    void redo();

    // Selection
    bool has_selection() const;
    void get_selection(int& sx, int& sy, int& ex, int& ey) const;
    void delete_selection();
    void copy_to_clipboard();

    // Syntax highlighting: is keyword?
    bool is_kw(const std::string& word) const {
        std::string upper = word;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        auto& kw = keywords();
        return kw.find(upper) != kw.end();
    }

    // Syntax highlighting: is a known native function (or a part of a dotted
    // native name like "AI" or "LOAD_LLM" for "AI.LOAD_LLM")?
    bool is_native(const std::string& word) const {
        std::string upper = word;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        auto& ns = native_names();
        return ns.find(upper) != ns.end();
    }

    std::wstring prompt(const std::wstring& msg);
};

// ── Main editor loop ─────────────────────────────────────────

void EditorImpl::run() {
    DWORD original_mode;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &original_mode);
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);

    clear_screen();
    draw_screen();
    visual_cx = calc_visual_x(cy, cx);
    set_cursor(cy - top_row, visual_cx - left_col);

    while (true) {
        INPUT_RECORD input;
        DWORD count;
        ReadConsoleInput(hIn, &input, 1, &count);

        if (input.EventType != KEY_EVENT || !input.Event.KeyEvent.bKeyDown) continue;
        auto key = input.Event.KeyEvent;

        const bool shift = key.dwControlKeyState & SHIFT_PRESSED;
        const bool ctrl = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
        const bool alt = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED);

        bool is_move = (key.wVirtualKeyCode == VK_LEFT || key.wVirtualKeyCode == VK_RIGHT ||
            key.wVirtualKeyCode == VK_UP || key.wVirtualKeyCode == VK_DOWN ||
            key.wVirtualKeyCode == VK_HOME || key.wVirtualKeyCode == VK_END ||
            key.wVirtualKeyCode == VK_PRIOR || key.wVirtualKeyCode == VK_NEXT);

        if (is_move) {
            if (shift) { if (!is_selecting) { is_selecting = true; sel_cx = cx; sel_cy = cy; } }
            else is_selecting = false;
        }

        // Ctrl+Shift combos (but NOT AltGr which sends Ctrl+Alt)
        if (ctrl && shift && !alt) {
            switch (key.wVirtualKeyCode) {
            case 'C': copy_to_clipboard(); break;
            case 'X': copy_to_clipboard(); delete_selection(); break;
            case 'V': if (has_selection()) delete_selection(); paste_from_clipboard(); break;
            }
        }
        // Ctrl combos (but NOT AltGr)
        else if (ctrl && !alt) {
            switch (key.wVirtualKeyCode) {
            case 'Q': goto exit_editor;
            case 'S': save_file(); break;
            case 'F': find_text(); break;
            case 'G': go_to_line(); break;
            case 'C': copy_to_clipboard(); break;
            case 'X': copy_to_clipboard(); delete_selection(); break;
            case 'P': if (has_selection()) delete_selection(); paste_from_clipboard(); break;
            case 'V': if (has_selection()) delete_selection(); paste_from_clipboard(); break;
            case 'Z': undo(); break;
            case 'Y': redo(); break;
            case 37: { // Ctrl+Left
                std::wstring wl = to_wide(lines_ref[cy]);
                while (cx > 0 && iswspace(wl[cx - 1])) move_cursor(-1, 0);
                while (cx > 0 && !iswspace(wl[cx - 1])) move_cursor(-1, 0);
                break;
            }
            case 39: { // Ctrl+Right
                std::wstring wl = to_wide(lines_ref[cy]);
                int len = (int)wl.length();
                while (cx < len && !iswspace(wl[cx])) move_cursor(1, 0);
                while (cx < len && iswspace(wl[cx])) move_cursor(1, 0);
                break;
            }
            }
        }
        // Normal keys
        else {
            switch (key.wVirtualKeyCode) {
            case VK_LEFT:   move_cursor(-1, 0); break;
            case VK_RIGHT:  move_cursor(1, 0); break;
            case VK_UP:     move_cursor(0, -1); break;
            case VK_DOWN:   move_cursor(0, 1); break;
            case VK_HOME:   cx = 0; break;
            case VK_END:    cx = (int)to_wide(lines_ref[cy]).length(); break;
            case VK_PRIOR:  // PageUp
                cy = std::max(0, cy - screen_rows);
                top_row = std::max(0, top_row - screen_rows);
                break;
            case VK_NEXT:   // PageDown
                cy = std::min((int)lines_ref.size() - 1, cy + screen_rows);
                top_row = std::min((int)lines_ref.size() - screen_rows, top_row + screen_rows);
                if (top_row < 0) top_row = 0;
                break;
            case VK_INSERT: overwrite_mode = !overwrite_mode; break;
            case VK_DELETE: {
                save_state();
                if (has_selection()) { delete_selection(); }
                else {
                    std::wstring wl = to_wide(lines_ref[cy]);
                    if (cx < (int)wl.length()) {
                        wl.erase(cx, 1);
                        lines_ref[cy] = to_utf8(wl);
                        file_modified = true;
                    } else if (cy < (int)lines_ref.size() - 1) {
                        lines_ref[cy] += lines_ref[cy + 1];
                        lines_ref.erase(lines_ref.begin() + cy + 1);
                        file_modified = true;
                    }
                }
                break;
            }
            case VK_RETURN: {
                if (has_selection()) delete_selection();
                save_state();
                std::wstring wl = to_wide(lines_ref[cy]);
                std::wstring remain = wl.substr(cx);
                std::wstring indent;
                for (wchar_t c : wl) {
                    if (c == L' ' || c == L'\t') indent += c;
                    else break;
                }
                wl = wl.substr(0, cx);
                lines_ref[cy] = to_utf8(wl);
                lines_ref.insert(lines_ref.begin() + cy + 1, to_utf8(indent + remain));
                cy++; cx = (int)indent.length();
                file_modified = true;
                break;
            }
            case VK_BACK: {
                save_state();
                if (has_selection()) { delete_selection(); }
                else if (cx > 0) {
                    std::wstring wl = to_wide(lines_ref[cy]);
                    wl.erase(cx - 1, 1);
                    lines_ref[cy] = to_utf8(wl);
                    cx--; file_modified = true;
                } else if (cy > 0) {
                    std::wstring prev = to_wide(lines_ref[cy - 1]);
                    cx = (int)prev.length();
                    prev += to_wide(lines_ref[cy]);
                    lines_ref[cy - 1] = to_utf8(prev);
                    lines_ref.erase(lines_ref.begin() + cy);
                    cy--; file_modified = true;
                }
                break;
            }
            case VK_TAB: {
                if (has_selection()) delete_selection();
                save_state();
                std::wstring wl = to_wide(lines_ref[cy]);
                wl.insert(cx, 1, L'\t');
                lines_ref[cy] = to_utf8(wl);
                cx++; file_modified = true;
                break;
            }
            default: {
                wchar_t ch = key.uChar.UnicodeChar;
                if (iswprint(ch)) {
                    if (has_selection()) delete_selection();
                    save_state();
                    std::wstring wl = to_wide(lines_ref[cy]);
                    if (overwrite_mode && cx < (int)wl.length())
                        wl[cx] = ch;
                    else
                        wl.insert(cx, 1, ch);
                    lines_ref[cy] = to_utf8(wl);
                    cx++; file_modified = true;
                }
            }
            }
        }

        // Clamp cursor
        std::wstring cwl = to_wide(lines_ref[cy]);
        if (cx > (int)cwl.length()) cx = (int)cwl.length();

        visual_cx = calc_visual_x(cy, cx);
        if (cy < top_row) top_row = cy;
        if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
        if (visual_cx < left_col) left_col = visual_cx;
        if (visual_cx >= left_col + screen_cols) left_col = visual_cx - screen_cols + 1;

        draw_screen();
        set_cursor(cy - top_row, visual_cx - left_col);
    }
exit_editor:
    SetConsoleMode(hIn, original_mode);
    clear_screen();
}

// ── Drawing ──────────────────────────────────────────────────

void EditorImpl::clear_screen() {
    COORD topLeft = { 0, 0 };
    DWORD written;
    FillConsoleOutputCharacter(hOut, ' ', screen_cols * (screen_rows + 2), topLeft, &written);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, screen_cols * (screen_rows + 2), topLeft, &written);
}

void EditorImpl::set_cursor(int row, int col) {
    COORD pos = { (SHORT)col, (SHORT)row };
    SetConsoleCursorPosition(hOut, pos);
}

void EditorImpl::write_status(const std::wstring& msg) {
    set_cursor(screen_rows, 0);
    DWORD written;
    std::wstring pad = msg;
    if ((int)pad.size() < screen_cols) pad += std::wstring(screen_cols - pad.size(), L' ');
    WriteConsoleW(hOut, pad.c_str(), (DWORD)pad.length(), &written, nullptr);
    set_cursor(screen_rows + 1, 0);
    std::wstring blank(screen_cols, L' ');
    WriteConsoleW(hOut, blank.c_str(), (DWORD)blank.length(), &written, nullptr);
}

void EditorImpl::draw_line(const std::string& line, int row, int line_index) {
    std::wstring wl = to_wide(line);
    int max_vis = left_col + screen_cols;
    std::vector<CHAR_INFO> buf(max_vis);

    for (auto& c : buf) {
        c.Char.UnicodeChar = L' ';
        c.Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }

    COORD bufSz = { (SHORT)screen_cols, 1 };
    COORD bufCo = { 0, 0 };
    SMALL_RECT wr = { 0, (SHORT)row, (SHORT)(screen_cols - 1), (SHORT)row };

    int sx = 0, sy = 0, ex = 0, ey = 0;
    bool sel = has_selection();
    if (sel) get_selection(sx, sy, ex, ey);

    auto apply_sel = [&](size_t ci, WORD& attr) {
        if (!sel) return;
        if (line_index > sy && line_index < ey)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        else if (line_index == sy && line_index == ey && (int)ci >= sx && (int)ci < ex)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        else if (line_index == sy && line_index != ey && (int)ci >= sx)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        else if (line_index == ey && line_index != sy && (int)ci < ex)
            attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    };

    const WORD BASE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;

    int col = 0;
    size_t i = 0;
    while (i < wl.size() && col < max_vis) {
        // Tab
        if (wl[i] == L'\t') {
            int sp = 4 - (col % 4);
            for (int s = 0; s < sp && col < max_vis; ++s, ++col) {
                buf[col].Char.UnicodeChar = L' ';
                WORD a = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                apply_sel(i, a); buf[col].Attributes = a;
            }
            ++i; continue;
        }
        // String
        if (wl[i] == L'"') {
            const WORD SA = FOREGROUND_RED | FOREGROUND_GREEN; // yellow
            WORD a = SA; apply_sel(i, a);
            buf[col].Char.UnicodeChar = wl[i]; buf[col].Attributes = a;
            i++; col++;
            while (i < wl.size() && col < max_vis) {
                a = SA; apply_sel(i, a);
                buf[col].Char.UnicodeChar = wl[i]; buf[col].Attributes = a;
                if (wl[i] == L'"') { i++; col++; break; }
                i++; col++;
            }
            continue;
        }
        // Comment
        if (wl[i] == L'\'') {
            while (i < wl.size() && col < max_vis) {
                WORD a = FOREGROUND_GREEN; apply_sel(i, a);
                buf[col].Char.UnicodeChar = wl[i++]; buf[col++].Attributes = a;
            }
            break;
        }
        // Number
        if (iswdigit(wl[i])) {
            size_t st = i;
            while (i < wl.size() && (iswdigit(wl[i]) || wl[i] == L'.')) ++i;
            for (size_t j = st; j < i && col < max_vis; ++j, ++col) {
                WORD a = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
                apply_sel(j, a); buf[col].Char.UnicodeChar = wl[j]; buf[col].Attributes = a;
            }
            continue;
        }
        // Identifier / keyword / native function
        if (iswalpha(wl[i]) || wl[i] == L'_') {
            size_t st = i;
            while (i < wl.size() && (iswalnum(wl[i]) || wl[i] == L'_' || wl[i] == L'$')) ++i;
            std::wstring word = wl.substr(st, i - st);
            std::string aword = to_utf8(word);
            WORD kw_attr = BASE;
            if (is_kw(aword)) {
                // Language keyword: magenta (red + blue bright)
                kw_attr = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            } else if (is_native(aword)) {
                // Native/built-in function: cyan (green + blue bright)
                kw_attr = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            }
            for (size_t j = 0; j < word.length() && col < max_vis; ++j, ++col) {
                WORD a = kw_attr; apply_sel(st + j, a);
                buf[col].Char.UnicodeChar = word[j]; buf[col].Attributes = a;
            }
            continue;
        }
        // Other char
        WORD fa = BASE; apply_sel(i, fa);
        buf[col].Char.UnicodeChar = wl[i]; buf[col].Attributes = fa;
        ++i; ++col;
    }

    // Extract visible portion
    std::vector<CHAR_INFO> screen_buf(screen_cols);
    for (int c = 0; c < screen_cols; ++c)
        screen_buf[c] = (left_col + c < (int)buf.size()) ? buf[left_col + c] : buf[0];

    WriteConsoleOutputW(hOut, screen_buf.data(), bufSz, bufCo, &wr);
}

void EditorImpl::draw_screen() {
    for (int row = 0; row < screen_rows; ++row) {
        int li = top_row + row;
        if (li < (int)lines_ref.size()) {
            draw_line(lines_ref[li], row, li);
        } else {
            set_cursor(row, 0);
            DWORD written;
            std::wstring empty = L"~" + std::wstring(screen_cols - 1, L' ');
            WriteConsoleW(hOut, empty.c_str(), (DWORD)empty.length(), &written, nullptr);
        }
    }
    std::wstring status = L" " + to_wide(filename.empty() ? "[new]" : filename);
    if (file_modified) status += L" *";
    status += L" | Ln:" + std::to_wstring(cy + 1) + L" Col:" + std::to_wstring(cx + 1);
    status += overwrite_mode ? L" | OVR" : L" | INS";
    status += L" | ^S:Save ^F:Find ^G:GoTo ^P:Paste ^Q:Exit";
    write_status(status);
}

// ── Navigation ───────────────────────────────────────────────

void EditorImpl::move_cursor(int dx, int dy) {
    cy = std::clamp(cy + dy, 0, (int)lines_ref.size() - 1);
    std::wstring wl = to_wide(lines_ref[cy]);
    cx = std::clamp(cx + dx, 0, (int)wl.length());
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
}

// ── File operations ──────────────────────────────────────────

void EditorImpl::save_file() {
    if (filename.empty()) {
        std::wstring wn = prompt(L"Save as: ");
        if (wn.empty()) { write_status(L"Save aborted."); return; }
        filename = to_utf8(wn);
    }
    std::ofstream out(filename);
    if (!out) { write_status(L"Error writing file!"); return; }
    for (size_t i = 0; i < lines_ref.size(); i++) {
        out << lines_ref[i];
        if (i + 1 < lines_ref.size()) out << "\n";
    }
    file_modified = false;
    write_status(L"File saved.");
}

void EditorImpl::find_text() {
    std::wstring query = prompt(L"Find: ");
    if (query.empty()) return;
    std::string q = to_utf8(query);
    for (size_t i = 0; i < lines_ref.size(); ++i) {
        size_t li = (cy + i) % lines_ref.size();
        size_t found = lines_ref[li].find(q, (li == (size_t)cy ? cx + 1 : 0));
        if (found != std::string::npos) {
            cy = (int)li; cx = (int)found;
            top_row = std::max(0, cy - screen_rows / 2);
            return;
        }
    }
    write_status(L"Not found: " + query);
}

void EditorImpl::go_to_line() {
    std::wstring input = prompt(L"Go to line: ");
    if (input.empty()) return;
    try {
        int line = std::stoi(input);
        if (line >= 1 && line <= (int)lines_ref.size()) {
            cy = line - 1; cx = 0;
            top_row = std::max(0, cy - screen_rows / 2);
        } else {
            write_status(L"Line out of range.");
        }
    } catch (...) { write_status(L"Invalid input."); }
}

void EditorImpl::paste_from_clipboard() {
    if (!OpenClipboard(NULL)) return;
    save_state();
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return; }
    wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
    if (!pText) { CloseClipboard(); return; }
    std::wstring wtext(pText);
    GlobalUnlock(hData);
    CloseClipboard();

    std::string text = to_utf8(wtext);
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    if (text.empty()) return;

    std::vector<std::string> new_lines;
    std::istringstream ss(text);
    std::string seg;
    while (std::getline(ss, seg, '\n')) new_lines.push_back(seg);
    if (!text.empty() && text.back() == '\n') new_lines.push_back("");
    if (new_lines.empty()) return;

    if (lines_ref.empty()) lines_ref.push_back("");
    if (cy >= (int)lines_ref.size()) cy = (int)lines_ref.size() - 1;

    std::wstring cwl = to_wide(lines_ref[cy]);
    if (cx > (int)cwl.length()) cx = (int)cwl.length();

    std::string prefix = to_utf8(cwl.substr(0, cx));
    std::string suffix = to_utf8(cwl.substr(cx));

    if (new_lines.size() == 1) {
        lines_ref[cy] = prefix + new_lines[0] + suffix;
        cx += (int)to_wide(new_lines[0]).length();
    } else {
        lines_ref[cy] = prefix + new_lines[0];
        lines_ref.insert(lines_ref.begin() + cy + 1, new_lines.begin() + 1, new_lines.end());
        int last = cy + (int)new_lines.size() - 1;
        lines_ref[last] += suffix;
        cy = last;
        cx = (int)to_wide(new_lines.back()).length();
    }

    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
    file_modified = true;
}

// ── Undo/Redo ────────────────────────────────────────────────

void EditorImpl::save_state() {
    undo_stack.push_back({ lines_ref, cx, cy });
    redo_stack.clear();
    if (undo_stack.size() > 100) undo_stack.erase(undo_stack.begin());
}

void EditorImpl::undo() {
    if (undo_stack.empty()) return;
    redo_stack.push_back({ lines_ref, cx, cy });
    auto st = undo_stack.back(); undo_stack.pop_back();
    lines_ref = st.lines; cx = st.cx; cy = st.cy;
    file_modified = true;
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = std::max(0, cy - screen_rows + 1);
}

void EditorImpl::redo() {
    if (redo_stack.empty()) return;
    undo_stack.push_back({ lines_ref, cx, cy });
    auto st = redo_stack.back(); redo_stack.pop_back();
    lines_ref = st.lines; cx = st.cx; cy = st.cy;
    file_modified = true;
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = std::max(0, cy - screen_rows + 1);
}

// ── Selection ────────────────────────────────────────────────

bool EditorImpl::has_selection() const {
    return is_selecting && (sel_cy != cy || sel_cx != cx);
}

void EditorImpl::get_selection(int& sx, int& sy, int& ex, int& ey) const {
    if (sel_cy < cy || (sel_cy == cy && sel_cx < cx)) {
        sx = sel_cx; sy = sel_cy; ex = cx; ey = cy;
    } else {
        sx = cx; sy = cy; ex = sel_cx; ey = sel_cy;
    }
}

void EditorImpl::delete_selection() {
    if (!has_selection()) return;
    save_state();
    int sx, sy, ex, ey;
    get_selection(sx, sy, ex, ey);

    std::wstring wey = to_wide(lines_ref[ey]);
    std::wstring remain = wey.substr(ex);
    std::wstring wsy = to_wide(lines_ref[sy]);
    wsy = wsy.substr(0, sx) + remain;
    lines_ref[sy] = to_utf8(wsy);

    if (ey > sy)
        lines_ref.erase(lines_ref.begin() + sy + 1, lines_ref.begin() + ey + 1);

    cx = sx; cy = sy;
    is_selecting = false;
    file_modified = true;
}

void EditorImpl::copy_to_clipboard() {
    if (!has_selection()) return;
    int sx, sy, ex, ey;
    get_selection(sx, sy, ex, ey);

    std::wstring wsel;
    for (int i = sy; i <= ey; ++i) {
        std::wstring wl = to_wide(lines_ref[i]);
        int ls = (i == sy) ? sx : 0;
        int le = (i == ey) ? ex : (int)wl.length();
        wsel += wl.substr(ls, le - ls);
        if (i < ey) wsel += L"\r\n";
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wsel.length() + 1) * sizeof(wchar_t));
        if (hMem) {
            memcpy(GlobalLock(hMem), wsel.c_str(), (wsel.length() + 1) * sizeof(wchar_t));
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

// ── Prompt ───────────────────────────────────────────────────

std::wstring EditorImpl::prompt(const std::wstring& msg) {
    write_status(msg);
    set_cursor(screen_rows + 1, (SHORT)msg.length());
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hIn, ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    wchar_t buf[256];
    DWORD read;
    ReadConsoleW(hIn, buf, 255, &read, nullptr);
    buf[read] = 0;
    std::wstring input = buf;
    input.erase(std::remove(input.begin(), input.end(), L'\r'), input.end());
    input.erase(std::remove(input.begin(), input.end(), L'\n'), input.end());
    SetConsoleMode(hIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    return input;
}

// ── Public interface ─────────────────────────────────────────

Editor::Editor(std::vector<std::string>& lines, const std::string& filename)
    : lines_ref(lines), filename(filename) {}

void Editor::run() {
    EditorImpl impl(lines_ref, filename);
    impl.run();
}
