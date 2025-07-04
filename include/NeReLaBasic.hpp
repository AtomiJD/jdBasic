// NeReLaBasic.hpp
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <unordered_map>
#include "Types.hpp"
#include "Tokens.hpp"
#include "NetworkManager.hpp"
#include <functional> 
#include <future>
#ifdef SDL3
#include "Graphics.hpp"
#include "SoundSystem.hpp"
#endif


// Forward declarations
class NetworkManager;
class DAPHandler;

class NeReLaBasic {
public:
    // --- Member Variables (Global State) ---
    std::string buffer;
    std::string lineinput;
    std::string filename;

    uint16_t prgptr = 0;
    uint16_t pcode = 0;
    uint16_t linenr = 0;

    uint8_t graphmode = 0;
    uint8_t fgcolor = 2;
    uint8_t bgcolor = 0;
    uint8_t trace = 0;
    bool is_stopped = false;
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

    // --- This struct is for the COMPILER for_stack ---
    struct CompilerForLoopInfo {
        uint16_t source_line;
        std::vector<uint16_t> exit_patch_locations;
    };

    // A type alias for our native C++ function pointers.
    // All native functions will take a vector of arguments and return a single BasicValue.
    using NativeFunction = std::function<BasicValue(NeReLaBasic&, const std::vector<BasicValue>&)>;

    struct FunctionInfo {
        std::string name;
        int arity = 0; // The number of arguments the function takes.
        bool is_procedure = false;
        bool is_exported = false;
        std::string module_name;
        uint16_t start_pcode = 0;
        std::vector<std::string> parameter_names;
        NativeFunction native_impl = nullptr; // A pointer to a C++ function
    };

    using FunctionTable = std::unordered_map<std::string, NeReLaBasic::FunctionInfo>;

    struct StackFrame {
        std::string function_name;
        uint16_t linenr;
        std::unordered_map<std::string, BasicValue> local_variables;
        uint16_t return_pcode = 0; // Where to jump back to after the function ends
        const std::vector<uint8_t>* return_p_code_ptr;
        FunctionTable* previous_function_table_ptr;
        size_t for_stack_size_on_entry;
    };

    struct IfStackInfo {
        uint16_t patch_address; // The address in the bytecode we need to patch
        uint16_t source_line;   // The source code line number of the IF statement
    };

    struct BasicModule {
        std::string name;
        std::vector<uint8_t> p_code;
        FunctionTable function_table;
    };

    // --- For DO...LOOP Stack ---
    struct DoLoopInfo {
        uint16_t loop_start_pcode_addr; // Address of the 'DO' token (or statement after it)
        uint16_t condition_pcode_addr;  // Address where the condition is (for pre-test loops)
        bool is_pre_test;               // True if WHILE/UNTIL is with DO, false if with LOOP
        Tokens::ID condition_type;      // WHILE or UNTIL
        uint16_t source_line;           // For error reporting
        std::vector<uint16_t> exit_patch_locations; // To patch EXIT DO jumps
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

    enum class DebugState {
        RUNNING,    // Normal execution
        PAUSED,     // Stopped at a breakpoint, step, etc.
        STEP_OVER   
    };

    DebugState debug_state = DebugState::RUNNING;

    std::promise<bool> dap_launch_promise; // Used to signal that launch has occurred
    std::string program_to_debug; // Will hold the path from the launch request

    DAPHandler* dap_handler = nullptr;

    // Threading primitives for DAP communication
    std::mutex dap_mutex;
    std::condition_variable dap_cv;
    bool dap_command_received = false;

    // Breakpoints: line number -> BreakpointInfo (can be just bool for now)
    std::map<uint16_t, bool> breakpoints;

    // For 'next' (step over) functionality
    size_t step_over_stack_depth = 0;

    void pause_for_debugger();
    void resume_from_debugger();
    void step_over();

    // This table will hold our built-in constants like 'vbNewLine' and 'PI'
    std::map<std::string, BasicValue> builtin_constants;

    std::string source_code;      // Stores the BASIC program source text
    std::vector<std::string> source_lines;
    std::vector<uint8_t> program_p_code;    // Stores the compiled bytecode for RUN/DUMP
    std::vector<uint8_t> direct_p_code;     // Temporary buffer for direct-mode commands
    const std::vector<uint8_t>* active_p_code = nullptr;    //Active P-Code Pointer

    std::vector<IfStackInfo> if_stack;
    std::vector<ForLoopInfo> for_stack;
    std::vector <CompilerForLoopInfo> compiler_for_stack;
    std::vector<DoLoopInfo> do_loop_stack;

    //std::unordered_map<std::string, FunctionInfo> function_table;
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

    std::unordered_map<std::string, uint16_t> label_addresses;

    // --- C++ Modules ---
    std::map<std::string, BasicModule> compiled_modules;
    // True if the compiler is currently processing a module file
    bool is_compiling_module = false;
    // Holds the name of the module currently being compiled
    std::string current_module_name;

#ifdef SDL3
    Graphics graphics_system;
    SoundSystem sound_system;
#endif

#ifdef HTTP
    NetworkManager network_manager;
#endif

    // --- Error Handling State ---
    bool error_handler_active = false;
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

    // --- Member Functions ---
    NeReLaBasic(); // Constructor
    void start();  // The main REPL
    void execute(const std::vector<uint8_t>& code_to_run, bool resume_mode);
    bool loadSourceFromFile(const std::string& filename);
    std::pair<BasicValue, std::string> resolve_dot_chain(const std::string& chain_string);
    void pre_scan_and_parse_types();

    // --- New Declarations for Expression Parsing ---
    BasicValue evaluate_expression();
    BasicValue parse_comparison();
    BasicValue parse_term();
    BasicValue parse_primary();
    BasicValue parse_power();
    BasicValue parse_unary();
    BasicValue parse_factor();
    BasicValue parse_array_literal();
    BasicValue parse_map_literal();
    bool compile_module(const std::string& module_name, const std::string& module_source_code);
    uint8_t tokenize_program(std::vector<uint8_t>& out_p_code, const std::string& source);
    void statement();
    BasicValue execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args);
    void execute_repl_command(const std::vector<uint8_t>& repl_p_code);
    uint8_t tokenize(const std::string& line, uint16_t lineNumber, std::vector<uint8_t>& out_p_code, FunctionTable& compilation_func_table);

private:
    void init_basic();
    void init_system();
    void init_screen();

    // --- Lexer---
    Tokens::ID parse(NeReLaBasic& vm, bool is_start_of_statement);
    
};

extern NeReLaBasic* g_vm_instance_ptr;
