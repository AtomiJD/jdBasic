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
    void send_exception_message(const std::string& msg);  // exception: b64(msg)
    // repl: b64(msg) ref   (ref 0 = leaf; >0 = expandable hover/watch result)
    void send_repl_message(const std::string& msg, int ref = 0);
    void send_program_ended_message();
    void send_stack_frame_message(int index, int frames, int line,
                                  const std::string& func_name, const std::string& path);
    // var: b64(name) b64(value) b64(eval_name) ref   (ref 0 = leaf)
    void send_variable_message(const std::string& name, const std::string& value,
                               const std::string& eval_name, int ref);

    // Callback: compile and run code for REPL (set by main.cpp)
    using ReplFunc = std::function<std::string(VM&, const std::string&)>;
    ReplFunc on_repl_eval;

    // Callback: recompile from file and reposition to the given source line
    // (set by main.cpp). Returns true if the running program was updated.
    using RecompileFunc = std::function<bool(VM&, const std::string&, int)>;
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
    void on_get_children(int ref);
    void on_watch(const std::string& input);
    void on_repl_cmd(const std::string& input);
    void on_goto(int line);
};

// Debug state stored in VM - these are the hooks
// Per-breakpoint metadata. An empty condition/hit_condition/log_message means
// that facet is unused. log_message turns the breakpoint into a logpoint:
// it emits the (interpolated) message and continues instead of pausing.
struct BreakpointInfo {
    std::string condition;      // pause only if this expression is truthy
    std::string hit_condition;  // e.g. ">5", "10", "%3" against the hit count
    std::string log_message;    // logpoint message ({expr} parts interpolated)
    int hit_count = 0;
};

struct DebugInfo {
    DAPHandler* dap = nullptr;
    DebugState state = DebugState::RUNNING;
    // Breakpoints keyed by normalized file path -> line -> metadata.
    std::map<std::string, std::map<int, BreakpointInfo>> breakpoints;
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

    // Host-driven break path (embed / Godot debugger). When host_hook is
    // set, debug_check calls it synchronously on a pause instead of sending
    // a DAP "stopped" message and blocking on the condvar. The host inspects
    // state and sets the next action (RUNNING / STEP_*) via the embed debug
    // ABI before the hook returns; the VM then proceeds without a wait.
    void* host_ud = nullptr;
    void (*host_hook)(void* ud, int line, const char* reason) = nullptr;

    // Per-line breakpoint predicate (embed/Godot). When set, debug_check
    // asks it on every line whether to break here - lets the host consult
    // an external breakpoint source (Godot's editor breakpoints) without
    // mirroring them into the map. Returns nonzero to pause.
    void* line_ud = nullptr;
    int (*line_break)(void* ud, int line) = nullptr;

    // Set while evaluating a watch expression at a breakpoint so that eval
    // doesn't re-enter the debugger (it runs as a sub-chunk that would
    // otherwise trip debug_check again).
    bool suppress = false;

    void pause();
    void resume();
    void step_over(size_t call_depth);
    void step_in();
    void step_out(size_t call_depth);
};
