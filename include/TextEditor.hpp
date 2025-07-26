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
    void run();

private:
    void process_keypress(int c);
    void draw_screen();
    void draw_status_bar();
    void move_cursor(int key);
    void draw_line(const std::string& line);
    void get_window_size(int& rows, int& cols);

    // --- NEW HELPER FUNCTIONS FOR NEW FEATURES ---
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

    // --- NEW STATE VARIABLES ---
    std::string filename;
    bool file_modified = false;
    std::string last_search_query;

    std::unordered_set<std::string> keywords;

    // Define some colors for readability
    const int COLOR_DEFAULT = 15; // White
    const int COLOR_KEYWORD = 12; // Bright Blue
    const int COLOR_STRING = 10;  // Bright Green
    const int COLOR_COMMENT = 7;  // Gray
    const int COLOR_NUMBER = 13;  // Bright Magenta
};