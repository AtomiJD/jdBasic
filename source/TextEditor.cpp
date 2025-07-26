#include "TextEditor.hpp"
#include "TextIO.hpp"
#ifdef _WIN32
#include <conio.h>
#else
#include <ncurses.h>
#endif
#include <algorithm>
#include <cctype> 
#include <fstream> // For saving the file

#define CTRL_KEY(k) ((k) & 0x1f)

void TextEditor::get_window_size(int& rows, int& cols) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    else { // Fallback to default size on error
        cols = 80;
        rows = 25;
    }

#else
    getmaxyx(stdscr, rows, cols);
#endif        

}

// MODIFIED: Constructor now accepts and stores the filename
TextEditor::TextEditor(std::vector<std::string>& lines, const std::string& fname)
    : lines_ref(lines), filename(fname) {
    get_window_size(screen_rows, screen_cols);
    screen_rows -= 2; // Leave space for status bar and prompt
    if (lines_ref.empty()) {
        lines_ref.push_back("");
    }
    keywords = {
        "PRINT", "IF", "THEN", "ELSE", "ENDIF", "FOR", "TO", "NEXT", "STEP",
        "GOTO", "FUNC", "ENDFUNC", "SUB", "ENDSUB", "RETURN", "STOP", "RESUME",
        "DIM", "AS", "INTEGER", "STRING", "DOUBLE", "DATE", "LEFT$", "RIGHT$",
        "MID$", "LEN", "ASC", "CHR$", "INSTR", "LCASE$", "UCASE$", "TRIM$",
        "INKEY$", "VAL", "STR$", "SIN", "COS", "TAN", "SQR", "RND", "TICK",
        "NOW", "DATE$", "TIME$", "DATEADD", "CVDATE", "CLS", "LOCATE", "SLEEP",
        "CURSOR", "DIR", "CD", "PWD", "COLOR", "MKDIR", "KILL", "REM"
    };
}

void TextEditor::draw_line(const std::string& line) {
    size_t i = 0;
    while (i < line.length()) {
        char current_char = line[i];
        if (current_char == '\'') {
            TextIO::setColor(COLOR_COMMENT, 0);
            TextIO::print(line.substr(i));
            break;
        }
        if (current_char == '"') {
            size_t end_pos = line.find('"', i + 1);
            if (end_pos == std::string::npos) end_pos = line.length();
            else end_pos++;
            TextIO::setColor(COLOR_STRING, 0);
            TextIO::print(line.substr(i, end_pos - i));
            i = end_pos;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(current_char))) {
            size_t end_pos = i;
            while (end_pos < line.length() && (std::isdigit(static_cast<unsigned char>(line[end_pos])) || line[end_pos] == '.')) {
                end_pos++;
            }
            TextIO::setColor(COLOR_NUMBER, 0);
            TextIO::print(line.substr(i, end_pos - i));
            i = end_pos;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(current_char))) {
            size_t end_pos = i;
            while (end_pos < line.length() && (std::isalnum(static_cast<unsigned char>(line[end_pos])) || line[end_pos] == '$')) {
                end_pos++;
            }
            std::string word = line.substr(i, end_pos - i);
            std::transform(word.begin(), word.end(), word.begin(), ::toupper);
            if (keywords.count(word)) {
                if (word == "REM") {
                    TextIO::setColor(COLOR_COMMENT, 0);
                    TextIO::print(line.substr(i));
                    break;
                }
                TextIO::setColor(COLOR_KEYWORD, 0);
            }
            else {
                TextIO::setColor(COLOR_DEFAULT, 0);
            }
            TextIO::print(line.substr(i, end_pos - i));
            i = end_pos;
            continue;
        }
        TextIO::setColor(COLOR_DEFAULT, 0);
        TextIO::print(std::string(1, current_char));
        i++;
    }
    TextIO::setColor(COLOR_DEFAULT, 0);
}

void TextEditor::draw_status_bar() {
    TextIO::setCursor(false);
    TextIO::locate(screen_rows + 1, 1);
    // Show filename and modification status
    std::string status = " " + (filename.empty() ? "[No Name]" : filename) + (file_modified ? " * |" : " |");
    status += " Line: " + std::to_string(cy + 1) + "/" + std::to_string(lines_ref.size()) + " | Col: " + std::to_string(cx + 1);
    status += " | ^S:Save ^F:Find ^G:GoTo ^X:Exit ";
    if (status.length() < (size_t)screen_cols) {
        status += std::string(screen_cols - status.length(), ' ');
    }
    TextIO::print(status);

    TextIO::locate(screen_rows + 2, 1);
    // Clear the prompt line
    std::string prompt_line(screen_cols, ' ');
    if (!status_msg.empty()) {
        prompt_line.replace(0, status_msg.length(), status_msg);
    }
    TextIO::print(prompt_line);
    status_msg.clear();
    TextIO::setCursor(true);
}

void TextEditor::draw_screen() {
    TextIO::setColor(COLOR_DEFAULT, 0);
    TextIO::clearScreen();
    for (int y = 0; y < screen_rows; ++y) {
        int file_row = top_row + y;
        TextIO::locate(y + 1, 1);
        if (file_row < lines_ref.size()) {
            draw_line(lines_ref[file_row]);
        }
        else {
            TextIO::setColor(8, 0);
            TextIO::print("~");
        }
    }
    TextIO::setColor(COLOR_DEFAULT, 0);
    draw_status_bar();
}

void TextEditor::move_cursor(int key) {
    switch (key) {
    case 72: if (cy > 0) cy--; break; // Up
    case 80: if (cy < lines_ref.size() - 1) cy++; break; // Down
    case 75: if (cx > 0) cx--; break; // Left
    case 77: if (cy < lines_ref.size() && cx < lines_ref[cy].length()) cx++; break; // Right
    case 73: cy = (cy < screen_rows) ? 0 : cy - screen_rows; break; // Page Up
    case 81: cy += screen_rows; if (cy >= lines_ref.size()) cy = lines_ref.size() - 1; break; // Page Down
    case 71: cx = 0; cy = 0; break; // Home (Ctrl+Home)
    case 79: cx = lines_ref.back().length(); cy = lines_ref.size() - 1; break; // End (Ctrl+End)
    }
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
    if (cy < lines_ref.size() && cx > lines_ref[cy].length()) cx = lines_ref[cy].length();
}

void TextEditor::process_keypress(int c) {
    if (c == 224 || c == 0) {
#ifdef _WIN32        
        move_cursor(_getch());
#else
        move_cursor(getch());
#endif
        return;
    }
    switch (c) {
    case 13: // Enter
        if (cx == lines_ref[cy].length()) lines_ref.insert(lines_ref.begin() + cy + 1, "");
        else {
            std::string line_end = lines_ref[cy].substr(cx);
            lines_ref[cy].erase(cx);
            lines_ref.insert(lines_ref.begin() + cy + 1, line_end);
        }
        cy++; cx = 0; file_modified = true;
        break;
    case 8: // Backspace
        if (cx > 0) { lines_ref[cy].erase(cx - 1, 1); cx--; file_modified = true; }
        else if (cy > 0) {
            cx = lines_ref[cy - 1].length();
            lines_ref[cy - 1] += lines_ref[cy];
            lines_ref.erase(lines_ref.begin() + cy);
            cy--; file_modified = true;
        }
        break;
    case 127: case 9: // DEL or TAB not handled yet
        break;
    default:
        if (isprint(c)) {
            lines_ref[cy].insert(cx, 1, (char)c);
            cx++; file_modified = true;
        }
        break;
    }
}

// --- IMPLEMENTATION OF NEW FEATURES ---

std::string TextEditor::prompt_user(const std::string& prompt, const std::string& default_val) {
    std::string input = default_val;
    while (true) {
        status_msg = prompt + input;
        draw_status_bar();
        TextIO::locate(screen_rows + 2, prompt.length() + input.length() + 1);
#ifdef _WIN32        
        int key = _getch();
#else
        int key = getch();
#endif
        if (key == 13) { // Enter
            status_msg = "";
            return input;
        }
        else if (key == 27) { // Escape
            status_msg = "";
            return "";
        }
        else if (key == 8 && !input.empty()) { // Backspace
            input.pop_back();
        }
        else if (isprint(key)) {
            input += (char)key;
        }
    }
}

void TextEditor::save_file() {
    if (filename.empty()) {
        filename = prompt_user("Save as: ");
        if (filename.empty()) {
            status_msg = "Save aborted.";
            return;
        }
    }
    std::ofstream outfile(filename);
    if (!outfile) {
        status_msg = "Error writing to file!";
        return;
    }
    for (const auto& line : lines_ref) {
        outfile << line << '\n';
    }
    outfile.close();
    status_msg = "File saved successfully.";
    file_modified = false;
}

void TextEditor::find_text() {
    int saved_cx = cx, saved_cy = cy, saved_top = top_row;
    last_search_query = prompt_user("Find (ESC to cancel): ", last_search_query);
    if (last_search_query.empty()) return;

    for (size_t i = 0; i < lines_ref.size(); ++i) {
        int current_line = (saved_cy + i) % lines_ref.size();
        size_t match_pos = lines_ref[current_line].find(last_search_query, (current_line == saved_cy) ? saved_cx + 1 : 0);
        if (match_pos != std::string::npos) {
            cy = current_line;
            cx = match_pos;
            top_row = cy; // Center the found line
            return;
        }
    }
    status_msg = "Text not found: " + last_search_query;
}

void TextEditor::go_to_line() {
    std::string line_num_str = prompt_user("Go to line: ");
    if (line_num_str.empty()) return;
    try {
        int line_num = std::stoi(line_num_str);
        if (line_num > 0 && line_num <= lines_ref.size()) {
            cy = line_num - 1;
            cx = 0;
        }
        else {
            status_msg = "Line number out of range.";
        }
    }
    catch (...) {
        status_msg = "Invalid number.";
    }
}

void TextEditor::run() {
    int key;
    TextIO::setCursor(true);

#ifndef _WIN32
    keypad(stdscr, TRUE);
#endif

    while (true) {
        draw_screen();
        TextIO::locate(cy - top_row + 1, cx + 1);

#ifdef _WIN32
        int key = _getch();
#else
        int key = getch();
#endif

        if (key == CTRL_KEY('x')) { break; }
        else if (key == CTRL_KEY('s')) { save_file(); }
        else if (key == CTRL_KEY('f')) { find_text(); }
        else if (key == CTRL_KEY('g')) { go_to_line(); }
        else if (key == 224 || key == 0) { // Special keys like arrows, F3, etc.
#ifdef _WIN32        
            int ex_key = _getch();
#else
            int ex_key = getch();
#endif
            if (ex_key == 61 && !last_search_query.empty()) { // F3 for Find Next
                find_text();
            }
            else {
                move_cursor(ex_key);
            }
        }
        else {
            process_keypress(key);
        }
    }
    TextIO::setColor(2, 0);
    TextIO::clearScreen();
    TextIO::setCursor(true);
}