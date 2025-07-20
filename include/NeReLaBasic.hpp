// NeReLaBasic.hpp
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <deque>
#include "Types.hpp"
#include "Tokens.hpp"
#include "NetworkManager.hpp"
#include <functional> 
#include <future>

// --- Platform-specific includes for dynamic library loading ---
#ifdef _WIN32
#include <windows.h>
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
class NetworkManager;
class DAPHandler;
class Compiler;

// Enum for the status of an asynchronous task
enum class TaskStatus {
    RUNNING,
    PAUSED_ON_AWAIT,
    COMPLETED,
    ERRORED
};

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

    uint16_t runtime_current_line = 0;
    uint16_t current_source_line = 0;
    uint16_t current_statement_start_pcode = 0; // Tracks the start of the current statement

    struct ForLoopInfo {
        std::string variable_name; // Name of the loop counter (e.g., "i")
        double end_value = 0;
        double step_value = 0;
        uint16_t loop_start_pcode = 0; // Address to jump back to on NEXT
        std::vector<uint16_t> exit_patch_locations; // To patch EXIT FOR jumps
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

    using FunctionTable = std::unordered_map<std::string, NeReLaBasic::FunctionInfo>;

    struct StackFrame {
        std::string function_name;
        uint16_t linenr;
        std::unordered_map<std::string, BasicValue> local_variables;
        uint16_t return_pcode = 0; // Where to jump back to after the function ends
        const std::vector<uint8_t>* return_p_code_ptr;
        FunctionTable* previous_function_table_ptr;
        size_t for_stack_size_on_entry = 0;
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
        std::vector<ForLoopInfo> for_stack;
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
    };

    std::promise<bool> dap_launch_promise; // Used to signal that launch has occurred
    std::string program_to_debug; // Will hold the path from the launch request

    DAPHandler* dap_handler = nullptr;

    // Threading primitives for DAP communication
    std::mutex dap_mutex;
    std::condition_variable dap_cv;
    bool dap_command_received = false;

    // Breakpoints: line number -> BreakpointInfo (can be just bool for now)
    std::map<uint16_t, bool> breakpoints;

    void pause_for_debugger();
    void resume_from_debugger();
    void step_over();
    void step_in();
    void step_out();
    void handle_debug_events();

    // This table will hold our built-in constants like 'vbNewLine' and 'PI'
    std::map<std::string, BasicValue> builtin_constants;

    std::string source_code;      // Stores the BASIC program source text
    std::vector<std::string> source_lines;
    std::vector<uint8_t> program_p_code;    // Stores the compiled bytecode for RUN/DUMP
    std::vector<uint8_t> direct_p_code;     // Temporary buffer for direct-mode commands
    const std::vector<uint8_t>* active_p_code = nullptr;    //Active P-Code Pointer
    std::vector<ForLoopInfo> for_stack;

    std::vector<StackFrame> call_stack;
    std::vector<uint16_t> func_stack;

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

    //std::unordered_map<std::string, uint16_t> label_addresses;

    // --- C++ Modules ---
    std::map<std::string, BasicModule> compiled_modules;
    // True if the compiler is currently processing a module file
    // bool is_compiling_module = false;
    // Holds the name of the module currently being compiled
    // std::string current_module_name;
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
#endif

    // --- Error Handling State ---
    bool error_handler_active = false;
    BasicValue current_error_data;
    std::string error_handler_function_name = ""; // Name of the function to call on error

    // Context to return to after RESUME
    // These will store the state *before* the error handler is invoked.
    uint16_t resume_pcode_next_statement = 0; // Where RESUME NEXT should jump
    uint16_t resume_pcode = 0;
    uint16_t resume_runtime_line = 0;
    const std::vector<uint8_t>* resume_p_code_ptr = nullptr; // Raw pointer, assuming it points to active_p_code
    NeReLaBasic::FunctionTable* resume_function_table_ptr = nullptr; // Raw pointer
    std::vector<StackFrame> resume_call_stack_snapshot; // Snapshot of call stack for RESUME NEXT/0
    std::vector<ForLoopInfo> resume_for_stack_snapshot; // Snapshot of FOR stack

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

    NeReLaBasic(const NeReLaBasic& other);

    // --- Member Functions ---
    NeReLaBasic(); // Constructor
    ~NeReLaBasic(); //Destructor

    void start();  // The main REPL
    void execute(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    void execute_t(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    bool loadSourceFromFile(const std::string& filename);
    std::pair<BasicValue, std::string> resolve_dot_chain(const std::string& chain_string);

    // --- Function to load a dynamic module ---
    bool load_dynamic_module(const std::string& module_path);

    // --- Declarations for Expression Parsing ---
    BasicValue evaluate_expression();
    BasicValue parse_comparison();
    BasicValue parse_term();
    BasicValue parse_primary();
    BasicValue parse_power();
    BasicValue parse_unary();
    BasicValue parse_factor();
    BasicValue parse_array_literal();
    BasicValue parse_map_literal();
    BasicValue parse_pipe();
    void statement();
    void process_system_events();
    BasicValue execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    void execute_repl_command(const std::vector<uint8_t>& repl_p_code);
    void execute_synchronous_block(const std::vector<uint8_t>& code_to_run);
    BasicValue execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    void execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    void raise_event(const std::string& event_name, BasicValue data);
    void process_event_queue();
    //BasicValue execute_function_for_value_t(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    BasicValue launch_bsync_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    void init_basic();
    void init_system();
    void init_screen();

private:

};

extern NeReLaBasic* g_vm_instance_ptr;
