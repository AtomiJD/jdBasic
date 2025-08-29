#pragma once
#include <vector>
#include <string>
#include <unordered_set> 
#ifdef _WIN32
#include <windows.h>
#endif

class TextEditor {
public:
    // MODIFIED: Constructor now accepts the filename
    TextEditor(std::vector<std::string>& lines, const std::string& filename);
#ifndef _WIN32
    ~TextEditor();
#endif
    void run();

private:
    void process_keypress(int c);
    void draw_screen();
    void draw_status_bar();
    void move_cursor(int key);
#ifdef _WIN32
    void draw_line(const std::string& line);
#else
    void draw_line(int screen_row, const std::string& line);
    int calculate_visual_cx(int line_idx, int char_pos);
#endif
    void get_window_size(int& rows, int& cols);

    // --- HELPER FUNCTIONS FOR NEW FEATURES ---
    void save_file();
    void find_text();
    void go_to_line();
    std::string prompt_user(const std::string& prompt, const std::string& default_val = "");


    std::vector<std::string>& lines_ref;
    int cx = 0, cy = 0;
    int screen_cols;
    int screen_rows;
    int top_row = 0;
    std::string status_msg;

    // --- STATE VARIABLES ---
    std::string filename;
    bool file_modified = false;
    std::string last_search_query;

    std::unordered_set<std::string> keywords;

    bool overwrite_mode = false;

#ifdef _WIN32
    // Define some colors for readability
    const int COLOR_DEFAULT = 15; // White
    const int COLOR_KEYWORD = 12; // Bright Blue
    const int COLOR_STRING = 10;  // Bright Green
    const int COLOR_COMMENT = 7;  // Gray
    const int COLOR_NUMBER = 13;  // Bright Magenta
#else
        // Define ncurses color PAIRS for syntax highlighting
    const int PAIR_DEFAULT = 1;
    const int PAIR_KEYWORD = 2;
    const int PAIR_STRING = 3;
    const int PAIR_COMMENT = 4;
    const int PAIR_NUMBER = 5;
#endif    
};