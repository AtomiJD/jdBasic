#pragma once
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <set>
#include <functional>
#include <algorithm>

class VM;

// Normalize a file path for case-insensitive comparison on Windows.
// Lowercases the string and converts all separators to backslash.
inline std::string normalize_path(const std::string& p) {
    std::string r = p;
#ifdef _WIN32
    for (auto& c : r) {
        if (c == '/') c = '\\';
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
#endif
    return r;
}

// Debug state for stepping
enum class DebugState {
    RUNNING,
    PAUSED,
    STEP_OVER,
    STEP_IN,
    STEP_OUT
};

class DAPHandler {
public:
    explicit DAPHandler(VM& vm_instance);
    ~DAPHandler();

    void start(int port);
    void stop();

    // Messages sent TO the client
    void send_message(const std::string& msg);
    void send_stopped_message(const std::string& reason, int line, const std::string& path);
    void send_output_message(const std::string& msg);
    void send_repl_message(const std::string& msg);
    void send_program_ended_message();
    void send_stack_frame_message(int index, int frames, int line,
                                  const std::string& func_name, const std::string& path);
    void send_variable_message(const std::string& scope, const std::string& name,
                               const std::string& value);

    // Callback: compile and run code for REPL (set by main.cpp)
    using ReplFunc = std::function<std::string(VM&, const std::string&)>;
    ReplFunc on_repl_eval;

    // Callback: recompile from file (set by main.cpp)
    using RecompileFunc = std::function<bool(VM&, const std::string&)>;
    RecompileFunc on_recompile;

    // Program file path
    std::string program_path;

private:
    VM& vm;
    std::atomic<bool> server_running{false};
    std::thread server_thread;

#ifdef _WIN32
    uintptr_t listen_socket = ~(uintptr_t)0;
    uintptr_t client_socket = ~(uintptr_t)0;
#else
    int listen_socket = -1;
    int client_socket = -1;
#endif

    void server_loop(int port);
    void client_session();
    void process_command(const std::string& command_line);

    // Command handlers
    void on_start();
    void on_set_breakpoint(const std::vector<std::string>& args);
    void on_clear_all_breakpoints();
    void on_clear_breakpoints(const std::string& file);
    void on_get_stacktrace();
    void on_get_vars(const std::vector<std::string>& args);
    void on_watch(const std::string& input);
    void on_repl_cmd(const std::string& input);
    void on_goto(int line);
};

// Debug state stored in VM — these are the hooks
struct DebugInfo {
    DAPHandler* dap = nullptr;
    DebugState state = DebugState::RUNNING;
    // Breakpoints keyed by file path → set of line numbers
    std::map<std::string, std::set<int>> breakpoints;
    std::string program_path;

    // Stepping
    size_t step_over_depth = 0;
    size_t step_out_depth = 0;
    int last_debug_line = -1;
    bool is_entry = true; // first pause sends "entry" reason

    // Pause/resume synchronization
    std::mutex mtx;
    std::condition_variable cv;
    bool command_received = false;

    // Launch synchronization
    std::mutex launch_mtx;
    std::condition_variable launch_cv;
    bool launch_ready = false;
    bool launch_success = false;

    void pause();
    void resume();
    void step_over(size_t call_depth);
    void step_in();
    void step_out(size_t call_depth);
};
