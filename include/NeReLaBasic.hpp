// NeReLaBasic.hpp
#pragma once
#include "AppConfig.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include "Types.hpp"
#include "Tokens.hpp"
#include "NetworkManager.hpp"
#include <functional> 
#include <future>

// --- Platform-specific includes for dynamic library loading ---
#if defined(_WIN32)  || defined(__EMSCRIPTEN__)
#ifndef __EMSCRIPTEN__
#include <windows.h>
#endif
#else
#include <dlfcn.h>
#include <ncurses.h>
#endif

#ifdef SDL3
#include "Graphics.hpp"
#include "SoundSystem.hpp"
#include "SpriteSystem.hpp"
#include "TileMapSystem.hpp"
#endif


// Forward declarations
#ifdef HTTP
class NetworkManager;
struct ServerRequestEvent; 
#endif
class DAPHandler;
class Compiler;

#if defined(__EMSCRIPTEN__)  
enum class VmState { IDLE, RUNNING, PAUSED_FOR_INPUT };
#endif 

// Enum for the status of an asynchronous task
enum class TaskStatus {
    RUNNING,
    PAUSED_ON_AWAIT,
    COMPLETED,
    ERRORED
};

// REACT IMPLEMENTATION: Struct to manage reactive dependencies for a single variable.
struct ReactiveNode {
    std::string name;                           // The variable name.
    std::vector<uint8_t> expression_pcode;      // The p-code to re-evaluate this variable's value.
    std::vector<std::string> dependencies;      // List of variable names this node depends on.
    std::vector<std::string> dependents;        // List of variable names that depend on this node.
    BasicValue last_value;                      // Cached value to check if the variable actually changed after re-evaluation.
};

#ifdef _WIN32
// Converts a std::string (UTF-8) to a std::wstring (UTF-16)
std::wstring string_to_wstring(const std::string& str);
#endif

class NeReLaBasic {
public:
    // --- Nested Types for Execution Machinery ---
    struct FunctionInfo;
    // --- Member Variables (Global State) ---
    std::string buffer;
    std::string lineinput;
    std::string filename;

    uint16_t prgptr = 0;
    uint16_t pcode = 0;
    uint16_t linenr = 0;
    std::string prompt = "> ";

    uint8_t graphmode = 0;
    uint8_t fgcolor = 2;
    uint8_t bgcolor = 0;
    uint8_t trace = 0;
    bool is_stopped = false;
    bool program_ended = false;
    bool nopause_active = false; // Set to true by OPTION "NOPAUSE", disables ESC/Spacebar break/pause
    bool verbose_mode = false;

    uint16_t runtime_current_line = 0;
    uint16_t current_source_line = 0;
    uint16_t current_statement_start_pcode = 0; // Tracks the start of the current statement

    using FunctionTable = std::unordered_map<std::string, NeReLaBasic::FunctionInfo>;

    struct ForLoopInfo {
        std::string variable_name; // Name of the loop counter (e.g., "i")
        double end_value = 0;
        double step_value = 0;
        uint16_t loop_start_pcode = 0; // Address to jump back to on NEXT
        std::vector<uint16_t> exit_patch_locations; // To patch EXIT FOR jumps
    };

    struct ForEachLoopInfo {
        std::string variable_name;
        // We use a variant to hold a pointer to either collection type
        std::variant<std::shared_ptr<Array>, std::shared_ptr<Map>> collection;
        size_t current_index = 0;
        uint16_t loop_body_pcode = 0;
        // This will store a copy of the keys for stable map iteration
        std::vector<std::string> map_keys;
    };

    // --- A single struct to hold either type of loop info ---
    struct LoopInfo {
        std::variant<ForLoopInfo, ForEachLoopInfo> data;
    };

    // A type alias for our native C++ function pointers.
    // All native functions will take a vector of arguments and return a single BasicValue.
    using NativeFunction = std::function<BasicValue(NeReLaBasic&, const std::vector<BasicValue>&)>;

    using NativeDLLFunction = void(*)(NeReLaBasic&, const std::vector<BasicValue>&, BasicValue*);

    // --- A type alias for the registration function in the DLL ---
    using ModuleRegisterFunc = void(*)(NeReLaBasic*);

    struct FunctionInfo {
        std::string name;
        int arity = 0; // The number of arguments the function takes.
        bool is_procedure = false;
        bool is_exported = false;
        bool is_async = false;
        std::string module_name;
        uint16_t start_pcode = 0;
        std::vector<std::string> parameter_names;
        NativeFunction native_impl = nullptr; // A pointer to a C++ function
        NativeDLLFunction native_dll_impl = nullptr; // A pointer to a C++ function in an DLL
    };

    //using FunctionTable = std::unordered_map<std::string, NeReLaBasic::FunctionInfo>;

    struct StackFrame {
        std::string function_name;
        uint16_t linenr;
        std::unordered_map<std::string, BasicValue> local_variables;
        uint16_t return_pcode = 0; // Where to jump back to after the function ends
        const std::vector<uint8_t>* return_p_code_ptr;
        FunctionTable* previous_function_table_ptr;
        size_t loop_stack_size_on_entry = 0;
        bool is_async_call = false;
    };

    enum class DebugState {
        RUNNING,    // Normal execution
        PAUSED,     // Stopped at a breakpoint, step, etc.
        STEP_OVER,
        STEP_IN,
        STEP_OUT
    };

    
    DebugState debug_state = DebugState::RUNNING;
    // For 'next' (step over) functionality
    size_t step_over_stack_depth = 0;
    size_t step_out_stack_depth = 0;

    struct Task {
        int id;
        TaskStatus status = TaskStatus::RUNNING;
        BasicValue result = false;
        std::shared_ptr<Task> awaiting_task = nullptr;
        const std::vector<uint8_t>* p_code_ptr = nullptr;
        uint16_t p_code_counter = 0;
        std::vector<StackFrame> call_stack;
        std::vector<LoopInfo> loop_stack;
        bool yielded_execution = false; // Flag to signal a yield from AWAIT
        // --- Per-Task Error Handling State ---
        bool error_handler_active = false;
        std::string error_handler_function_name = "";
        bool jump_to_error_handler = false;
        uint16_t resume_pcode_next_statement = 0;
        uint16_t resume_pcode = 0;
        uint16_t resume_runtime_line = 0;
        const std::vector<uint8_t>* resume_p_code_ptr = nullptr;
        FunctionTable* resume_function_table_ptr = nullptr;
        std::vector<StackFrame> resume_call_stack_snapshot;
        std::vector<ForLoopInfo> resume_for_stack_snapshot;
        std::vector<ForEachLoopInfo> resume_for_each_stack_snapshot;
        std::future<BasicValue>  result_future; // For C++ background tasks like HTTP
    };

    struct BasicModule {
        std::string name;
        std::vector<uint8_t> p_code;
        FunctionTable function_table;
    };

    // --- Structures for User-Defined Types ---
    struct MemberInfo {
        std::string name;
        DataType type_id = DataType::DOUBLE; // Default type
    };

    struct TypeInfo {
        std::string name;
        // Using a map for members allows for quick lookup
        std::map<std::string, MemberInfo> members;
        FunctionTable methods;
    };

    // --- For TRY/CATCH Runtime ---
    struct ExceptionHandler {
        uint16_t catch_address;    // p-code address of the CATCH block
        uint16_t finally_address;  // p-code address of the FINALLY block
        size_t call_stack_depth;   // Stack depth at the moment TRY was entered
        size_t loop_stack_depth;
    };
    std::vector<ExceptionHandler> handler_stack;

    // --- Flags for pending jumps from Error::set ---
    bool jump_to_catch_pending = false;
    uint16_t pending_catch_address = 0;

    std::promise<bool> dap_launch_promise; // Used to signal that launch has occurred
    std::string program_to_debug; // Will hold the path from the launch request
    std::vector<std::string> command_line_args;

    DAPHandler* dap_handler = nullptr;

    // Threading primitives for DAP communication
    std::mutex dap_mutex;
    std::condition_variable dap_cv;
    bool dap_command_received = false;

    // Breakpoints: line number -> BreakpointInfo (can be just bool for now)
    std::map<uint16_t, bool> breakpoints;

    // This map stores the p-code address for the start of each source line.
    std::map<uint16_t, uint16_t> line_to_pcode_map;

    void pause_for_debugger();
    void resume_from_debugger();
    void step_over();
    void step_in();
    void step_out();
    void handle_debug_events();
    void recompile(int vs_code_current_line);

    // This table will hold our built-in constants like 'vbNewLine' and 'PI'
    std::map<std::string, BasicValue> builtin_constants;

    std::string source_code;      // Stores the BASIC program source text
    std::vector<std::string> source_lines;
    std::vector<uint8_t> program_p_code;    // Stores the compiled bytecode for RUN/DUMP
    std::vector<uint8_t> direct_p_code;     // Temporary buffer for direct-mode commands
    const std::vector<uint8_t>* active_p_code = nullptr;    //Active P-Code Pointer
    std::vector<LoopInfo> loop_stack;

    std::vector<StackFrame> call_stack;
    std::vector<uint16_t> func_stack;
    std::vector<std::shared_ptr<Map>> this_stack;

    // The main program has its own function table
    FunctionTable main_function_table;

    // Each compiled module's function table is stored here
    std::map<std::string, FunctionTable> module_function_tables;

    // This single pointer represents the currently active function table.
    // At runtime, we just change what this pointer points to.
    FunctionTable* active_function_table = nullptr;

    // -- - Symbol Tables for Variables-- -
    std::unordered_map<std::string, BasicValue> variables;
    std::map<std::string, TypeInfo> user_defined_types; // Storage for UDTs

    // --- Structure for Enum Definitions ---
    using EnumMembers = std::unordered_map<std::string, int>;
    std::unordered_map<std::string, EnumMembers> user_defined_enums;

    // --- Runtime stack for SWITCH values ---
    std::vector<BasicValue> switch_value_stack;
    std::vector<bool> switch_case_matched_stack;

    //std::unordered_map<std::string, uint16_t> label_addresses;

    // --- C++ Modules ---
    std::map<std::string, BasicModule> compiled_modules;

    // --- Event Handling System ---
    std::map<std::string, std::string> event_handlers; // Maps event name to function name
    std::deque<std::pair<std::string, BasicValue>> event_queue; // Queue of raised events
    bool is_processing_event = false; // Prevents event recursion

#ifdef _WIN32
    std::vector<HMODULE> loaded_libraries;
#else
    std::vector<void*> loaded_libraries;
#endif

#ifdef SDL3
    Graphics graphics_system;
    SoundSystem sound_system;
#endif

#ifdef HTTP
    NetworkManager network_manager;
    // --- Server Event Handling ---
    std::deque<std::shared_ptr<ServerRequestEvent>> http_request_queue;
    std::mutex http_queue_mutex;
#endif

#if defined(__EMSCRIPTEN__) 
    // Flag to signal the main loop to yield for a frame 
    bool yielded_for_frame = false;
    std::string m_input_variable_name;
    VmState m_internal_vm_state = VmState::IDLE;
    void assign_input_value(const std::string& value);
#endif

    // --- Error Handling State ---
    bool error_handler_active = false;
    BasicValue current_error_data;
    std::string error_handler_function_name = ""; // Name of the function to call on error

    // Flag to signal the main loop to jump to error handler
    bool jump_to_error_handler = false;

    // Built-in variables for error information (accessible in error handler)
    BasicValue err_code = 0.0; // ERR
    BasicValue erl_line = 0.0; // ERL

    std::unique_ptr<Compiler> compiler;

    std::map<int, std::shared_ptr<Task>> task_queue;
    std::map<int, std::shared_ptr<Task>> task_completed;
    int next_task_id = 0;
    Task* current_task = nullptr;

    std::mutex background_tasks_mutex;
    std::map<std::thread::id, std::future<BasicValue>> background_tasks;

    bool is_in_pipe_call = false;
    BasicValue piped_value_for_call;

    std::chrono::steady_clock::time_point last_keyboard_check_time;
    const std::chrono::milliseconds keyboard_check_interval{ 100 };

    // REACT IMPLEMENTATION: Data structures for the reactive graph.
    // The graph itself, mapping a variable name to its reactive properties.
    std::unordered_map<std::string, ReactiveNode> reactive_graph;
    // A simple, fast-lookup set of all variable names declared as REACT.
    std::unordered_set<std::string> react_variables;

    // Lint and Pretty and Strict and explicit
    bool pretty_keywords_vb = false;
    int pretty_indent_width = 4;
    bool option_explicit = false;
    bool option_sctrict = false;

    NeReLaBasic(const NeReLaBasic& other);

    // --- Member Functions ---
    NeReLaBasic(); // Constructor
    ~NeReLaBasic(); //Destructor

    void start();  // The main REPL
    //void execute(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    //void execute_t(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    bool loadSourceFromFile(const std::string& filename, bool verbose = false);
    std::pair<BasicValue, std::string> resolve_dot_chain(const std::string& chain_string);

    // --- Function to load a dynamic module ---
    bool load_dynamic_module(const std::string& module_path);

    // --- Declarations for Expression Parsing ---
    std::vector<BasicValue> parse_argument_list();
    BasicValue evaluate_expression();
    BasicValue parse_comparison();
    BasicValue parse_term();
    BasicValue parse_primary();
    BasicValue parse_power();
    BasicValue parse_unary();
    BasicValue parse_factor();
    BasicValue parse_array_literal();
    BasicValue parse_map_literal();
    BasicValue parse_membership();
    BasicValue parse_pipe();
    BasicValue parse_bitwise_or();
    BasicValue parse_bitwise_xor();
    BasicValue parse_bitwise_and();
    BasicValue parse_logical_and();
    BasicValue parse_logical_or();
    void skip_logical_or();
    void skip_logical_and();
    void skip_bitwise_or();
    void skip_bitwise_xor();
    void skip_bitwise_and();
    void skip_membership();
    void skip_pipe();
    void skip_comparison();
    void skip_term();
    void skip_factor();
    void skip_power();
    void skip_expression();
    void skip_primary();
    void skip_unary();
    void skip_binary_op_chain(std::function<void()> skip_higher_precedence, const std::vector<Tokens::ID>& operators);

    // --- REACT functions ---
    void register_react_variable(const std::string& name);
    void set_reactive_dependency(const std::string& dependent_name);
    void analyze_and_build_dependencies(const std::string& dependent_name, const std::vector<uint8_t>& expression_pcode);
    void propagate_changes(const std::string& changed_variable_name, const std::string& key = "");

    // --- Main execution ---
    BasicValue get_stacktrace();
    void statement();
    void process_system_events();
    void execute_one_frame();
    BasicValue execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args, std::shared_ptr<Map> closure_env = nullptr);
    void execute_repl_command(const std::vector<uint8_t>& repl_p_code);
    void execute_synchronous_block(const std::vector<uint8_t>& code_to_run, int multiline = false);
    BasicValue execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args, std::shared_ptr<Map> closure_env = nullptr);
    //BasicValue execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    void execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    void raise_event(const std::string& event_name, BasicValue data);
    void process_event_queue();
    //BasicValue execute_function_for_value_t(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    BasicValue launch_bsync_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    void init_basic();
    void init_system();
    void init_screen();

#ifdef HTTP
    void queue_http_request(std::shared_ptr<ServerRequestEvent> request);
    void process_http_requests();
#endif 
private:

};

extern NeReLaBasic* g_vm_instance_ptr;
