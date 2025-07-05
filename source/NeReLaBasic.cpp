// NeReLaBasic.cpp
#include <sstream>     
#include "Commands.hpp"
#include "BuiltinFunctions.hpp" 
#include "Statements.hpp"
#include "NeReLaBasic.hpp"
#include "TextIO.hpp"
#include "Error.hpp"
#include "StringUtils.hpp"
#include "Types.hpp"
#include "DAPHandler.hpp"
#include "Compiler.hpp"
#include <iostream>
#include <fstream>   // For std::ifstream
#include <string>
#include <stdexcept>
#include <cstring>
#include <conio.h>
#include <algorithm> // for std::transform, std::find_if
#include <cctype>    // for std::isspace, std::toupper

// Forward declarations for tensor math functions from BuiltinFunctions.cpp
BasicValue tensor_add(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_subtract(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_elementwise_multiply(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_power(NeReLaBasic& vm, const BasicValue& base, const BasicValue& exponent);
BasicValue tensor_scalar_divide(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
std::shared_ptr<Array> array_add(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);
std::shared_ptr<Array> array_subtract(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);


const std::string NERELA_VERSION = "0.7.2";

void register_builtin_functions(NeReLaBasic& vm, NeReLaBasic::FunctionTable& table_to_populate);

NeReLaBasic* g_vm_instance_ptr = nullptr;

// Helper function to convert a string from the BASIC source to a number.
// Supports decimal, hexadecimal ('$'), and binary ('%').
uint16_t stringToWord(const std::string& s) {
    if (s.empty()) return 0;
    try {
        if (s[0] == '$') {
            return static_cast<uint16_t>(std::stoul(s.substr(1), nullptr, 16));
        }
        if (s[0] == '%') {
            return static_cast<uint16_t>(std::stoul(s.substr(1), nullptr, 2));
        }
        return static_cast<uint16_t>(std::stoul(s, nullptr, 10));
    }
    catch (const std::exception&) {
        // Handle cases where the number is invalid, e.g., "12A4"
        return 0;
    }
}

void trim(std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");

    if (start == std::string::npos)
        s.clear();
    else
        s = s.substr(start, end - start + 1);
}

// --- MODIFIED: The chain resolver now handles both UDTs (Maps) and COM Objects ---
std::pair<BasicValue, std::string> NeReLaBasic::resolve_dot_chain(const std::string& chain_string) {
    std::stringstream ss(chain_string);
    std::string segment;
    std::vector<std::string> parts;
    while (std::getline(ss, segment, '.')) {
        parts.push_back(segment);
    }

    if (parts.empty()) {
        Error::set(1, runtime_current_line);
        return {};
    }

    // Get the base variable (e.g., "PLAYER").
    BasicValue current_object = get_variable(*this, to_upper(parts[0]));

    // Navigate the chain up to the second-to-last part.
    for (size_t i = 1; i < parts.size() - 1; ++i) {
        std::string& part = parts[i];

        // Check if we have a UDT (Map) or a COM object
        if (std::holds_alternative<std::shared_ptr<Map>>(current_object)) {
            auto& map_ptr = std::get<std::shared_ptr<Map>>(current_object);
            if (map_ptr && map_ptr->data.count(part)) {
                current_object = map_ptr->data.at(part);
            }
            else {
                Error::set(3, runtime_current_line, "Member not found: " + part); return {};
            }
        }
#ifdef JDCOM
        else if (std::holds_alternative<ComObject>(current_object)) {
            IDispatchPtr pDisp = std::get<ComObject>(current_object).ptr;
            if (!pDisp) { Error::set(1, runtime_current_line, "Uninitialized COM object."); return {}; }
            _variant_t result_vt;
            HRESULT hr = invoke_com_method(pDisp, part, {}, result_vt, DISPATCH_PROPERTYGET);
            if (FAILED(hr)) {
                hr = invoke_com_method(pDisp, part, {}, result_vt, DISPATCH_METHOD);
                if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM member not found: " + part); return {}; }
            }
            current_object = variant_t_to_basic_value(result_vt, *this);
        }
#endif
        else {
            Error::set(15, runtime_current_line, "Dot notation can only be used on objects and user-defined types.");
            return {};
        }
    }

    if (parts.size() > 1) {
        return { current_object, parts.back() };
    }
    else {
        return { current_object, "" };
    }
}

// Constructor: Initializes the interpreter state
NeReLaBasic::NeReLaBasic() : program_p_code(65536, 0) { // Allocate 64KB of memory
    buffer.reserve(64);
    lineinput.reserve(160);
    filename.reserve(40);
    
    active_function_table = &main_function_table;
    compiler = std::make_unique<Compiler>();

    register_builtin_functions(*this, *active_function_table);
    srand(static_cast<unsigned int>(time(nullptr)));

    builtin_constants["VBNEWLINE"] = std::string("\n");
    builtin_constants["PI"] = 3.14159265358979323846;
    
    // Initialize ERR and ERL as global variables
    builtin_constants["ERR"] = 0.0;
    builtin_constants["ERL"] = 0.0;
}

NeReLaBasic::~NeReLaBasic() = default;

bool NeReLaBasic::loadSourceFromFile(const std::string& filename) {
    std::ifstream infile(filename);
    if (!infile) {
        TextIO::print("Error: File not found -> " + filename + "\n");
        return false;
    }
    TextIO::print("LOADING " + filename + "\n");
    // Read the entire file into the source_code string
    source_lines.clear();
    std::string line;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        source_lines.push_back(line);
    }
    return true;
}

void NeReLaBasic::init_screen() {
    TextIO::setColor(fgcolor, bgcolor);
    TextIO::clearScreen();
    TextIO::print("NeReLa Basic v " + NERELA_VERSION + "\n");
    TextIO::print("(c) 2025\n\n");
}

void NeReLaBasic::init_system() {
    // Let's set the program start memory location, as in the original.
    pcode = 0;
    trace = 0;

    TextIO::print("Trace is:  ");
    TextIO::print_uw(trace);
    TextIO::nl();
}

void NeReLaBasic::init_basic() {
    TextIO::nl();
    //TextIO::print("Ready\n");
}

// The main REPL
void NeReLaBasic::start() {
    init_screen();
    init_system();
    init_basic();

    std::string inputLine;

    while (true) {
        Error::clear();
        direct_p_code.clear();
        linenr = 0;
        TextIO::print("Ready\n? ");

        if (!std::getline(std::cin, inputLine) || inputLine.empty()) {
            std::cin.clear();
            continue;
        }

        // --- Special handling for RESUME ---
        std::string temp_line = inputLine;
        StringUtils::trim(temp_line);
        if (StringUtils::to_upper(temp_line) == "RESUME") {
            if (is_stopped) {
                TextIO::print("Resuming...\n");
                is_stopped = false;
                execute_main_program(program_p_code, true); // Continues from the saved pcode
                if (Error::get() != 0) Error::print();
            }
            else {
                TextIO::print("?Nothing to resume.\n");
            }
            continue; // Go back to the REPL prompt
        }

        // Tokenize the direct-mode line, passing '0' as the line number
        active_function_table = &main_function_table;
        if (compiler->tokenize(*this, inputLine, 0, direct_p_code, *active_function_table) != 0) {
            Error::print();
            continue;
        }
        // Execute the direct-mode p_code
        execute_synchronous_block(direct_p_code);

        if (Error::get() != 0) {
            Error::print();
        }
    }
}

BasicValue NeReLaBasic::execute_function_for_value_t(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {

    UINT16 old_line = 0;

    if (func_info.native_impl != nullptr) {
        return func_info.native_impl(*this, args);
    }

    size_t initial_stack_depth = call_stack.size();

    // 1. Set up the new stack frame
    NeReLaBasic::StackFrame frame;
    frame.return_p_code_ptr = this->active_p_code;
    frame.return_pcode = this->pcode;
    frame.previous_function_table_ptr = this->active_function_table;
    frame.for_stack_size_on_entry = this->for_stack.size();
    frame.function_name = func_info.name;
    frame.linenr = runtime_current_line;

    for (size_t i = 0; i < func_info.parameter_names.size(); ++i) {
        if (i < args.size()) frame.local_variables[func_info.parameter_names[i]] = args[i];
    }
    call_stack.push_back(frame);

    // 2. --- CONTEXT SWITCH ---
    if (!func_info.module_name.empty() && compiled_modules.count(func_info.module_name)) {
        this->active_p_code = &this->compiled_modules.at(func_info.module_name).p_code;
        this->active_function_table = &this->compiled_modules.at(func_info.module_name).function_table;
    }
    // 3. Set the program counter to the function's start address IN THE NEW CONTEXT
    this->pcode = func_info.start_pcode;

    // 4. Execute statements until the function returns
    this->pcode = func_info.start_pcode;
    while (call_stack.size() > initial_stack_depth && !is_stopped) {

        if (Error::get() != 0) {
            // Unwind stack on error to prevent infinite loops
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            this->active_function_table = frame.previous_function_table_ptr; // Restore context
            return false;
        }
        if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
            Error::set(25, runtime_current_line); // Missing RETURN
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            break;
        }

        if (dap_handler) {
            // 1. Handle general PAUSED state (e.g., user manually paused)
            if (debug_state == DebugState::PAUSED) {
                pause_for_debugger();
            }

            // 2. Check for regular breakpoints
            if (breakpoints.count(runtime_current_line)) {
                // Pause if a breakpoint is hit, unless we are stepping over a call
                // and are currently inside that call (stack is deeper).
                if (debug_state != DebugState::STEP_OVER ) {
                    debug_state = DebugState::PAUSED;
                    dap_handler->send_stopped_message("breakpoint", runtime_current_line, this->program_to_debug);
                    pause_for_debugger();
                    continue; // Re-evaluate the loop after un-pausing
                }
            }
        }

        old_line = runtime_current_line;
        statement();
        // --- >> DAP INTEGRATION POINT (POST-EXECUTION) << ---
        if (dap_handler && runtime_current_line > old_line) {
            // Check if a "step over" action has completed
            if (debug_state == DebugState::STEP_OVER ) {
                debug_state = DebugState::PAUSED;
                // Update line number for the message, as the next loop hasn't run yet
                dap_handler->send_stopped_message("step", runtime_current_line, this->program_to_debug);
                pause_for_debugger();
                continue; // Re-evaluate loop after command
            }
        }

    }
    return variables["RETVAL"];
}

void NeReLaBasic::execute_repl_command(const std::vector<uint8_t>& repl_p_code) {
    if (repl_p_code.empty()) {
        return;
    }

    // --- Save the state of the main program's execution context ---
    const auto* original_active_pcode = this->active_p_code;
    uint16_t original_pcode = this->pcode;

    // --- Temporarily switch context to the REPL's p-code ---
    this->active_p_code = &repl_p_code;
    this->pcode = 0; // Start at the beginning of the REPL code

    // --- Simplified execution loop for the REPL command ---
    // This loop does NOT check for debug state, breakpoints, or pause signals.
    while (this->pcode < this->active_p_code->size()) {
        // The first two bytes of tokenized code are the line number (always 0 for REPL).
        // The main `statement()` function expects `pcode` to point AFTER these bytes.
        if (this->pcode == 0) {
            this->pcode += 2;
        }

        Tokens::ID token = static_cast<Tokens::ID>((*this->active_p_code)[this->pcode]);

        // Stop if we hit the end-of-line or end-of-code markers
        if (token == Tokens::ID::NOCMD || token == Tokens::ID::C_CR) {
            break;
        }

        // Execute one statement from the REPL command
        statement();

        // If the statement caused an error, stop processing
        if (Error::get() != 0) {
            break;
        }

        // Handle multi-statement lines separated by ':'
        if (this->pcode < this->active_p_code->size() &&
            static_cast<Tokens::ID>((*this->active_p_code)[this->pcode]) == Tokens::ID::C_COLON) {
            this->pcode++; // Consume the colon and continue the loop
        }
    }

    // --- Restore the original execution context ---
    this->active_p_code = original_active_pcode;
    this->pcode = original_pcode;
}

// In NeReLaBasic.cpp
void NeReLaBasic::pause_for_debugger() {
    std::unique_lock<std::mutex> lock(dap_mutex);
    dap_command_received = false;
    dap_cv.wait(lock, [this] { return dap_command_received; });
    dap_command_received = false;
}

void NeReLaBasic::resume_from_debugger() {
    {
        std::lock_guard<std::mutex> lock(dap_mutex);
        debug_state = DebugState::RUNNING;
        dap_command_received = true;
    }
    dap_cv.notify_one();
}

void NeReLaBasic::step_over() {
    {
        std::lock_guard<std::mutex> lock(dap_mutex);
        debug_state = DebugState::STEP_OVER;
        step_over_stack_depth = call_stack.size();
        dap_command_received = true;
    }
    dap_cv.notify_one();
}


// Wrapper for synchronous function calls from the expression parser
BasicValue NeReLaBasic::execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    if (func_info.native_impl != nullptr) {
        return func_info.native_impl(*this, args);
    }
    return execute_synchronous_function(func_info, args);
}

// New synchronous executor for REPL and direct commands
void NeReLaBasic::execute_synchronous_block(const std::vector<uint8_t>& code_to_run) {
    if (code_to_run.empty()) return;

    auto prev_active_p_code = active_p_code;
    auto prev_pcode = pcode;
    active_p_code = &code_to_run;
    pcode = 0;

    while (pcode < active_p_code->size()) {
        if (pcode == 0) pcode += 2; // Skip line number
        Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (token == Tokens::ID::NOCMD || token == Tokens::ID::C_CR) break;
        statement();
        if (Error::get() != 0) break;
        if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_COLON) {
            pcode++;
        }
    }

    active_p_code = prev_active_p_code;
    pcode = prev_pcode;
}

// New synchronous executor for user-defined functions
BasicValue NeReLaBasic::execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    size_t initial_stack_depth = call_stack.size();

    // --- FIX: Properly initialize the stack frame ---
    StackFrame frame;
    frame.return_p_code_ptr = this->active_p_code;
    frame.return_pcode = this->pcode;
    frame.previous_function_table_ptr = this->active_function_table;
    frame.for_stack_size_on_entry = this->for_stack.size();
    frame.function_name = func_info.name;
    frame.linenr = runtime_current_line;

    for (size_t i = 0; i < func_info.parameter_names.size(); ++i) {
        if (i < args.size()) {
            frame.local_variables[func_info.parameter_names[i]] = args[i];
        }
    }
    call_stack.push_back(frame);

    // --- Context switch ---
    auto prev_active_func_table = this->active_function_table;
    auto prev_active_p_code = this->active_p_code;

    if (!func_info.module_name.empty() && compiled_modules.count(func_info.module_name)) {
        this->active_p_code = &this->compiled_modules.at(func_info.module_name).p_code;
        this->active_function_table = &this->compiled_modules.at(func_info.module_name).function_table;
    }
    this->pcode = func_info.start_pcode;

    // --- Execution Loop ---
    while (call_stack.size() > initial_stack_depth && !is_stopped) {
        if (Error::get() != 0) {
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            break;
        }
        if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
            Error::set(25, runtime_current_line); // Missing RETURN
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            break;
        }
        statement();
    }

    // --- Context restore ---
    //this->active_function_table = prev_active_func_table;
    //this->active_p_code = prev_active_p_code;

    if (variables.count("RETVAL")) {
        return variables["RETVAL"];
    }
    return false;
}

void NeReLaBasic::execute_t(const std::vector<uint8_t>& code_to_run, bool resume_mode) {
    // If there's no code to run, do nothing.
    if (code_to_run.empty()) {
        return;
    }

    // Set the active p_code pointer for the duration of this execution.
    auto prev_active_p_code = active_p_code;
    active_p_code = &code_to_run;

    // Only reset the program counter if we are not resuming.
    if (!resume_mode) {
        pcode = 0;
    }

    Error::clear();
    g_vm_instance_ptr = this;

    if (dap_handler) { // Check if the debugger is attached
        debug_state = DebugState::PAUSED;
        // Tell the client we are paused at the entry point.
        runtime_current_line = (*active_p_code)[0] | ((*active_p_code)[1] << 8);
        dap_handler->send_stopped_message("entry", runtime_current_line, this->program_to_debug);
        // Wait for the first "continue" or "next" command from the client.
        pause_for_debugger();
    }

    // Main execution loop
    while (pcode < active_p_code->size() && !is_stopped) {

#ifdef SDL3
        if (graphics_system.is_initialized) { // Check if graphics are active
            if (!graphics_system.handle_events()) {
                break; // Exit execution if the user closed the window
            }
        }
#endif
        if (!nopause_active) { // Only process input if NOPAUSE is NOT active
            if (_kbhit()) {
                char key = _getch(); // Get the pressed key

                // The ESC key has an ASCII value of 27
                if (key == 27) {
                    TextIO::print("\n--- BREAK ---\n");
                    break;                   // Exit the loop immediately
                }
                // Let's use the spacebar to pause
                else if (key == ' ') {
                    TextIO::print("\n--- PAUSED (Press any key to resume) ---\n");
                    _getch(); // Wait for another key press to un-pause
                    TextIO::print("--- RESUMED ---\n");
                }
            }
        }
        // --- >> DAP INTEGRATION POINT << ---
        if (debug_state == DebugState::PAUSED) {
            // We are paused, waiting for the DAP client
            std::unique_lock<std::mutex> lock(dap_mutex);
            dap_cv.wait(lock, [this] { return dap_command_received; });
            dap_command_received = false; // Reset the flag
            dap_handler->send_output_message("We are in PAUSED");
        }

        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);

        // Use the active_p_code pointer to access the bytecode

        if (resume_mode) {
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR)
                pcode++; // Consume CR
        }

        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);

        // 2. Check for regular breakpoints.
        if (breakpoints.count(runtime_current_line)) {
            // Do NOT stop at a breakpoint if we are stepping over a line
            // and are currently inside a function call (stack is deeper).
            // dap_handler->send_output_message("jdBasic: Breakpoint:" + std::to_string(step_over_stack_depth) + ", " + std::to_string(call_stack.size()));
            if (debug_state != DebugState::STEP_OVER ) {
                debug_state = DebugState::PAUSED;
                dap_handler->send_stopped_message("breakpoint", runtime_current_line, this->program_to_debug);
                dap_handler->send_output_message("Breakpoint arrived->PAUSED");
                pause_for_debugger();
                continue;
            }
        }

        pcode += 2;
        

        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
            break;
        }
        bool line_is_done = false;
        bool statement_is_run = false;
        while (!line_is_done && pcode < active_p_code->size()) {
            current_statement_start_pcode = pcode; // Capture statement start here!
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR) {
                statement();
                statement_is_run = true;
            }
            else {
                statement_is_run = false;
            }

            if (Error::get() != 0 || is_stopped) {
                line_is_done = true; // Stop processing this line on error
                continue;
            }

            // After the statement, check the token that follows.
            if (pcode < active_p_code->size()) {
                Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (next_token == Tokens::ID::C_COLON) {
                    pcode++; // Consume the separator and loop again for the next statement.
                }
                else {
                    // Token (C_CR, NOCMD) means the line is finished.
                    if (next_token == Tokens::ID::C_CR || next_token == Tokens::ID::NOCMD) {
                        line_is_done = true;
                    }
                }
            }
            if (dap_handler && statement_is_run) {
                // 1. Check if we are in STEP_OVER mode.
                if ((debug_state == DebugState::STEP_OVER) && line_is_done == true) {
                    // We should stop if the current call stack is the same size as,
                    // or smaller than, when we started the step. This means we are
                    // on the next line in the same function, or have returned from a function.
                    
                    debug_state = DebugState::PAUSED;
                    uint16_t opcode = pcode;
                    opcode++;
                    const uint16_t debug_line = (*active_p_code)[opcode] | ((*active_p_code)[opcode+1] << 8);
                    dap_handler->send_output_message("jdBasic: step received from debugger: " + std::to_string(debug_line));
                    dap_handler->send_stopped_message("step", debug_line, this->program_to_debug);
                    pause_for_debugger();
                    // After pausing, we might get another command, so re-evaluate the loop.
                    continue;
                }
            }
        }

        // --- Error handling ---
        if (Error::get() != 0 && error_handler_active && jump_to_error_handler) {
            jump_to_error_handler = false; // Reset the flag
            // Clear the actual error code so ERR/ERL can be read by the handler
            uint8_t caught_error_code = Error::get();
            Error::clear();

            // Invoke the error handling function (like a CALLSUB)
            // You can re-use your do_callsub logic or call it directly:
            if (active_function_table->count(error_handler_function_name)) {
                const auto& proc_info = active_function_table->at(error_handler_function_name);
                // Call the procedure without arguments (ERR and ERL are global vars)
                NeReLaBasic::StackFrame frame;
                frame.return_p_code_ptr = this->active_p_code;
                frame.return_pcode = this->pcode;
                frame.previous_function_table_ptr = this->active_function_table;
                frame.for_stack_size_on_entry = this->for_stack.size();
                frame.function_name = error_handler_function_name;
                frame.linenr = runtime_current_line;
                // No args to pass if ERR and ERL are global
                this->call_stack.push_back(frame);

                if (!proc_info.module_name.empty() && compiled_modules.count(proc_info.module_name)) {
                    auto& target_module = compiled_modules[proc_info.module_name];
                    this->active_p_code = &target_module.p_code;
                    this->active_function_table = &target_module.function_table;
                }
                uint16_t current_error_pcode_snapshot = pcode; // Save where the error occurred

                // Find the end of the current logical line
                while (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::NOCMD) {
                    pcode++; // Move past the current problematic statement/expression
                }
                if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                    pcode++; // Consume C_CR
                    pcode += 2; // Consume line number bytes for the *next* line
                }
                // Now pcode points to the start of the *next* statement after the error line.
                resume_pcode_next_statement = pcode;

                // Restore pcode to where it was for ERL to be accurate in the handler
                pcode = current_error_pcode_snapshot;

                Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (next_token == Tokens::ID::C_CR)
                    pcode++; // Consume the C_CR
            }
            else {
                // If the error handler function is somehow not found at runtime,
                // this is a fatal error, print the original error and stop.
                Error::set(caught_error_code, runtime_current_line); // Re-set original error
                Error::print();
                break; // Stop execution
            }
            continue; // Continue execution loop from the new pcode (the error handler)
        }
        else {
            if (Error::get() != 0 || is_stopped) {
                break;
            }
            if (pcode < active_p_code->size() && (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD)) {
                pcode++; // Consume the C_CR
            }
        }
    }

#ifdef SDL3
    // After the loop, ensure the graphics system is shut down
    graphics_system.shutdown();
    sound_system.shutdown();
#endif

    // Clear the pointer so it's not pointing to stale data
    active_p_code = prev_active_p_code;
    // Clear the global VM pointer when execution finishes
    g_vm_instance_ptr = nullptr;
}

void NeReLaBasic::execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode) {
    if (code_to_run.empty()) return;

    task_queue.clear();
    next_task_id = 0;

    auto main_task = std::make_shared<Task>();
    main_task->id = next_task_id++;
    main_task->status = TaskStatus::RUNNING;
    main_task->p_code_ptr = &code_to_run;
    main_task->p_code_counter = resume_mode ? this->pcode : 0;
    if (dap_handler) {
        main_task->debug_state = DebugState::PAUSED;
        dap_handler->send_stopped_message("entry", 0, this->program_to_debug);
    }
    task_queue[main_task->id] = main_task;

    Error::clear();

    while (!task_queue.empty()) {
#ifdef SDL3
        if (graphics_system.is_initialized) { if (!graphics_system.handle_events()) break; }
#endif
        if (!nopause_active && _kbhit()) {
            char key = _getch();
            if (key == 27) { TextIO::print("\n--- BREAK ---\n"); break; }
            else if (key == ' ') {
                TextIO::print("\n--- PAUSED (Press any key to resume) ---\n");
                _getch();
                TextIO::print("--- RESUMED ---\n");
            }
        }

        for (auto it = task_queue.begin(); it != task_queue.end(); ) {
            current_task = it->second.get();

            // --- Context Switch: Load ---
            this->pcode = current_task->p_code_counter;
            this->active_p_code = current_task->p_code_ptr;
            this->call_stack = current_task->call_stack;
            this->for_stack = current_task->for_stack;
            current_task->yielded_execution = false;

            bool task_removed = false;

            // --- Task Execution Logic ---
            if (current_task->status == TaskStatus::RUNNING) {
                // --- Task Completion & Error Check at the TOP ---
                if (Error::get() != 0 || pcode >= active_p_code->size() || 
                    (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD &&
                     static_cast<Tokens::ID>((*active_p_code)[pcode-1]) != Tokens::ID::C_CR)
                    ) {
                    current_task->status = (Error::get() != 0) ? TaskStatus::ERRORED : TaskStatus::COMPLETED;
                    if (variables.count("RETVAL")) {
                        current_task->result = variables["RETVAL"];
                    }
                    goto end_of_task_processing; // Skip to cleanup
                }
                // --- DAP and Breakpoint Logic ---
                if (dap_handler) {
                    if (current_task->debug_state == DebugState::PAUSED) {
                        pause_for_debugger();
                    }
                    runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
                    if (breakpoints.count(runtime_current_line)) {
                        if (current_task->debug_state != DebugState::STEP_OVER || call_stack.size() <= current_task->step_over_stack_depth) {
                            current_task->debug_state = DebugState::PAUSED;
                            dap_handler->send_stopped_message("breakpoint", runtime_current_line, this->program_to_debug);
                            pause_for_debugger();
                            continue; // Re-evaluate this task in the next scheduler cycle
                        }
                    }
                }

                // --- Main Statement Execution ---
                runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
                pcode += 2;

                bool line_is_done = false;
                bool statement_is_run = false;
                while (!line_is_done && pcode < active_p_code->size()) {
                    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR) {
                        statement();
                    }
                    if (Error::get() != 0 || is_stopped) {
                        line_is_done = true; // Stop processing this line on error
                        continue;
                    }
                    
                    // After the statement, check the token that follows.
                    if (pcode < active_p_code->size()) {
                        Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                        if (next_token == Tokens::ID::C_COLON) {
                            pcode++; // Consume the separator and loop again for the next statement.
                        }
                        else {
                            // Token (C_CR, NOCMD) means the line is finished.
                            if (next_token == Tokens::ID::C_CR || next_token == Tokens::ID::NOCMD) {
                                line_is_done = true;
                            }
                        }
                    }


                    // --- Post-Line Processing ---
                    if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                        pcode++;
                    }

                    // --- Post-Statement DAP Logic ---
                    if (dap_handler && (current_task->debug_state == DebugState::STEP_OVER && call_stack.size() <= current_task->step_over_stack_depth)) {
                        current_task->debug_state = DebugState::PAUSED;
                        uint16_t next_line = (pcode < active_p_code->size() - 1) ? (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8) : runtime_current_line;
                        dap_handler->send_stopped_message("step", next_line, this->program_to_debug);
                        pause_for_debugger();
                    }

                    // --- Error Handling Logic ---
                    if (Error::get() != 0 && current_task->error_handler_active && current_task->jump_to_error_handler) {
                        jump_to_error_handler = false; // Reset the flag
                        // Clear the actual error code so ERR/ERL can be read by the handler
                        uint8_t caught_error_code = Error::get();
                        Error::clear();

                        // Invoke the error handling function (like a CALLSUB)
                        // You can re-use your do_callsub logic or call it directly:
                        if (active_function_table->count(error_handler_function_name)) {
                            const auto& proc_info = active_function_table->at(error_handler_function_name);
                            // Call the procedure without arguments (ERR and ERL are global vars)
                            NeReLaBasic::StackFrame frame;
                            frame.return_p_code_ptr = this->active_p_code;
                            frame.return_pcode = this->pcode;
                            frame.previous_function_table_ptr = this->active_function_table;
                            frame.for_stack_size_on_entry = this->for_stack.size();
                            frame.function_name = error_handler_function_name;
                            frame.linenr = runtime_current_line;
                            // No args to pass if ERR and ERL are global
                            this->call_stack.push_back(frame);

                            if (!proc_info.module_name.empty() && compiled_modules.count(proc_info.module_name)) {
                                auto& target_module = compiled_modules[proc_info.module_name];
                                this->active_p_code = &target_module.p_code;
                                this->active_function_table = &target_module.function_table;
                            }
                            uint16_t current_error_pcode_snapshot = pcode; // Save where the error occurred

                            // Find the end of the current logical line
                            while (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::NOCMD) {
                                pcode++; // Move past the current problematic statement/expression
                            }
                            if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                                pcode++; // Consume C_CR
                                pcode += 2; // Consume line number bytes for the *next* line
                            }
                            // Now pcode points to the start of the *next* statement after the error line.
                            resume_pcode_next_statement = pcode;

                            // Restore pcode to where it was for ERL to be accurate in the handler
                            pcode = current_error_pcode_snapshot;

                            Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                            if (next_token == Tokens::ID::C_CR)
                                pcode++; // Consume the C_CR
                        }
                        else {
                            // If the error handler function is somehow not found at runtime,
                            // this is a fatal error, print the original error and stop.
                            Error::set(caught_error_code, runtime_current_line); // Re-set original error
                            Error::print();
                            break; // Stop execution
                        }
                        continue; // Continue execution loop from the new pcode (the error handler)
                    }
                }
            }
            else if (current_task->status == TaskStatus::PAUSED_ON_AWAIT) {
                if (current_task->awaiting_task && current_task->awaiting_task->status == TaskStatus::COMPLETED) {
                    current_task->status = TaskStatus::RUNNING;
                    current_task->awaiting_task = nullptr;
                }
            }

        end_of_task_processing:
            // --- Context Switch: Save ---
            current_task->p_code_counter = this->pcode;
            current_task->call_stack = this->call_stack;
            current_task->for_stack = this->for_stack;

            if (current_task->status == TaskStatus::COMPLETED || current_task->status == TaskStatus::ERRORED) {
                it = task_queue.erase(it);
                task_removed = true;
            }

            if (!task_removed) { ++it; }
        }

        if (task_queue.empty() || !task_queue.count(0)) { break; }
    }

#ifdef SDL3
    graphics_system.shutdown();
    sound_system.shutdown();
#endif
    current_task = nullptr;
}
void NeReLaBasic::statement() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]); // Peek at the token

    // In trace mode, print the token being executed.
    if (trace == 1) {
        TextIO::print("(");
        TextIO::print_uw(runtime_current_line);
        TextIO::print("/");
        TextIO::print_uwhex(static_cast<uint8_t>(token));
        TextIO::print(")");
    }

    switch (token) {

    case Tokens::ID::DIM:
        pcode++;
        Commands::do_dim(*this);
        break;

    case Tokens::ID::INPUT:
        pcode++;
        Commands::do_input(*this);
        break;

    case Tokens::ID::PRINT:
        pcode++;
        Commands::do_print(*this); // Pass a reference to ourselves
        break;

    case Tokens::ID::VARIANT:
    case Tokens::ID::INT:
    case Tokens::ID::STRVAR:
    case Tokens::ID::ARRAY_ACCESS:
    case Tokens::ID::MAP_ACCESS:
        //pcode++;
        Commands::do_let(*this);
        break;

    case Tokens::ID::GOTO:
        pcode++;
        Commands::do_goto(*this);
        break;

    case Tokens::ID::LABEL:
    case Tokens::ID::ENDIF:
        pcode++;
        // Compile-time only, do nothing at runtime.
        break;
    case Tokens::ID::IF:
        pcode++;
        Commands::do_if(*this);
        break;
    case Tokens::ID::ELSE:
        pcode++;
        Commands::do_else(*this);
        break;
    case Tokens::ID::FOR:
        pcode++;
        Commands::do_for(*this);
        break;
    case Tokens::ID::NEXT:
        pcode++;
        Commands::do_next(*this);
        break;
    case Tokens::ID::FUNC:
        pcode++;
        Commands::do_func(*this);
        break;
    case Tokens::ID::CALLFUNC:
        pcode++;
        Commands::do_callfunc(*this);
        break;
    case Tokens::ID::ENDFUNC:
        pcode++;
        Commands::do_endfunc(*this);
        break;
    case Tokens::ID::RETURN:
        pcode++;
        Commands::do_return(*this);
        break;
    case Tokens::ID::SUB:
        pcode++;
        Commands::do_sub(*this);
        break;
    case Tokens::ID::ENDSUB:
        pcode++;
        Commands::do_endsub(*this);
        break;
    case Tokens::ID::CALLSUB:
        pcode++;
        Commands::do_callsub(*this);
        break;
    case Tokens::ID::ONERRORCALL:
        pcode++;
        Commands::do_onerrorcall(*this);
        break;
    case Tokens::ID::RESUME:
        pcode++;
        Commands::do_resume(*this);
        break;
    case Tokens::ID::LOOP:
        pcode++;
        Commands::do_loop(*this);
        break;
    case Tokens::ID::DO:
        pcode++;
        Commands::do_do(*this);
        break;
    case Tokens::ID::EXIT_FOR:
        pcode++;
        Commands::do_exit_for(*this);
        break;
    case Tokens::ID::EXIT_DO:
        pcode++;
        Commands::do_exit_do(*this);
        break;
    case Tokens::ID::EDIT:
        pcode++;
        Commands::do_edit(*this);
        break;

    case Tokens::ID::LIST:
        pcode++;
        Commands::do_list(*this);
        break;

    case Tokens::ID::LOAD:
        pcode++;
        Commands::do_load(*this);
        break;

    case Tokens::ID::SAVE:
        pcode++;
        Commands::do_save(*this);
        break;

    case Tokens::ID::COMPILE:
        pcode++;
        Commands::do_compile(*this);
        break;

    case Tokens::ID::RUN:
        pcode++;
        Commands::do_run(*this);
        break;

    case Tokens::ID::STOP:
        pcode++;
        Commands::do_stop(*this);
        break;

    case Tokens::ID::TRON:
        pcode++;
        Commands::do_tron(*this);
        break;

    case Tokens::ID::TROFF:
        pcode++;
        Commands::do_troff(*this);
        break;

    case Tokens::ID::DUMP:
        pcode++;
        Commands::do_dump(*this);
        break;

    case Tokens::ID::C_CR:
        // This token is followed by a 2-byte line number. Skip it during execution.
        pcode++;
        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
        pcode += 2;
        break;
    case Tokens::ID::NOCMD: // we reached the end and do nothing more
        break;

    default:
        // If we don't recognize the token, something is wrong.
        // This could be a token that shouldn't be executed directly, like a NUMBER.
        // For now, we'll just stop. In the future, this would be a syntax error.
        pcode++;
        Error::set(1, runtime_current_line); // Syntax Error
        break;
    }
}

// --- CLASS MEMBER FUNCTION FOR PARSING ARRAY LITERALS ---
BasicValue NeReLaBasic::parse_array_literal() {
    // We expect the current token to be '['
    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTBRACKET) {
        Error::set(1, runtime_current_line); // Should not happen if called correctly
        return {};
    }

    std::vector<BasicValue> elements;
    // Loop until we find the closing bracket ']'
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
        while (true) {
            // Recursively call evaluate_expression, which can now handle nested literals
            elements.push_back(evaluate_expression());
            if (Error::get() != 0) return {};

            Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
            if (separator == Tokens::ID::C_RIGHTBRACKET) break;
            if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; }
            pcode++;
        }
    }
    //if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
    //    Error::set(16, runtime_current_line); return false;
    //}
    //else
        pcode++; // Consume ']'

    // --- Now, construct the Array object from the parsed elements ---
    auto new_array_ptr = std::make_shared<Array>();
    if (elements.empty()) {
        new_array_ptr->shape = { 0 };
        return new_array_ptr;
    }

    if (std::holds_alternative<std::shared_ptr<Array>>(elements[0])) {
        const auto& first_sub_array_ptr = std::get<std::shared_ptr<Array>>(elements[0]);
        new_array_ptr->shape.push_back(elements.size());
        if (first_sub_array_ptr) {
            for (size_t dim : first_sub_array_ptr->shape) {
                new_array_ptr->shape.push_back(dim);
            }
        }

        for (const auto& el : elements) {
            if (!std::holds_alternative<std::shared_ptr<Array>>(el)) { Error::set(15, runtime_current_line); return{}; }
            const auto& sub_array_ptr = std::get<std::shared_ptr<Array>>(el);
            if (!sub_array_ptr || sub_array_ptr->shape != first_sub_array_ptr->shape) { Error::set(15, runtime_current_line); return{}; }
            new_array_ptr->data.insert(new_array_ptr->data.end(), sub_array_ptr->data.begin(), sub_array_ptr->data.end());
        }
    }
    else {
        new_array_ptr->shape = { elements.size() };
        new_array_ptr->data = elements;
    }
    return new_array_ptr;
}

// --- FUNCTION TO PARSE MAP LITERALS ---
BasicValue NeReLaBasic::parse_map_literal() {
    // We expect the current token to be '{'
    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTBRACE) {
        Error::set(1, runtime_current_line, "Expected '{' to start map literal.");
        return {};
    }

    auto new_map_ptr = std::make_shared<Map>();

    // Check for an empty map {}
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTBRACE) {
        pcode++; // Consume '}'
        return new_map_ptr;
    }

    while (true) {
        // 1. Parse Key (must be a string)
        BasicValue key_val = evaluate_expression();
        if (Error::get() != 0) return {};
        std::string key_str = to_string(key_val);

        // 2. Expect Colon
        if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_COLON) {
            Error::set(1, runtime_current_line, "Expected ':' after map key.");
            return {};
        }

        // 3. Parse Value
        BasicValue value = evaluate_expression();
        if (Error::get() != 0) return {};

        // 4. Add to map
        new_map_ptr->data[key_str] = value;

        // 5. Check for separator
        Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (separator == Tokens::ID::C_RIGHTBRACE) {
            pcode++; // Consume '}'
            break; // End of map
        }
        if (separator != Tokens::ID::C_COMMA) {
            Error::set(1, runtime_current_line, "Expected ',' or '}' in map literal.");
            return {};
        }
        pcode++; // Consume ','
    }

    return new_map_ptr;
}

// Level 5: Handles highest-precedence items
BasicValue NeReLaBasic::parse_primary() {
    BasicValue current_value;
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
    if (token == Tokens::ID::OP_START_TASK) {
        pcode++; // consume OP_START_TASK
        std::string func_name = to_upper(read_string(*this));
        if (!active_function_table->count(func_name)) {
            Error::set(22, runtime_current_line, "Async function not found: " + func_name);
            return {};
        }
        const auto& func_info = active_function_table->at(func_name);
        std::vector<BasicValue> args;
        if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
            while (true) {
                args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                if (separator == Tokens::ID::C_RIGHTPAREN) break;
                if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
            }
        }
        pcode++; // Consume ')'

        auto new_task = std::make_shared<Task>();
        new_task->id = next_task_id++;
        new_task->status = TaskStatus::RUNNING;
        if (!func_info.module_name.empty() && compiled_modules.count(func_info.module_name)) {
            new_task->p_code_ptr = &compiled_modules.at(func_info.module_name).p_code;
        }
        else {
            new_task->p_code_ptr = &program_p_code;
        }
        new_task->p_code_counter = func_info.start_pcode;

        StackFrame frame;
        frame.function_name = func_name;
        for (size_t i = 0; i < func_info.parameter_names.size(); ++i) {
            if (i < args.size()) frame.local_variables[func_info.parameter_names[i]] = args[i];
        }
        new_task->call_stack.push_back(frame);
        task_queue[new_task->id] = new_task;
        current_value = TaskRef{ new_task->id };

    }
    else {
        if (token == Tokens::ID::VARIANT || token == Tokens::ID::INT || token == Tokens::ID::STRVAR) {
            pcode++;
            std::string var_or_qual_name = to_upper(read_string(*this));

            if (var_or_qual_name.find('.') != std::string::npos) {
                auto [final_obj, final_member] = resolve_dot_chain(var_or_qual_name);
                if (Error::get() != 0) return {};
                if (final_member.empty()) { current_value = final_obj; }
                else if (std::holds_alternative<std::shared_ptr<Map>>(final_obj)) {
                    auto& map_ptr = std::get<std::shared_ptr<Map>>(final_obj);
                    if (map_ptr && map_ptr->data.count(final_member)) {
                        current_value = map_ptr->data.at(final_member);
                    }
                    else { Error::set(3, runtime_current_line, "Member not found: " + final_member); return {}; }
                }
                else if (std::holds_alternative<std::shared_ptr<Tensor>>(final_obj)) {
                    const auto& tensor_ptr = std::get<std::shared_ptr<Tensor>>(final_obj);
                    if (!tensor_ptr) {
                        Error::set(3, runtime_current_line, "Cannot access member of a null Tensor.");
                        return {};
                    }
                    if (final_member == "GRAD") {
                        if (tensor_ptr->grad) {
                            current_value = tensor_ptr->grad;
                        }
                        else {
                            // If .grad is accessed but doesn't exist yet, return a valid but empty Tensor.
                            current_value = std::make_shared<Tensor>();
                        }
                    }
                    else {
                        Error::set(3, runtime_current_line, "Invalid member for Tensor: " + final_member + ". Only .grad is supported.");
                        return {};
                    }
                }
#ifdef JDCOM
                else if (std::holds_alternative<ComObject>(final_obj)) {
                    IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
                    _variant_t result_vt;
                    HRESULT hr = invoke_com_method(pDisp, final_member, {}, result_vt, DISPATCH_PROPERTYGET);
                    if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM property not found: " + final_member); return {}; }
                    current_value = variant_t_to_basic_value(result_vt, *this);
                }
#endif
                else { Error::set(15, runtime_current_line, "Invalid object for dot notation."); return {}; }
            }
            else {
                current_value = get_variable(*this, var_or_qual_name);
            }
        }
        else if (token == Tokens::ID::CALLFUNC) {
            pcode++;
            std::string identifier_being_called = to_upper(read_string(*this));
            std::string real_func_to_call = identifier_being_called;

            if (!active_function_table->count(real_func_to_call)) {
                BasicValue& var = get_variable(*this, identifier_being_called);
                if (std::holds_alternative<FunctionRef>(var)) {
                    real_func_to_call = std::get<FunctionRef>(var).name;
                }
            }

            if (active_function_table->count(real_func_to_call)) {
                const auto& func_info = active_function_table->at(real_func_to_call);
                std::vector<BasicValue> args;
                if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
                if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
                    while (true) {
                        args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                        Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                        if (separator == Tokens::ID::C_RIGHTPAREN) break;
                        if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
                    }
                }
                pcode++;
                if (func_info.arity != -1 && args.size() != func_info.arity) { Error::set(26, runtime_current_line); return {}; }
                current_value = execute_function_for_value(func_info, args);
            }
            else if (identifier_being_called.find('.') != std::string::npos) {
#ifdef JDCOM
                auto [final_obj, final_method] = resolve_dot_chain(identifier_being_called);
                if (Error::get() != 0) return {};
                if (!std::holds_alternative<ComObject>(final_obj)) {
                    Error::set(15, runtime_current_line, "Methods can only be called on COM objects."); return {};
                }
                IDispatchPtr pDisp = std::get<ComObject>(final_obj).ptr;
                if (!pDisp) { Error::set(1, runtime_current_line, "Uninitialized COM object."); return {}; }
                std::vector<BasicValue> com_args;
                if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) { Error::set(1, runtime_current_line); return {}; }
                if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
                    while (true) {
                        com_args.push_back(evaluate_expression()); if (Error::get() != 0) return {};
                        Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                        if (separator == Tokens::ID::C_RIGHTPAREN) break;
                        if (separator != Tokens::ID::C_COMMA) { Error::set(1, runtime_current_line); return {}; } pcode++;
                    }
                }
                pcode++;
                _variant_t result_vt;
                HRESULT hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_METHOD);
                if (FAILED(hr)) {
                    hr = invoke_com_method(pDisp, final_method, com_args, result_vt, DISPATCH_PROPERTYGET);
                    if (FAILED(hr)) { Error::set(12, runtime_current_line, "Failed to call COM method or get property '" + final_method + "'"); return {}; }
                }
                current_value = variant_t_to_basic_value(result_vt, *this);
#else
                Error::set(22, runtime_current_line, "Unknown function: " + identifier_being_called); return {};
#endif
            }
            else { Error::set(22, runtime_current_line, "Unknown function: " + real_func_to_call); return {}; }
        }
        else if (token == Tokens::ID::JD_TRUE) {
            pcode++; current_value = true;
        }
        else if (token == Tokens::ID::JD_FALSE) {
            pcode++; current_value = false;
        }
        else if (token == Tokens::ID::NUMBER) {
            pcode++; double value;
            memcpy(&value, &(*active_p_code)[pcode], sizeof(double));
            pcode += sizeof(double);
            current_value = value;
        }
        else if (token == Tokens::ID::STRING) {
            pcode++; current_value = read_string(*this);
        }
        else if (token == Tokens::ID::CONSTANT) {
            pcode++; current_value = builtin_constants.at(read_string(*this));
        }
        else if (token == Tokens::ID::FUNCREF) {
            pcode++; current_value = FunctionRef{ to_upper(read_string(*this)) };
        }
        else if (token == Tokens::ID::C_LEFTBRACKET) {
            current_value = parse_array_literal();
        }
        else if (token == Tokens::ID::C_LEFTBRACE) {
            current_value = parse_map_literal();
        }
        else if (token == Tokens::ID::C_LEFTPAREN) {
            pcode++; current_value = evaluate_expression();
            if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTPAREN) { Error::set(18, runtime_current_line); return {}; }
        }
        else { Error::set(1, runtime_current_line); return {}; }
        if (Error::get() != 0) return {};
    }

    // --- MAIN LOOP FOR HANDLING ACCESSORS LIKE [..], {..}, .member ---
    while (true) {
        Tokens::ID accessor_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

        if (accessor_token == Tokens::ID::C_LEFTBRACKET) { // Array index access: e.g., A[i, j]
            pcode++; // Consume '['

            std::vector<size_t> indices;
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
                while (true) {
                    BasicValue index_val = evaluate_expression();
                    if (Error::get() != 0) return {};
                    indices.push_back(static_cast<size_t>(to_double(index_val)));

                    Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                    if (separator == Tokens::ID::C_RIGHTBRACKET) break;
                    if (separator != Tokens::ID::C_COMMA) {
                        Error::set(1, runtime_current_line, "Expected ',' or ']' in array index.");
                        return {};
                    }
                    pcode++; // Consume ','
                }
            }
            if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTBRACKET) {
                Error::set(16, runtime_current_line, "Missing ']' in array access.");
                return {};
            }

            if (std::holds_alternative<std::shared_ptr<Array>>(current_value)) {
                const auto& arr_ptr = std::get<std::shared_ptr<Array>>(current_value);
                if (!arr_ptr) { Error::set(15, runtime_current_line, "Attempt to index a null array."); return {}; }

                try {
                    size_t flat_index = arr_ptr->get_flat_index(indices);
                    // The get_flat_index function already checks bounds, but an extra check is safe.
                    if (flat_index >= arr_ptr->data.size()) {
                        throw std::out_of_range("Calculated index is out of bounds.");
                    }
                    BasicValue next_val = arr_ptr->data[flat_index];
                    current_value = std::move(next_val);
                    //current_value = arr_ptr->data[flat_index]; // Update current value with the element
                }
                catch (const std::exception&) {
                    // This catches errors from get_flat_index (dimension mismatch or out of bounds)
                    Error::set(10, runtime_current_line, "Array index out of bounds or dimension mismatch.");
                    return {};
                }
            }
            else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
                if (indices.size() != 1) {
                    Error::set(15, runtime_current_line, "Multi-dimensional indexing is not supported for JSON objects."); return {};
                }
                size_t index = indices[0];
                const auto& json_ptr = std::get<std::shared_ptr<JsonObject>>(current_value);
                if (!json_ptr || !json_ptr->data.is_array() || index >= json_ptr->data.size()) { Error::set(10, runtime_current_line, "JSON index out of bounds."); return {}; }
                current_value = json_to_basic_value(json_ptr->data[index]);
            }
            else { Error::set(15, runtime_current_line, "Indexing '[]' can only be used on an Array."); return {}; }

        }
        else if (accessor_token == Tokens::ID::C_LEFTBRACE) { // Map key access: {key}
            pcode++; // Consume '{'
            BasicValue key_val = evaluate_expression();
            if (Error::get() != 0) return {};
            std::string key = to_string(key_val);
            if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTBRACE) { Error::set(17, runtime_current_line, "Missing '}' in map access."); return {}; }

            if (std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
                const auto& map_ptr = std::get<std::shared_ptr<Map>>(current_value);
                if (!map_ptr || map_ptr->data.find(key) == map_ptr->data.end()) { Error::set(3, runtime_current_line, "Map key not found: " + key); return {}; }
                BasicValue next_val = map_ptr->data.at(key);
                current_value = std::move(next_val);
            }
            else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
                const auto& json_ptr = std::get<std::shared_ptr<JsonObject>>(current_value);
                if (!json_ptr || !json_ptr->data.is_object() || !json_ptr->data.contains(key)) { Error::set(3, runtime_current_line, "JSON key not found: " + key); return {}; }
                current_value = json_to_basic_value(json_ptr->data.at(key));
            }
            else { Error::set(15, runtime_current_line, "Key access '{}' can only be used on a Map or JSON object."); return {}; }

        }
        else if (accessor_token == Tokens::ID::C_DOT) {
            pcode++; // Consume '.'
            Tokens::ID member_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
            if (member_token != Tokens::ID::VARIANT && member_token != Tokens::ID::INT && member_token != Tokens::ID::STRVAR) {
                Error::set(1, runtime_current_line, "Expected member name after '.'"); return {};
            }
            pcode++;
            std::string member_name = to_upper(read_string(*this));

            if (std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
                const auto& map_ptr = std::get<std::shared_ptr<Map>>(current_value);
                if (!map_ptr || map_ptr->data.find(member_name) == map_ptr->data.end()) { Error::set(3, runtime_current_line, "Member '" + member_name + "' not found in object."); return {}; }
                current_value = map_ptr->data.at(member_name);
            }
#ifdef JDCOM
            else if (std::holds_alternative<ComObject>(current_value)) {
                IDispatchPtr pDisp = std::get<ComObject>(current_value).ptr;
                _variant_t result_vt;
                HRESULT hr = invoke_com_method(pDisp, member_name, {}, result_vt, DISPATCH_PROPERTYGET);
                if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM property '" + member_name + "' not found or failed to get."); return {}; }
                current_value = variant_t_to_basic_value(result_vt, *this);
            }
#endif
            else { Error::set(15, runtime_current_line, "Member access '.' can only be used on an object."); return {}; }
        }
        else {
            break; // No more accessors, break the loop
        }
    }
    return current_value;
}

// Level 5: Handles unary operators like - and NOT
BasicValue NeReLaBasic::parse_unary() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::AWAIT) {
        pcode++; // Consume AWAIT
        BasicValue task_ref_val = parse_unary(); // AWAIT has high precedence
        if (Error::get() != 0) return {};

        if (!std::holds_alternative<TaskRef>(task_ref_val)) {
            Error::set(15, runtime_current_line, "Can only AWAIT a TaskRef object.");
            return {};
        }
        int task_id_to_await = std::get<TaskRef>(task_ref_val).id;

        if (task_queue.count(task_id_to_await)) {
            auto& task_to_await = task_queue.at(task_id_to_await);
            if (task_to_await->status == TaskStatus::COMPLETED) {
                return task_to_await->result;
            }
            else {
                current_task->status = TaskStatus::PAUSED_ON_AWAIT;
                current_task->awaiting_task = task_to_await;
                current_task->yielded_execution = true;
                return false; // Dummy value, will be re-evaluated
            }
        }
        else {
            // Task not found, assume it's done.
            return false; // Return default value
        }
    }

    if (token == Tokens::ID::C_MINUS) {
        pcode++; // Consume the '-'
        // Recursively call to handle expressions like -(5+2) or -x
        // Note: The result of a unary minus is always a number.
        return -to_double(parse_unary());
    }
    if (token == Tokens::ID::NOT) {
        pcode++; // Consume 'NOT'
        // The result of NOT is always a boolean.
        return !to_bool(parse_unary());
    }

    // If there is no unary operator, parse the primary expression.
    return parse_primary();
}

BasicValue NeReLaBasic::parse_power() {
    BasicValue left = parse_unary();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::C_CARET) {
            pcode++;
            BasicValue right = parse_unary();

            if (std::holds_alternative<std::shared_ptr<Tensor>>(left)) {
                if (std::holds_alternative<std::shared_ptr<Tensor>>(right)) {
                    Error::set(1, runtime_current_line, "Tensor-to-Tensor power is not supported."); return {};
                }
                left = tensor_power(*this, left, right);
            }
            else {
                left = std::visit([this](auto&& l, auto&& r) -> BasicValue {
                    using LeftT = std::decay_t<decltype(l)>;
                    using RightT = std::decay_t<decltype(r)>;

                    auto array_op = [this](const auto& arr, double scalar, bool arr_is_left) -> BasicValue {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = arr->shape;
                        for (const auto& elem : arr->data) {
                            double arr_val = to_double(elem);
                            double res = arr_is_left ? std::pow(arr_val, scalar) : std::pow(scalar, arr_val);
                            result_ptr->data.push_back(res);
                        }
                        return result_ptr;
                        };

                    if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Shape mismatch for element-wise power."); return false; }
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = l->shape;
                        for (size_t i = 0; i < l->data.size(); ++i) { result_ptr->data.push_back(std::pow(to_double(l->data[i]), to_double(r->data[i]))); }
                        return result_ptr;
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) { return array_op(l, to_double(r), true); }
                    else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) { return array_op(r, to_double(l), false); }
                    else { return std::pow(to_double(l), to_double(r)); }
                    }, left, right);
            }
        }
        else { break; }
    }
    return left;
}

// Level 3: Handles *, /, and MOD
BasicValue NeReLaBasic::parse_factor() {
    BasicValue left = parse_power();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::C_ASTR || op == Tokens::ID::C_SLASH || op == Tokens::ID::MOD) {
            pcode++;
            BasicValue right = parse_power();

            bool is_left_tensor = std::holds_alternative<std::shared_ptr<Tensor>>(left);

            if (is_left_tensor) {
                if (op == Tokens::ID::C_ASTR) {
                    if (!std::holds_alternative<std::shared_ptr<Tensor>>(right)) { Error::set(15, runtime_current_line, "Tensor can only be element-wise multiplied by another Tensor."); return {}; }
                    left = tensor_elementwise_multiply(*this, left, right);
                }
                else if (op == Tokens::ID::C_SLASH) {
                    if (std::holds_alternative<std::shared_ptr<Tensor>>(right)) { Error::set(1, runtime_current_line, "Tensor-by-Tensor division is not supported."); return {}; }
                    left = tensor_scalar_divide(*this, left, right);
                }
                else { // MOD
                    Error::set(1, runtime_current_line, "MOD operator is not supported for Tensors."); return {};
                }
            }
            else {
                left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
                    using LeftT = std::decay_t<decltype(l)>; using RightT = std::decay_t<decltype(r)>;

                    auto array_op = [op, this](const auto& arr, double scalar, bool arr_is_left) -> BasicValue {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = arr->shape;
                        for (const auto& elem : arr->data) {
                            double arr_val = to_double(elem); double res = 0;
                            if (op == Tokens::ID::C_ASTR) res = arr_is_left ? arr_val * scalar : scalar * arr_val;
                            else if (op == Tokens::ID::C_SLASH) {
                                if ((arr_is_left && scalar == 0.0) || (!arr_is_left && arr_val == 0.0)) { Error::set(2, runtime_current_line, "Division by zero."); return false; }
                                res = arr_is_left ? arr_val / scalar : scalar / arr_val;
                            }
                            else { // MOD
                                if ((arr_is_left && scalar == 0.0) || (!arr_is_left && arr_val == 0.0)) { Error::set(2, runtime_current_line, "Division by zero."); return false; }
                                res = static_cast<double>(arr_is_left ? static_cast<long long>(arr_val) % static_cast<long long>(scalar) : static_cast<long long>(scalar) % static_cast<long long>(arr_val));
                            }
                            result_ptr->data.push_back(res);
                        }
                        return result_ptr;
                        };

                    if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Shape mismatch for element-wise operation."); return false; }
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = l->shape;
                        for (size_t i = 0; i < l->data.size(); ++i) {
                            double val_l = to_double(l->data[i]); double val_r = to_double(r->data[i]); double res = 0;
                            if (op == Tokens::ID::C_ASTR) res = val_l * val_r;
                            else if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line, "Division by zero."); return false; } res = val_l / val_r; }
                            else { if (val_r == 0.0) { Error::set(2, runtime_current_line, "Division by zero."); return false; } res = static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                            result_ptr->data.push_back(res);
                        }
                        return result_ptr;
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) { return array_op(l, to_double(r), true); }
                    else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) { return array_op(r, to_double(l), false); }
                    else { // scalar op
                        double val_l = to_double(l); double val_r = to_double(r);
                        if (op == Tokens::ID::C_ASTR) return val_l * val_r;
                        if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                        if (op == Tokens::ID::MOD) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                    }
                    return false;
                    }, left, right);
            }
        }
        else { break; }
    }
    return left;
}

// Level 2: Handles + and -
BasicValue NeReLaBasic::parse_term() {
    BasicValue left = parse_factor();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::C_PLUS || op == Tokens::ID::C_MINUS) {
            pcode++;
            BasicValue right = parse_factor();
            if (std::holds_alternative<std::shared_ptr<Tensor>>(left) || std::holds_alternative<std::shared_ptr<Tensor>>(right)) {
                if (!std::holds_alternative<std::shared_ptr<Tensor>>(left) || !std::holds_alternative<std::shared_ptr<Tensor>>(right)) {
                    Error::set(15, runtime_current_line, "Cannot mix Tensor and non-Tensor types in add/subtract."); return {};
                }
                if (op == Tokens::ID::C_PLUS) { left = tensor_add(*this, left, right); }
                else { left = tensor_subtract(*this, left, right); }
            }
            else {
                left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
                    using LeftT = std::decay_t<decltype(l)>; using RightT = std::decay_t<decltype(r)>;
                    if constexpr (std::is_same_v<LeftT, std::string> || std::is_same_v<RightT, std::string>) {
                        if (op == Tokens::ID::C_PLUS) return to_string(l) + to_string(r);
                        else { Error::set(15, runtime_current_line, "Cannot subtract strings."); return false; }
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        // This uses the same array_add/subtract helpers from BuiltinFunctions.cpp
                        // Ensure they are globally accessible or duplicate the logic here.
                        if (op == Tokens::ID::C_PLUS) return array_add(l, r); else return array_subtract(l, r);
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = l->shape;
                        double scalar = to_double(r);
                        for (const auto& elem : l->data) { result_ptr->data.push_back(op == Tokens::ID::C_PLUS ? to_double(elem) + scalar : to_double(elem) - scalar); }
                        return result_ptr;
                    }
                    else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        auto result_ptr = std::make_shared<Array>(); result_ptr->shape = r->shape;
                        double scalar = to_double(l);
                        for (const auto& elem : r->data) { result_ptr->data.push_back(op == Tokens::ID::C_PLUS ? scalar + to_double(elem) : scalar - to_double(elem)); }
                        return result_ptr;
                    }
                    else {
                        if (op == Tokens::ID::C_PLUS) return to_double(l) + to_double(r);
                        else return to_double(l) - to_double(r);
                    }
                    }, left, right);
            }
        }
        else { break; }
    }
    return left;
}

// Level 2: Handles <, >, = with element-wise array operations
BasicValue NeReLaBasic::parse_comparison() {
    BasicValue left = parse_term(); // parse_term handles + and -

    Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    // Check if the next token is ANY of our comparison operators
    if (op == Tokens::ID::C_EQ || op == Tokens::ID::C_LT || op == Tokens::ID::C_GT ||
        op == Tokens::ID::C_NE || op == Tokens::ID::C_LE || op == Tokens::ID::C_GE)
    {
        pcode++; // Consume the operator
        BasicValue right = parse_term();

        // Use std::visit to handle all type combinations
        left = std::visit([op, this, &left, &right](auto&& l, auto&& r) -> BasicValue {
            using LeftT = std::decay_t<decltype(l)>;
            using RightT = std::decay_t<decltype(r)>;

            // --- ARRAY COMPARISON LOGIC ---

            // Case 1: Array-Array comparison
            if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                if (!l || !r) { Error::set(15, runtime_current_line, "Comparison with null array."); return false; }
                if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Array shape mismatch in comparison."); return false; }

                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = l->shape;
                result_ptr->data.reserve(l->data.size());

                for (size_t i = 0; i < l->data.size(); ++i) {
                    double left_val = to_double(l->data[i]);
                    double right_val = to_double(r->data[i]);
                    bool result = false;
                    switch (op) {
                    case Tokens::ID::C_EQ: result = (left_val == right_val); break;
                    case Tokens::ID::C_NE: result = (left_val != right_val); break;
                    case Tokens::ID::C_LT: result = (left_val < right_val); break;
                    case Tokens::ID::C_GT: result = (left_val > right_val); break;
                    case Tokens::ID::C_LE: result = (left_val <= right_val); break;
                    case Tokens::ID::C_GE: result = (left_val >= right_val); break;
                    }
                    result_ptr->data.push_back(result);
                }
                return result_ptr;
            }
            // Case 2: Array-Scalar comparison
            else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) {
                if (!l) { Error::set(15, runtime_current_line, "Comparison with null array."); return false; }
                double scalar = to_double(r);
                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = l->shape;
                result_ptr->data.reserve(l->data.size());
                for (const auto& elem : l->data) {
                    double left_val = to_double(elem);
                    bool result = false;
                    switch (op) {
                    case Tokens::ID::C_EQ: result = (left_val == scalar); break;
                    case Tokens::ID::C_NE: result = (left_val != scalar); break;
                    case Tokens::ID::C_LT: result = (left_val < scalar); break;
                    case Tokens::ID::C_GT: result = (left_val > scalar); break;
                    case Tokens::ID::C_LE: result = (left_val <= scalar); break;
                    case Tokens::ID::C_GE: result = (left_val >= scalar); break;
                    }
                    result_ptr->data.push_back(result);
                }
                return result_ptr;
            }
            // Case 3: Scalar-Array comparison
            else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) {
                if (!r) { Error::set(15, runtime_current_line, "Comparison with null array."); return false; }
                double scalar = to_double(l);
                auto result_ptr = std::make_shared<Array>();
                result_ptr->shape = r->shape;
                result_ptr->data.reserve(r->data.size());
                for (const auto& elem : r->data) {
                    double right_val = to_double(elem);
                    bool result = false;
                    switch (op) {
                    case Tokens::ID::C_EQ: result = (scalar == right_val); break;
                    case Tokens::ID::C_NE: result = (scalar != right_val); break;
                    case Tokens::ID::C_LT: result = (scalar < right_val); break;
                    case Tokens::ID::C_GT: result = (scalar > right_val); break;
                    case Tokens::ID::C_LE: result = (scalar <= right_val); break;
                    case Tokens::ID::C_GE: result = (scalar >= right_val); break;
                    }
                    result_ptr->data.push_back(result);
                }
                return result_ptr;
            }

            // --- EXISTING SCALAR COMPARISON LOGIC (Unchanged) ---

            // Check the type of the ORIGINAL variant objects.
            else if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                // If either is a string, we compare them as strings.
                if (op == Tokens::ID::C_EQ) return to_string(l) == to_string(r);
                if (op == Tokens::ID::C_NE) return to_string(l) != to_string(r);
                if (op == Tokens::ID::C_LT) return to_string(l) < to_string(r);
                if (op == Tokens::ID::C_GT) return to_string(l) > to_string(r);
                if (op == Tokens::ID::C_LE) return to_string(l) <= to_string(r);
                if (op == Tokens::ID::C_GE) return to_string(l) >= to_string(r);
            }
            // Priority 2: If BOTH operands are DateTime, compare their internal time_points.
            else if (std::holds_alternative<DateTime>(left) && std::holds_alternative<DateTime>(right)) {
                const auto& dt_l = std::get<DateTime>(left);
                const auto& dt_r = std::get<DateTime>(right);
                if (op == Tokens::ID::C_EQ) return dt_l.time_point == dt_r.time_point;
                if (op == Tokens::ID::C_NE) return dt_l.time_point != dt_r.time_point;
                if (op == Tokens::ID::C_LT) return dt_l.time_point < dt_r.time_point;
                if (op == Tokens::ID::C_GT) return dt_l.time_point > dt_r.time_point;
                if (op == Tokens::ID::C_LE) return dt_l.time_point <= dt_r.time_point;
                if (op == Tokens::ID::C_GE) return dt_l.time_point >= dt_r.time_point;
            }
            else {
                // Otherwise, both are numeric (double or bool), so we compare them as numbers.
                if (op == Tokens::ID::C_EQ) return to_double(l) == to_double(r);
                if (op == Tokens::ID::C_NE) return to_double(l) != to_double(r);
                if (op == Tokens::ID::C_LT) return to_double(l) < to_double(r);
                if (op == Tokens::ID::C_GT) return to_double(l) > to_double(r);
                if (op == Tokens::ID::C_LE) return to_double(l) <= to_double(r);
                if (op == Tokens::ID::C_GE) return to_double(l) >= to_double(r);
            }
            return false; // Should not be reached
            }, left, right);
    }

    return left;
}

// Level 1: Handles AND, OR
// Level 1: Handles AND, OR, XOR with element-wise array support
BasicValue NeReLaBasic::evaluate_expression() {
    BasicValue left = parse_comparison();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::AND || op == Tokens::ID::OR || op == Tokens::ID::XOR) {
            pcode++;
            BasicValue right = parse_comparison();

            left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
                using LeftT = std::decay_t<decltype(l)>;
                using RightT = std::decay_t<decltype(r)>;

                // Helper lambda for Array-Scalar operations
                auto array_scalar_op = [op, this](const auto& arr_ptr, const auto& scalar_val) -> BasicValue {
                    if (!arr_ptr) { Error::set(15, runtime_current_line, "Operation on null array."); return false; }
                    bool scalar_bool = to_bool(scalar_val);
                    auto result_ptr = std::make_shared<Array>();
                    result_ptr->shape = arr_ptr->shape;
                    result_ptr->data.reserve(arr_ptr->data.size());

                    for (const auto& elem : arr_ptr->data) {
                        bool elem_bool = to_bool(elem);
                        bool result = false;
                        if (op == Tokens::ID::AND) result = elem_bool && scalar_bool;
                        else if (op == Tokens::ID::OR) result = elem_bool || scalar_bool;
                        else if (op == Tokens::ID::XOR) result = elem_bool != scalar_bool;
                        result_ptr->data.push_back(result);
                    }
                    return result_ptr;
                    };

                // Case 1: Array AND/OR/XOR Array
                if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                    if (!l || !r) { Error::set(15, runtime_current_line, "Operation on null array."); return false; }
                    if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Array shape mismatch in logical operation."); return false; }

                    auto result_ptr = std::make_shared<Array>();
                    result_ptr->shape = l->shape;
                    result_ptr->data.reserve(l->data.size());

                    for (size_t i = 0; i < l->data.size(); ++i) {
                        bool left_bool = to_bool(l->data[i]);
                        bool right_bool = to_bool(r->data[i]);
                        bool result = false;
                        if (op == Tokens::ID::AND) result = left_bool && right_bool;
                        else if (op == Tokens::ID::OR) result = left_bool || right_bool;
                        else if (op == Tokens::ID::XOR) result = left_bool != right_bool; // Logical XOR
                        result_ptr->data.push_back(result);
                    }
                    return result_ptr;
                }
                // Case 2: Array op Scalar
                else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) {
                    return array_scalar_op(l, r);
                }
                // Case 3: Scalar op Array
                else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) {
                    return array_scalar_op(r, l);
                }
                // Case 4: Scalar op Scalar (fallback)
                else {
                    bool left_bool = to_bool(l);
                    bool right_bool = to_bool(r);
                    if (op == Tokens::ID::AND) return left_bool && right_bool;
                    if (op == Tokens::ID::OR) return left_bool || right_bool;
                    if (op == Tokens::ID::XOR) return left_bool != right_bool; // Logical XOR
                }

                return false; // Should not be reached
                }, left, right);
        }
        else {
            break;
        }
    }
    return left;
}