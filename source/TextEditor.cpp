#ifndef _WIN32
#include "TextEditor.hpp"
#include <ncurses.h>
#include <algorithm>
#include <cctype>
#include <fstream>

#define CTRL_KEY(k) ((k) & 0x1f)

// --- Constructor and Destructor ---

TextEditor::TextEditor(std::vector<std::string>& lines, const std::string& fname)
    : lines_ref(lines), filename(fname) {
    // --- ncurses Initialization ---
    initscr();            // Start curses mode
    raw();                // Disable line buffering
    keypad(stdscr, TRUE); // Enable F1, arrow keys etc.
    noecho();             // Don't echo() while we do getch
    
    // --- Color Initialization ---
    start_color();
    // Use bright colors for better readability on dark terminals
    init_pair(PAIR_DEFAULT, COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_KEYWORD, COLOR_CYAN, COLOR_BLACK);
    init_pair(PAIR_STRING,  COLOR_GREEN, COLOR_BLACK);
    init_pair(PAIR_COMMENT, COLOR_YELLOW, COLOR_BLACK);
    init_pair(PAIR_NUMBER,  COLOR_MAGENTA, COLOR_BLACK);

    getmaxyx(stdscr, screen_rows, screen_cols);
    screen_rows -= 2; // Leave space for status bar and prompt

    if (lines_ref.empty()) {
        lines_ref.push_back("");
    }
}

TextEditor::~TextEditor() {
    endwin(); // End curses mode
}


// --- Drawing and Rendering ---

void TextEditor::draw_line(int screen_row, const std::string& line) {
    move(screen_row, 0);
    clrtoeol();

    size_t i = 0;
    while (i < line.length()) {
        char current_char = line[i];

        if (current_char == '\'') {
            attron(COLOR_PAIR(PAIR_COMMENT));
            addstr(line.substr(i).c_str());
            attroff(COLOR_PAIR(PAIR_COMMENT));
            break;
        }
        if (current_char == '"') {
            attron(COLOR_PAIR(PAIR_STRING));
            size_t start_pos = i;
            i++; // Move past opening quote
            while (i < line.length() && line[i] != '"') {
                i++;
            }
            if (i < line.length()) i++; // Move past closing quote
            addstr(line.substr(start_pos, i - start_pos).c_str());
            attroff(COLOR_PAIR(PAIR_STRING));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(current_char))) {
            attron(COLOR_PAIR(PAIR_NUMBER));
            size_t start_pos = i;
            while (i < line.length() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) {
                i++;
            }
            addstr(line.substr(start_pos, i - start_pos).c_str());
            attroff(COLOR_PAIR(PAIR_NUMBER));
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(current_char))) {
            size_t start_pos = i;
            while (i < line.length() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '$')) {
                i++;
            }
            std::string word = line.substr(start_pos, i - start_pos);
            std::string upper_word = word;
            std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), ::toupper);

            int current_pair = PAIR_DEFAULT;
            if (KeywordRepository::is_keyword(upper_word)) {
                if (upper_word == "REM") {
                    attron(COLOR_PAIR(PAIR_COMMENT));
                    addstr(line.substr(start_pos).c_str());
                    attroff(COLOR_PAIR(PAIR_COMMENT));
                    break;
                }
                current_pair = PAIR_KEYWORD;
            }
            attron(COLOR_PAIR(current_pair));
            addstr(word.c_str());
            attroff(COLOR_PAIR(current_pair));
            continue;
        }

        // Default character
        addch(current_char);
        i++;
    }
}

void TextEditor::draw_status_bar() {
    attron(A_REVERSE); // Invert colors for status bar
    std::string status = " " + (filename.empty() ? "[No Name]" : filename) + (file_modified ? " * |" : " |");
    status += " Line: " + std::to_string(cy + 1) + "/" + std::to_string(lines_ref.size()) + " | Col: " + std::to_string(cx + 1);
    status += (overwrite_mode ? " | OVR" : " | INS");
    status += " | ^S:Save ^F:Find ^G:GoTo ^X:Exit ";
    
    mvprintw(screen_rows, 0, "%s", status.c_str());
    // Fill the rest of the line
    for (size_t i = status.length(); i < screen_cols; ++i) {
        addch(' ');
    }
    attroff(A_REVERSE);

    // Clear and draw prompt line
    move(screen_rows + 1, 0);
    clrtoeol();
    if (!status_msg.empty()) {
        addstr(status_msg.c_str());
    }
}

void TextEditor::draw_screen() {
    curs_set(0); // Hide cursor during redraw
    for (int y = 0; y < screen_rows; ++y) {
        int file_row = top_row + y;
        if (file_row < lines_ref.size()) {
            draw_line(y, lines_ref[file_row]);
        } else {
            // Draw tildes for empty lines like in Vim
            attron(COLOR_PAIR(PAIR_KEYWORD)); // Use a dim color
            mvaddch(y, 0, '~');
            attroff(COLOR_PAIR(PAIR_KEYWORD));
        }
    }
    draw_status_bar();
    status_msg.clear();
    curs_set(1); // Show cursor again
}


// --- Core Editor Logic ---

void TextEditor::run() {
    while (true) {
        draw_screen();
        int visual_x = calculate_visual_cx(cy, cx);
        move(cy - top_row, visual_x); // Position cursor correctly
        
        int key = getch();

        switch (key) {
            case CTRL_KEY('x'):
                return; // Exit editor
            case CTRL_KEY('s'):
                save_file();
                break;
            case CTRL_KEY('f'):
                find_text();
                break;
            case CTRL_KEY('g'):
                go_to_line();
                break;

            // --- Movement ---
            case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT:
            case KEY_PPAGE: case KEY_NPAGE: case KEY_HOME: case KEY_END:
                move_cursor(key);
                break;

            // --- Editing ---
            case KEY_IC: // Insert key
                overwrite_mode = !overwrite_mode;
                break;
            case KEY_DC: // Delete key
                if (cx < lines_ref[cy].length()) {
                    lines_ref[cy].erase(cx, 1);
                    file_modified = true;
                } else if (cy < lines_ref.size() - 1) { // Merge with next line
                    lines_ref[cy] += lines_ref[cy + 1];
                    lines_ref.erase(lines_ref.begin() + cy + 1);
                    file_modified = true;
                }
                break;
            case '\n':
            case KEY_ENTER: {
                // Get indentation of current line
                std::string indent;
                for (char ch : lines_ref[cy]) {
                    if (ch == ' ' || ch == '\t') indent += ch;
                    else break;
                }
                std::string remainder = lines_ref[cy].substr(cx);
                lines_ref[cy].erase(cx);
                lines_ref.insert(lines_ref.begin() + cy + 1, indent + remainder);
                cy++; 
                cx = indent.length(); 
                file_modified = true;
                break;
            }
            case KEY_BACKSPACE:
            case 127: // Common backspace codes
            case 8:
                if (cx > 0) {
                    lines_ref[cy].erase(cx - 1, 1);
                    cx--;
                    file_modified = true;
                } else if (cy > 0) { // Merge with previous line
                    cx = lines_ref[cy - 1].length();
                    lines_ref[cy - 1] += lines_ref[cy];
                    lines_ref.erase(lines_ref.begin() + cy);
                    cy--;
                    file_modified = true;
                }
                break;
            case '\t':
                lines_ref[cy].insert(cx, 1, '\t');
                cx++;
                file_modified = true;
                break;
            default:
                if (isprint(key)) {
                    if (overwrite_mode && cx < lines_ref[cy].length()) {
                        lines_ref[cy][cx] = key;
                    } else {
                        lines_ref[cy].insert(cx, 1, (char)key);
                    }
                    cx++;
                    file_modified = true;
                }
                break;
        }
        // After any action, ensure cursor is not past the end of the line
        if (cy < lines_ref.size() && cx > lines_ref[cy].length()) {
            cx = lines_ref[cy].length();
        }
    }
}


// --- Helper Functions ---

void TextEditor::move_cursor(int key) {
    switch (key) {
        case KEY_UP:    if (cy > 0) cy--; break;
        case KEY_DOWN:  if (cy < lines_ref.size() - 1) cy++; break;
        case KEY_LEFT:  if (cx > 0) cx--; break;
        case KEY_RIGHT: if (cy < lines_ref.size() && cx < lines_ref[cy].length()) cx++; break;
        case KEY_PPAGE: cy = (cy < screen_rows) ? 0 : cy - screen_rows; break;
        case KEY_NPAGE: cy += screen_rows; if (cy >= lines_ref.size()) cy = lines_ref.size() - 1; break;
        case KEY_HOME:  cx = 0; break;
        case KEY_END:   if (cy < lines_ref.size()) cx = lines_ref[cy].length(); break;
    }
    // Scrolling logic
    if (cy < top_row) top_row = cy;
    if (cy >= top_row + screen_rows) top_row = cy - screen_rows + 1;
    // Clamp cursor to end of line
    if (cy < lines_ref.size() && cx > lines_ref[cy].length()) {
        cx = lines_ref[cy].length();
    }
}

int TextEditor::calculate_visual_cx(int line_idx, int char_pos) {
    if (line_idx >= lines_ref.size()) return 0;
    int visual_pos = 0;
    const std::string& line = lines_ref[line_idx];
    const int TAB_WIDTH = 4;
    for (int i = 0; i < char_pos; ++i) {
        if (line[i] == '\t') {
            visual_pos += TAB_WIDTH - (visual_pos % TAB_WIDTH);
        } else {
            visual_pos++;
        }
    }
    return visual_pos;
}

std::string TextEditor::prompt_user(const std::string& prompt, const std::string& default_val) {
    std::string input = default_val;
    while (true) {
        status_msg = prompt + input;
        draw_status_bar();
        move(screen_rows + 1, prompt.length() + input.length());
        
        int key = getch();
        if (key == '\n' || key == KEY_ENTER) {
            status_msg = "";
            return input;
        } else if (key == 27) { // Escape
            status_msg = "";
            return "";
        } else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
            if (!input.empty()) input.pop_back();
        } else if (isprint(key)) {
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
        status_msg = "Error: Cannot write to file!";
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
    int saved_cx = cx, saved_cy = cy;
    last_search_query = prompt_user("Find (ESC to cancel): ", last_search_query);
    if (last_search_query.empty()) return;

    for (size_t i = 0; i < lines_ref.size(); ++i) {
        int current_line = (saved_cy + i) % lines_ref.size();
        // Start search from next character on the same line, or pos 0 on subsequent lines
        size_t start_pos = (current_line == saved_cy) ? saved_cx + 1 : 0;
        size_t match_pos = lines_ref[current_line].find(last_search_query, start_pos);
        if (match_pos != std::string::npos) {
            cy = current_line;
            cx = match_pos;
            // Center the found line if possible
            top_row = std::max(0, cy - screen_rows / 2);
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
            // Center the line if possible
            top_row = std::max(0, cy - screen_rows / 2);
        } else {
            status_msg = "Line number out of range.";
        }
    } catch (...) {
        status_msg = "Invalid number.";
    }
}
#endif