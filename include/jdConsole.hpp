#pragma once
#ifdef  JDREPL
#include "NeReLaBasic.hpp"
#include <string>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <vector>
#include <deque>

constexpr int MAX_WORKSPACES = 4;

// Key constants
constexpr int KEY_ENTER = 13;
constexpr int KEY_TAB = 9;
constexpr int KEY_BACKSPACE = 8;
constexpr int KEY_ESC = 27;

// Extended keys mapped above standard ASCII
constexpr int KEY_UP = 1000;
constexpr int KEY_DOWN = 1001;
constexpr int KEY_LEFT = 1002;
constexpr int KEY_RIGHT = 1003;
constexpr int KEY_DELETE = 1004; 
constexpr int KEY_HOME = 1005; // (Pos1)
constexpr int KEY_END = 1006; 
constexpr int KEY_CTRL_LEFT = 1007; 
constexpr int KEY_CTRL_RIGHT = 1008;
constexpr int KEY_CTRL_C = 3;  // ASCII code for Ctrl+C
constexpr int KEY_CTRL_V = 22; // ASCII code for Ctrl+V
constexpr int KEY_F1 = 1011;
constexpr int KEY_F2 = 1012;
constexpr int KEY_F3 = 1013;
constexpr int KEY_F4 = 1014;
constexpr int KEY_F5 = 1015;
constexpr int KEY_F7 = 1017;
constexpr int KEY_F8 = 1018;

struct ConsoleWorkspace {
    std::string current_input;
    std::deque<std::string> history;
    int history_idx = -1;
    int cursor_pos = 0;

    std::string history_search_prefix;
    bool is_f8_searching = false;

#if defined(_WIN32)
    std::vector<CHAR_INFO> screen_buffer;
    COORD screen_size = { 0, 0 };
    COORD win32_cursor_pos = { 0, 0 };
    WORD color_attrs = 7;
#endif

    // Sandbox State
    decltype(NeReLaBasic::variables) variables;
    std::vector<std::string> source_lines;
    std::vector<uint8_t> program_p_code;
    NeReLaBasic::FunctionTable main_function_table;
    decltype(NeReLaBasic::user_defined_types) user_defined_types;
};

class jdConsole {
public:
    jdConsole(NeReLaBasic& vm_ref);
    ~jdConsole();

    void run();

private:
    NeReLaBasic& vm;
    ConsoleWorkspace workspaces[MAX_WORKSPACES];
    int active_ws = 0;
    bool is_running = true;

    int read_raw_key();
    void process_key(int key);
    void copy_to_clipboard(const std::string& text);
    std::string get_from_clipboard();

    void execute_current_line();
    void render_prompt();
    void switch_workspace(int index);
    void navigate_history(int direction);
    void show_history_f7();
    void search_history_f8();
    void save_state();
    void load_state();
    void handle_autocomplete();
    void enable_raw_mode();
    void disable_raw_mode();
};
#endif
