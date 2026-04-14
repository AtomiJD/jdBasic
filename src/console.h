#pragma once
#include <string>
#include <vector>
#include <deque>
#include <queue>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

// Key constants
#define KEY_ENTER       13
#define KEY_TAB         9
#define KEY_BACKSPACE   8
#define KEY_ESC         27
#define KEY_CTRL_C      3
#define KEY_CTRL_V      22
#define KEY_UP          1000
#define KEY_DOWN        1001
#define KEY_LEFT        1002
#define KEY_RIGHT       1003
#define KEY_DELETE      1004
#define KEY_HOME        1005
#define KEY_END         1006
#define KEY_CTRL_LEFT   1007
#define KEY_CTRL_RIGHT  1008
#define KEY_F1          1011
#define KEY_F2          1012
#define KEY_F3          1013
#define KEY_F4          1014
#define KEY_F5          1015
#define KEY_F7          1017
#define KEY_F8          1018

static const int MAX_WORKSPACES = 4;

struct ConsoleState {
    std::string current_input;
    int cursor_pos = 0;
    std::deque<std::string> history;
    int history_idx = -1;
    bool is_f8_searching = false;
    std::string history_search_prefix;
    std::string program_buffer;
};

class VM;
struct VMState;

class Console {
public:
    // executor receives: command string, pointer to the active VM
    using ExecuteFunc = std::function<void(const std::string&, VM&, std::string&)>;

    Console();
    ~Console();

    void set_executor(ExecuteFunc fn) { executor = std::move(fn); }
    VM& active_vm();
    ConsoleState& active_state();
    void run();
    void print(const std::string& text);
    void println(const std::string& text);

private:
    struct Workspace {
        ConsoleState console;
        std::unique_ptr<VMState> vm_state;
    };

    Workspace workspaces[MAX_WORKSPACES];
    int active_ws = 0;
    bool is_running = true;
    bool dirty_prompt = false; // set by key handlers; main loop re-renders once per batch
    ExecuteFunc executor;

    // Shared VM (state is swapped per workspace)
    std::unique_ptr<VM> vm_instance;

#if defined(_WIN32)
    uint32_t original_console_mode = 0;
    std::queue<int> utf8_buffer;
    uint16_t high_surrogate = 0;
#endif

    // Raw mode
    void enable_raw_mode();
    void disable_raw_mode();
    int read_raw_key();

    // Key processing
    void process_key(int key);
    void execute_current_line();
    void navigate_history(int direction);
    void show_history_f7();
    void search_history_f8();
    void switch_workspace(int target);

    // Clipboard
    void copy_to_clipboard(const std::string& text);
    std::string get_from_clipboard();

    // Rendering
    void render_prompt();
    void set_color(int fg, int bg = 0);

    // Syntax highlighting helpers
    bool is_keyword(const std::string& word) const;
    bool is_native(const std::string& word) const;

    // History persistence
    void save_history();
    void load_history();

public:
    // ── RECUR task system ────────────────────────────────────
    struct RecurTask {
        int id;
        int interval_ms;
        std::string code;
        std::atomic<bool> active{true};
        std::chrono::steady_clock::time_point next_run;
    };
    std::vector<std::shared_ptr<RecurTask>> recur_tasks;
    std::mutex recur_mutex;
    int next_task_id = 1;
    std::vector<std::string> pending_executions; // queue for main thread

    int add_recur_task(int interval_ms, const std::string& code);
    void clear_recur_task(int task_id);
    void list_recur_tasks();
    void process_recur_tasks(); // called from main loop
};
