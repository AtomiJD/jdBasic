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
#include "ModuleInterface.h"
#include <iostream>
#include <fstream>   // For std::ifstream
#include <string>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#include <conio.h>
#else
#include <ncurses.h>
#endif
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


const std::string NERELA_VERSION = "0.9.2";

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

// --- The chain resolver now handles both UDTs (Maps) and COM Objects ---
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
    //BasicValue current_object = get_variable(*this, to_upper(parts[0]));
    BasicValue current_object;
    std::string base = to_upper(parts[0]);

    if (base == "THIS") {
        // Resolve THIS from the current call stack
        if (!this_stack.empty()) {
            // CORRECTED: The object on the stack *is* THIS.
            current_object = this_stack.back();
        }
        else {
            Error::set(13, runtime_current_line, "`THIS` used outside method block.");
            return {};
        }
    }
    else {
        current_object = get_variable(*this, base);
    }

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
    //builtin_constants["ERR"] = 0.0;
    //builtin_constants["ERL"] = 0.0;
    //builtin_constants["ERRMSG"] = "";

    debug_state = DebugState::RUNNING;
    step_over_stack_depth = 0;
    step_out_stack_depth = 0;

}

NeReLaBasic::~NeReLaBasic() {
    // --- NEW: Unload any dynamically loaded libraries on exit ---
    for (auto& lib_handle : loaded_libraries) {
#ifdef _WIN32
        FreeLibrary(lib_handle);
#else
        dlclose(lib_handle);
#endif
    }
}

#ifdef _WIN32
// Converts a std::string (UTF-8) to a std::wstring (UTF-16)
std::wstring string_to_wstring(const std::string& str) {
    if (str.empty()) {
        return L"";
    }

    // 1. Get the required buffer size for the wide string.
    //    The last parameter is -1 for null-terminated strings.
    int required_size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (required_size == 0) {
        // Handle error if needed, e.g., by throwing an exception or returning an empty string.
        return L"";
    }

    // 2. Create a std::wstring with the required size.
    std::wstring wstr(required_size - 1, 0); // -1 because required_size includes the null terminator

    // 3. Perform the actual conversion.
    int bytes_converted = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], required_size);
    if (bytes_converted == 0) {
        // Handle error
        return L"";
    }

    return wstr;
}
#endif

// --- Implementation for loading a dynamic module ---
bool NeReLaBasic::load_dynamic_module(const std::string& module_path) {
    std::string full_path = module_path;
    

    // --- Define the new registration function pointer type ---
    using RegisterModuleWithServicesFunc = void(*)(NeReLaBasic*, ModuleServices*);

#ifdef _WIN32
    if (full_path.size() < 4 || full_path.substr(full_path.size() - 4) != ".dll") {
        full_path += ".dll";
    }

    std::wstring wfull_path = string_to_wstring(full_path);
    LPCWSTR my_lpcwstr = wfull_path.c_str();
    HMODULE lib_handle = LoadLibrary(my_lpcwstr);
    if (!lib_handle) {
        Error::set(6, runtime_current_line, "Failed to load DLL: " + full_path + " (Error code: " + std::to_string(GetLastError()) + ")");
        return false;
    }

    auto register_func = (RegisterModuleWithServicesFunc)GetProcAddress(lib_handle, "jdBasic_register_module");
    if (!register_func) {
        Error::set(22, runtime_current_line, "Cannot find 'jdBasic_register_module' entry point in " + full_path);
        FreeLibrary(lib_handle);
        return false;
    }
#else
    if (full_path.size() < 3 || full_path.substr(full_path.size() - 3) != ".so") {
        full_path += ".so";
    }

    void* lib_handle = dlopen(full_path.c_str(), RTLD_LAZY);
    if (!lib_handle) {
        Error::set(6, runtime_current_line, "Failed to load shared library: " + std::string(dlerror()));
        return false;
    }

    auto register_func = (RegisterModuleWithServicesFunc)dlsym(lib_handle, "jdBasic_register_module");
    const char* dlsym_error = dlerror();
    if (dlsym_error) {
        Error::set(22, runtime_current_line, "Cannot find symbol 'jdBasic_register_module': " + std::string(dlsym_error));
        dlclose(lib_handle);
        return false;
    }
#endif

    // --- NEW: Create and populate the services struct ---
    ModuleServices services;
    services.error_set = &Error::set;
    services.to_upper = &to_upper; // This assumes to_upper is a free function.
    services.to_string = &to_string;

    // --- Call the registration function, passing the services ---
    register_func(this, &services);

    loaded_libraries.push_back(lib_handle);
    TextIO::print("Successfully imported module: " + full_path + "\n");
    return true;
}

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
    TextIO::print("jdBasic v " + NERELA_VERSION + "\n");
    TextIO::print("(c) 2025\n\n");
// #ifndef _WIN32    
//     keypad(stdscr, TRUE); 
// #endif    
}

void NeReLaBasic::init_system() {
    pcode = 0;
    trace = 0;
    
    //TextIO::print("Trace is:  ");
    //TextIO::print_uw(trace);
    //TextIO::nl();
}

void NeReLaBasic::init_basic() {
    TextIO::nl();
    //TextIO::print("Ready\n");
}

void NeReLaBasic::process_system_events() {
    // 1. Process the internal event queue (for events raised by RAISEEVENT)
    process_event_queue();

    // 2. Process system-level keyboard events if not paused by OPTION "NOPAUSE"
#ifdef _WIN32
    if (!nopause_active && _kbhit()) {
        char key = _getch();
#else
    if (!nopause_active && TextIO::kbhit()) {
        char key = getch();
#endif


        auto key_data = std::make_shared<Map>();
        key_data->data["key"] = std::string(1, key);

        int scancode = static_cast<unsigned char>(key);
        // On Windows, extended keys (arrows, etc.) send a 224 prefix.
        // We read the second byte and map it to the custom scancodes our BASIC program expects.
        if (scancode == 224) {
#ifdef _WIN32            
            int extended_code = _getch();
#else
            int extended_code = getch();
#endif           
            switch (extended_code) {
            case 75: scancode = 276; break; // Left Arrow
            case 77: scancode = 275; break; // Right Arrow
            case 71: scancode = 280; break; // Home
            case 79: scancode = 279; break; // End
            case 83: scancode = 281; break; // Delete
            default: scancode = 0; // Unhandled extended key
            }
        }
        key_data->data["scancode"] = static_cast<double>(scancode);

        // Raise the "KEYDOWN" event. It will be picked up by process_event_queue()
        // on the next iteration of whichever loop is currently active.
        raise_event("KEYDOWN", key_data);
    }

#ifdef SDL3
    if (graphics_system.is_initialized) {
        if (!graphics_system.handle_events(*this)) {
            program_ended = true;
        }
    }
#endif
}

// The main REPL
void NeReLaBasic::start() {
    init_screen();
    init_system();
    init_basic();

    std::string inputLine;

    while (true) {
        g_vm_instance_ptr = this;
        Error::clear();
        direct_p_code.clear();
        linenr = 0;
        TextIO::print("Ready\n" + prompt);

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
        if (compiler->tokenize(*this, inputLine, 0, direct_p_code, *active_function_table,false,true) != 0) {
            Error::print();
            continue;
        }
        // Execute the direct-mode p_code
        //dump_p_code(direct_p_code, "dump");
        execute_synchronous_block(direct_p_code);
        if (program_ended) { // Check if the END command was executed
            Error::clear();
            program_ended = false;
        }

        if (Error::get() != 0) {
            Error::print();
        }
    }
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
        try
        {
            statement();
        }
        catch (const std::exception& e)
        {
            //TextIO::print("Exception " + std::string(e.what()));
            Error::set(1, 1, "Exception " + std::string(e.what()));
        }

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

BasicValue NeReLaBasic::get_stacktrace() {
    std::string stack_string = "[GLOBAL]\n";
    int i;
    int frames = call_stack.size();
    for (i = frames - 1; i >= 0; --i) {
        const auto& frame = call_stack[i];
        stack_string += std::to_string(i + 1) + ":, frames: " + std::to_string(frames) + ", linenr "  + std::to_string(frame.linenr) + ", function name: " + frame.function_name + ", program: " + program_to_debug + "\n";
    }
    return to_string(stack_string);
}

void NeReLaBasic::pause_for_debugger() {
    std::unique_lock<std::mutex> lock(dap_mutex);
    dap_command_received = false;
    dap_cv.wait(lock, [this] { return dap_command_received; });
    dap_command_received = false;
}

// All debug methods now affect `this->debug_state`, not `current_task`.
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

void NeReLaBasic::step_in() {
    {
        std::lock_guard<std::mutex> lock(dap_mutex);
        debug_state = DebugState::STEP_IN;
        dap_command_received = true;
    }
    dap_cv.notify_one();
}

void NeReLaBasic::step_out() {
    {
        std::lock_guard<std::mutex> lock(dap_mutex);
        debug_state = DebugState::STEP_OUT;
        step_out_stack_depth = call_stack.size();
        dap_command_received = true;
    }
    dap_cv.notify_one();
}

// The generic debug handler now uses `this->debug_state`.
void NeReLaBasic::handle_debug_events() {
    // If no debugger is attached or we are running freely, do nothing.
    if (!dap_handler)  {
        return;
    }

    // Get the line number for the *next* statement to be executed.
    //runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);

    //TextIO::print("D: " + std::to_string((int) (*active_p_code)[pcode - 1]) + ", " + std::to_string((int)(*active_p_code)[pcode]) + ", " + std::to_string((int)(*active_p_code)[pcode + 1]) + ", " + std::to_string((int)(*active_p_code)[pcode + 2]));
    // Skip empty and rem lines
    if (pcode>0 && pcode < (active_p_code->size()-2))
        if (static_cast<Tokens::ID>((*active_p_code)[pcode - 1]) == Tokens::ID::C_CR && static_cast<Tokens::ID>((*active_p_code)[pcode + 2]) == Tokens::ID::C_CR) 
            { return; }

    bool should_pause = false;
    std::string pause_reason = "step";
    //dap_handler->send_output_message("in handle: " + std::to_string(runtime_current_line) + ", b: " + std::to_string(breakpoints.count(runtime_current_line)));
    // 1. Check for a breakpoint. This has high priority.
    if (breakpoints.count(runtime_current_line)) {
        should_pause = true;
        pause_reason = "breakpoint";
        //dap_handler->send_output_message("in break: ");
    }
    // 2. Check for stepping completion.
    else {
        switch (debug_state) {
        case DebugState::PAUSED:
            should_pause = true;
            break;

        case DebugState::STEP_IN:
            should_pause = true;
            pause_reason = "step";
            break;

        case DebugState::STEP_OUT:
            if (call_stack.size() < step_out_stack_depth) {
                should_pause = true;
                pause_reason = "step";
            }
            break;

        case DebugState::STEP_OVER:
            if (call_stack.size() <= step_over_stack_depth) {
                should_pause = true;
                pause_reason = "step";
            }
            break;

        case DebugState::RUNNING:
            break;
        }
    }

    if (should_pause) {
        debug_state = DebugState::PAUSED;
        dap_handler->send_stopped_message(pause_reason, runtime_current_line, this->program_to_debug);
        pause_for_debugger();
    }
}

void NeReLaBasic::raise_event(const std::string& event_name, BasicValue data) {
    event_queue.push_back({ to_upper(event_name), data });
}

void NeReLaBasic::process_event_queue() {
    if (is_processing_event || event_queue.empty()) {
        return;
    }

    is_processing_event = true;

    auto event_pair = event_queue.front();
    event_queue.pop_front();

    std::string event_name = event_pair.first;
    BasicValue event_data = event_pair.second;

    if (event_handlers.count(event_name)) {
        std::string handler_func_name = event_handlers.at(event_name);
        if (active_function_table->count(handler_func_name)) {
            const auto& func_info = active_function_table->at(handler_func_name);

            auto args_array = std::make_shared<Array>();
            args_array->shape = { 1 };
            args_array->data.push_back(event_data);

            std::vector<BasicValue> args = { args_array };

            execute_synchronous_function(func_info, args);
        }
    }
    is_processing_event = false;
}

// Wrapper for synchronous function calls from the expression parser
BasicValue NeReLaBasic::execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    // Priority 1: Check for the new ABI-safe DLL function pointer
    if (func_info.native_dll_impl != nullptr) {
        BasicValue result;
        // Call it using the new "output pointer" style
        func_info.native_dll_impl(*this, args, &result);
        //if (Error::get() != 0) {
        //    Error::print();
        //}
        return result;
    }
    // Priority 2: Check for the old internal function pointer
    else if (func_info.native_impl != nullptr) {
        // Call it using the old "return by value" style
        return func_info.native_impl(*this, args);
    }
    // Priority 3: If neither native pointer is set, it's a user-defined BASIC function
    else {
        return execute_synchronous_function(func_info, args);
    }
}

// New synchronous executor for REPL and direct commands
void NeReLaBasic::execute_synchronous_block(const std::vector<uint8_t>& code_to_run, int multiline) {
    if (code_to_run.empty()) return;

    auto prev_active_p_code = active_p_code;
    auto prev_pcode = pcode;
    active_p_code = &code_to_run;
    pcode = 0;

    while (pcode < active_p_code->size()) {
        process_system_events(); 
        if (program_ended) break;

        if (pcode == 0) pcode += 2; // Skip line number
        Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (token == Tokens::ID::NOCMD || (token == Tokens::ID::C_CR && multiline == false)) break;
        try
        {
            statement();
        }
        catch (const std::exception& e)
        {
            //TextIO::print("Exception " + std::string(e.what()));
            Error::set(1, 1, "Exception " + std::string(e.what()));
        }

        handle_debug_events();

        if (jump_to_catch_pending) {
            pcode = pending_catch_address;
            jump_to_catch_pending = false; // Reset flag
            Error::clear(); // Clear error now that we've jumped
            continue; // Continue execution in the CATCH/FINALLY block
        }

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
    // --- Properly initialize the stack frame ---
    StackFrame frame;
    frame.return_p_code_ptr = this->active_p_code;
    frame.return_pcode = this->pcode;
    frame.previous_function_table_ptr = this->active_function_table;
    frame.for_stack_size_on_entry = this->for_stack.size();
    frame.function_name = func_info.name;
    frame.linenr = runtime_current_line;
    frame.is_async_call = func_info.is_async;

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
        process_system_events(); 
        if (program_ended) break;

        handle_debug_events();

        if (Error::get() != 0) {
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            break;
        }
        if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
            Error::set(25, runtime_current_line); // Missing RETURN
            while (call_stack.size() > initial_stack_depth) call_stack.pop_back();
            break;
        }
        try
        {
            statement();
        }
        catch (const std::exception& e)
        {
            //TextIO::print("Exception " + std::string(e.what()));
            Error::set(1, 1, "Exception " + std::string(e.what()));
        }
        if (jump_to_catch_pending) {
            pcode = pending_catch_address;
            jump_to_catch_pending = false; // Reset flag
            Error::clear(); // Clear error now that we've jumped
            continue; // Continue execution in the CATCH/FINALLY block
        }
        // Handle multi-statement lines separated by ':'
        if (pcode < active_p_code->size() &&
            static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_COLON) {
            pcode++; // Consume the colon and continue the loop
        }
        
    }

    // --- Context restore ---
    //this->active_function_table = prev_active_func_table;
    //this->active_p_code = prev_active_p_code;

    if (variables.count("RETVAL")) {
        return variables["RETVAL"];
    }
    return false;
}

void NeReLaBasic::execute_main_program(const std::vector<uint8_t>& code_to_run, bool resume_mode) {
    if (code_to_run.empty()) return;

    task_queue.clear();
    task_completed.clear();
    next_task_id = 0;
    program_ended = false;

    auto main_task = std::make_shared<Task>();
    main_task->id = next_task_id++;
    main_task->status = TaskStatus::RUNNING;
    main_task->p_code_ptr = &code_to_run;
    main_task->p_code_counter = resume_mode ? this->pcode : 0;

    task_queue[main_task->id] = main_task;

    g_vm_instance_ptr = this;
    Error::clear();

    //dap_handler->send_output_message("We are in execute main.\n");

    if (dap_handler) { // Check if the debugger is attached
        debug_state = DebugState::PAUSED;
        // Tell the client we are paused at the entry point. 1 because we dont have an active_p_code yet!
        dap_handler->send_stopped_message("entry", 1, this->program_to_debug);
        // Wait for the first "continue" or "next" command from the client.
        pause_for_debugger();
    }

    

    while (!task_queue.empty()) {
        process_system_events();
        if (program_ended) {
            break;
        }
#ifdef SDL3
        if (graphics_system.is_initialized) { if (!graphics_system.handle_events(*this)) break; }
#endif

        for (auto it = task_queue.begin(); it != task_queue.end(); ) {
            current_task = it->second.get();

            bool task_removed = false;

            if (current_task->result_future.valid()) {
                // Check if the future is ready without blocking the interpreter.
                if (current_task->result_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    // The C++ thread is done! Get the result and complete this task.
                    try {
                        current_task->result = current_task->result_future.get();
                        current_task->status = TaskStatus::COMPLETED;
                        int task_id_to_delete = current_task->id;
                        task_completed[task_id_to_delete] = task_queue.at(task_id_to_delete);
                        it = task_queue.erase(it);
                        task_removed = true;
                    }
                    catch (const std::exception& e) {
                        // Handle exceptions from the background C++ thread.
                        current_task->result = "Error: " + std::string(e.what());
                        current_task->status = TaskStatus::ERRORED;
                    }
                }
                // If the future is not ready yet, we just skip to the next task.
                if (!task_removed) ++it;
                continue;
            }

            // --- Context Switch: Load ---
            this->pcode = current_task->p_code_counter;
            this->active_p_code = current_task->p_code_ptr;
            this->call_stack = current_task->call_stack;
            this->for_stack = current_task->for_stack;
            // Restore the correct function table for the current context
            if (!this->call_stack.empty()) {
                this->active_function_table = this->call_stack.back().previous_function_table_ptr;
            }
            else {
                this->active_function_table = &this->main_function_table;
            }
            current_task->yielded_execution = false;

            // --- Task Execution Logic ---
            if (current_task->status == TaskStatus::RUNNING) {
                // Check if the task's pcode is valid *before* executing
                //if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::NOCMD) {
                if (pcode >= active_p_code->size()) {
                    // This can happen if a function ends without RETURN/ENDFUNC. Mark as complete.
                    current_task->status = TaskStatus::COMPLETED;
                    handle_debug_events();
                }
                else {
                    // Execute one line of the task
                    runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
                    pcode += 2;

                    handle_debug_events();

                    bool line_is_done = false;
                    while (!line_is_done && pcode < active_p_code->size()) {
                        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR) {
                            try
                            {
                                statement();
                            }
                            catch (const std::exception& e)
                            {
                                //TextIO::print("Exception " + std::string(e.what()));
                                Error::set(1, 1, "Exception " + std::string(e.what()));
                            }
                            
                        }
                        // --- Error Handling Logic ---
                        if (jump_to_catch_pending) {
                            pcode = pending_catch_address;
                            jump_to_catch_pending = false; // Reset flag
                            Error::clear(); // Clear error now that we've jumped
                            continue; // Continue execution in the CATCH/FINALLY block
                        }
                        if (Error::get() != 0) {
                            // The new Error::set function will have already jumped to a CATCH block if one exists.
                            // If we get here, it means the error was unhandled.
                            current_task->status = TaskStatus::ERRORED;
                            line_is_done = true;
                            continue;
                        }
                        // If the statement caused the task to complete (e.g. RETURN) or yield (AWAIT), stop processing this line.
                        if (current_task->status != TaskStatus::RUNNING || current_task->yielded_execution) {
                            line_is_done = true;
                            continue;
                        }

                        // Handle multi-statement lines
                        if (pcode < active_p_code->size()) {
                            Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
                            if (next_token == Tokens::ID::C_COLON) {
                                pcode++;
                            }
                            else {
                                if (next_token == Tokens::ID::C_CR || next_token == Tokens::ID::NOCMD) {
                                    line_is_done = true;
                                }
                            }
                        }
                    }
                    if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_CR) {
                        pcode++;
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
                int task_id_to_delete = current_task->id;
                task_completed[task_id_to_delete] = task_queue.at(task_id_to_delete);
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

    case Tokens::ID::THIS_KEYWORD:
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
    case Tokens::ID::TRY:
    case Tokens::ID::CATCH:
    case Tokens::ID::FINALLY:
    case Tokens::ID::ENDTRY:
        pcode++; // Should not happen, but skip if it does.
        break;
    case Tokens::ID::OP_PUSH_HANDLER:
        pcode++;
        Commands::do_push_handler(*this);
        break;
    case Tokens::ID::OP_POP_HANDLER:
        pcode++;
        Commands::do_pop_handler(*this);
        break;
    case Tokens::ID::ON:
        pcode++;
        Commands::do_on(*this);
        break;
    case Tokens::ID::RAISEEVENT:
        pcode++;
        Commands::do_raiseevent(*this);
        break;
    case Tokens::ID::END:
    case Tokens::ID::T_EOF:
        pcode++;
        Commands::do_end(*this);
        break;
    //case Tokens::ID::RESUME:
    //    pcode++;
    //    Commands::do_resume(*this);
    //    break;
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
    case Tokens::ID::DLLIMPORT:
        pcode++;
        Commands::do_dllimport(*this);
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
    case Tokens::ID::C_UNDERLINE:
        pcode++; // Fall through and consume C_CR
    case Tokens::ID::C_CR:
        // This token is followed by a 2-byte line number. Skip it during execution.
        pcode++;
        runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
        pcode += 2;
        break;
    case Tokens::ID::NOCMD: // we reached the end and do nothing more
        pcode++;
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

/**
 * @brief Copy constructor for creating an isolated VM instance for a background thread.
 *
 * This constructor performs a specialized copy. It duplicates the essential
 * program definition (like p-code and function tables) but intentionally
 * does NOT copy the runtime state (like variables, call stacks, or the program counter).
 * This creates a "clean" clone of the interpreter that can safely execute a function
 * in a separate thread without interfering with the original instance.
 * @param other The original NeReLaBasic instance to clone.
 */
NeReLaBasic::NeReLaBasic(const NeReLaBasic& other) :
    // --- 1. Copy Program Definition Data ---
    // These members define the program itself and are safe to copy. They are read-only
    // during execution in the new thread.
    source_lines(other.source_lines),
    program_p_code(other.program_p_code),
    direct_p_code(), // A new, empty buffer for the new instance.
    main_function_table(other.main_function_table),
    module_function_tables(other.module_function_tables),
    compiled_modules(other.compiled_modules),
    user_defined_types(other.user_defined_types),
    builtin_constants(other.builtin_constants),
    nopause_active(other.nopause_active)
{
    // --- 2. Initialize Runtime State to Defaults ---
    // These members represent the execution state and MUST be reset to their
    // default values to ensure the new instance is a clean slate.

    // Pointers and counters
    pcode = 0;
    prgptr = 0;
    linenr = 0;
    runtime_current_line = 0;
    current_source_line = 0;
    current_statement_start_pcode = 0;

    // State flags
    is_stopped = false;
    trace = 0;

    // I/O and Graphics
    buffer.reserve(64);
    lineinput.reserve(160);
    filename.reserve(40);
    graphmode = 0;
    fgcolor = 2;
    bgcolor = 0;

    // Stacks (must be empty for the new thread)
    variables.clear();
    for_stack.clear();
    call_stack.clear();
    func_stack.clear();
    handler_stack.clear();

    // Asynchronous Task System (not used by BSYNC threads)
    task_queue.clear();
    task_completed.clear();
    next_task_id = 0;
    current_task = nullptr;

    // Error Handling State (must be reset)
    error_handler_active = false;
    jump_to_error_handler = false;
    error_handler_function_name = "";
    err_code = 0.0;
    erl_line = 0.0;
    //resume_pcode = 0;
    //resume_pcode_next_statement = 0;
    //resume_runtime_line = 0;
    //resume_p_code_ptr = nullptr;
    //resume_function_table_ptr = nullptr;
    //resume_call_stack_snapshot.clear();
    //resume_for_stack_snapshot.clear();

    // Debugger State (the new thread is not being debugged)
    dap_handler = nullptr;
    debug_state = DebugState::RUNNING;
    step_over_stack_depth = 0;
    breakpoints.clear();

    // --- 3. Handle Members Requiring Special Initialization ---

    // The compiler is a unique_ptr and must be a new instance.
    compiler = std::make_unique<Compiler>();

    // Set the active pointers to point to THIS instance's data, not the other's.
    active_p_code = &this->program_p_code;
    active_function_table = &this->main_function_table;

    // The hardware abstractions (Graphics, Sound, Network) are default-constructed,
    // which correctly gives the new instance its own non-initialized systems.

    // Note: We don't need to re-run `register_builtin_functions` because the native
    // function pointers within the `std::function` objects inside the copied
    // FunctionInfo structs are already correct.
}

BasicValue NeReLaBasic::launch_bsync_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args) {
    auto promise = std::make_shared<std::promise<BasicValue>>();
    std::future<BasicValue> future = promise->get_future();

    // The thread lambda captures the promise and arguments.
    std::thread worker_thread([this, promise, func_info, args]() {
        try {
            // 1. Create a completely isolated copy of the interpreter.
            auto thread_vm = std::make_unique<NeReLaBasic>(*this);

            // 2. Execute the function synchronously *within this thread* using the isolated VM.
            BasicValue result = thread_vm->execute_synchronous_function(func_info, args);

            // 3. Fulfill the promise with the result.
            promise->set_value(result);
        }
        catch (const std::exception& e) {
            // Handle exceptions within the thread to avoid crashing.
            promise->set_exception(std::current_exception());
        }
        });

    // Get the ID of the thread we just created.
    std::thread::id worker_id = worker_thread.get_id();

    // Store the future so we can get the result later, keyed by the thread ID.
    {
        std::lock_guard<std::mutex> lock(background_tasks_mutex);
        background_tasks[worker_id] = std::move(future);
    }

    // Detach the thread to let it run independently. The `future` is our way to sync with it.
    worker_thread.detach();

    // Return the handle to the BASIC script.
    return ThreadHandle{ worker_id };
}

// A generic helper to apply any binary operation element-wise.
// It handles scalar-scalar, array-scalar, scalar-array, and array-array operations.
static BasicValue apply_binary_op(
    const BasicValue& left,
    const BasicValue& right,
    const std::function<BasicValue(const BasicValue&, const BasicValue&)>& op
) {
    // Case 1: Array-Array operation
    if (std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
        const auto& left_ptr = std::get<std::shared_ptr<Array>>(left);
        const auto& right_ptr = std::get<std::shared_ptr<Array>>(right);
        if (!left_ptr || !right_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }
        if (left_ptr->shape != right_ptr->shape) { Error::set(15, 0, "Array shapes must match for element-wise operation."); return {}; }

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = left_ptr->shape;
        result_ptr->data.reserve(left_ptr->data.size());

        for (size_t i = 0; i < left_ptr->data.size(); ++i) {
            result_ptr->data.push_back(op(left_ptr->data[i], right_ptr->data[i]));
        }
        return result_ptr;
    }
    // Case 2: Array-Scalar operation
    else if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
        const auto& left_ptr = std::get<std::shared_ptr<Array>>(left);
        if (!left_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = left_ptr->shape;
        result_ptr->data.reserve(left_ptr->data.size());

        for (const auto& elem : left_ptr->data) {
            result_ptr->data.push_back(op(elem, right));
        }
        return result_ptr;
    }
    // Case 3: Scalar-Array operation
    else if (std::holds_alternative<std::shared_ptr<Array>>(right)) {
        const auto& right_ptr = std::get<std::shared_ptr<Array>>(right);
        if (!right_ptr) { Error::set(15, 0, "Operation on a null array."); return {}; }

        auto result_ptr = std::make_shared<Array>();
        result_ptr->shape = right_ptr->shape;
        result_ptr->data.reserve(right_ptr->data.size());

        for (const auto& elem : right_ptr->data) {
            result_ptr->data.push_back(op(left, elem));
        }
        return result_ptr;
    }
    // Case 4: Scalar-Scalar operation
    else {
        return op(left, right);
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

std::vector<BasicValue> NeReLaBasic::parse_argument_list() {
    std::vector<BasicValue> args;
    // Expect '(' to start the argument list
    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_LEFTPAREN) {
        Error::set(1, runtime_current_line, "Expected '('.");
        return {};
    }

    // Check for empty argument list: ()
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTPAREN) {
        pcode++; // Consume ')'
        return args;
    }

    // Loop through the arguments
    while (true) {
        // *** Check for the placeholder '?' ***
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::PLACEHOLDER) {
            pcode++; // Consume '?'
            if (!is_in_pipe_call) {
                Error::set(1, runtime_current_line, "Placeholder '?' can only be used on the right side of a pipe '|>' operator.");
                return {};
            }
            args.push_back(piped_value_for_call);
        }
        else {
            // Original logic: evaluate the expression for the argument.
            args.push_back(evaluate_expression());
        }

        if (Error::get() != 0) return {};

        Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (separator == Tokens::ID::C_RIGHTPAREN) {
            break; // End of list
        }
        if (separator != Tokens::ID::C_COMMA) {
            Error::set(1, runtime_current_line, "Expected ',' or ')' in argument list.");
            return {};
        }
        pcode++; // Consume ','
    }

    pcode++; // Consume ')'
    return args;
}

// Level 5: Handles highest-precedence items
BasicValue NeReLaBasic::parse_primary() {
    BasicValue current_value;
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    if (token == Tokens::ID::THREAD) {
        pcode++; // Consume BSYNC
        // Expect a function call right after
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::CALLFUNC) {
            Error::set(1, runtime_current_line, "THREAD must be followed by a function call.");
            return {};
        }
        pcode++; // Consume CALLFUNC

        std::string func_name = to_upper(read_string(*this));
        if (!active_function_table->count(func_name)) {
            Error::set(22, runtime_current_line, "Function not found for THREAD: " + func_name);
            return {};
        }
        const auto& func_info = active_function_table->at(func_name);

        // Parse arguments just like a normal function call
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

        // Launch the function in a background thread and get the handle
        return launch_bsync_function(func_info, args);
    }
    else if (token == Tokens::ID::OP_START_TASK) {
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
        new_task->p_code_counter = func_info.start_pcode + 1; //Dirty JD was here!

        StackFrame frame;
        frame.function_name = func_name;
        frame.is_async_call = func_info.is_async;
        frame.previous_function_table_ptr = this->active_function_table; //JD ??
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

                std::vector<BasicValue> args = parse_argument_list();
                if (Error::get() != 0) return {};

                if (func_info.arity != -1 && args.size() != func_info.arity) {
                    Error::set(26, runtime_current_line);
                    return {};
                }
                current_value = execute_function_for_value(func_info, args);
            }
            else if (identifier_being_called.find('.') != std::string::npos) {
                size_t dot_pos = identifier_being_called.find('.');
                std::string object_name = to_upper(identifier_being_called.substr(0, dot_pos));
                std::string method_name = to_upper(identifier_being_called.substr(dot_pos + 1));
                BasicValue& object_instance_val = get_variable(*this, object_name);
                auto jt = std::holds_alternative<std::shared_ptr<Map>>(object_instance_val);
                if (variables.contains(object_name) && jt) {
                    // 1. Get the object instance
                    //BasicValue& object_instance_val = get_variable(*this, object_name);
                    //if (!std::holds_alternative<std::shared_ptr<Map>>(object_instance_val)) {
                    //    Error::set(15, runtime_current_line, "Methods can only be called on objects.");
                    //    return {};
                    //}
                    auto object_instance_ptr = std::get<std::shared_ptr<Map>>(object_instance_val);

                    // 2. Get its type and find the method
                    std::string type_name = object_instance_ptr->type_name_if_udt;
                    if (type_name.empty() || !user_defined_types.count(type_name)) {
                        Error::set(15, runtime_current_line, "Object has no type information.");
                        return {};
                    }
                    const auto& type_info = user_defined_types.at(type_name);
                    if (!type_info.methods.count(method_name)) {
                        Error::set(22, runtime_current_line, "Method '" + method_name + "' not found in type '" + type_name + "'.");
                        return {};
                    }

                    // 3. Get the mangled function name and its FunctionInfo
                    std::string mangled_name = type_name + "." + method_name;
                    const auto& func_info = active_function_table->at(mangled_name);

                    // 4. Parse arguments
                    auto args = parse_argument_list();
                    if (Error::get() != 0) return {};

                    // 5. *** SET THE 'THIS' CONTEXT ***
                    this_stack.push_back(object_instance_ptr);

                    // 6. Execute the function
                    current_value = execute_function_for_value(func_info, args);

                    // 7. *** CLEAN UP THE 'THIS' CONTEXT ***
                    this_stack.pop_back();
                }
#ifdef JDCOM
                else
                {
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
                }
#else
                Error::set(22, runtime_current_line, "Unknown function: " + identifier_being_called); return {};
#endif
            }
            else { 
                Error::set(22, runtime_current_line, "Unknown function: " + real_func_to_call); return {}; 
            }
        }
        else if (token == Tokens::ID::THIS_KEYWORD) {
            pcode++; // Consume the token
            if (this_stack.empty()) {
                Error::set(1, runtime_current_line, "'this' can only be used inside a method.");
                return {};
            }
            // The current value is a reference to the object on top of the stack
            current_value = this_stack.back();
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
        else if (token == Tokens::ID::INTEGER_LITERAL) {
            pcode++; int value;
            memcpy(&value, &(*active_p_code)[pcode], sizeof(int));
            pcode += sizeof(int);
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
        else { 
            Error::set(1, runtime_current_line);
            return {};
        }
        if (Error::get() != 0) return {};
    }

    // --- MAIN LOOP FOR HANDLING ACCESSORS LIKE [..], {..}, .member ---
    while (true) {
        Tokens::ID accessor_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

        //if (accessor_token == Tokens::ID::C_LEFTBRACKET) { // Array index access: e.g., A[i, j]
        //    pcode++; // Consume '['

        //    std::vector<size_t> indices;
        //    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
        //        while (true) {
        //            BasicValue index_val = evaluate_expression();
        //            if (Error::get() != 0) return {};
        //            indices.push_back(static_cast<size_t>(to_double(index_val)));

        //            Tokens::ID separator = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        //            if (separator == Tokens::ID::C_RIGHTBRACKET) break;
        //            if (separator != Tokens::ID::C_COMMA) {
        //                Error::set(1, runtime_current_line, "Expected ',' or ']' in array index.");
        //                return {};
        //            }
        //            pcode++; // Consume ','
        //        }
        //    }
        //    if (static_cast<Tokens::ID>((*active_p_code)[pcode++]) != Tokens::ID::C_RIGHTBRACKET) {
        //        Error::set(16, runtime_current_line, "Missing ']' in array access.");
        //        return {};
        //    }

        //    if (std::holds_alternative<std::shared_ptr<Array>>(current_value)) {
        //        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(current_value);
        //        if (!arr_ptr) { Error::set(15, runtime_current_line, "Attempt to index a null array."); return {}; }

        //        try {
        //            size_t flat_index = arr_ptr->get_flat_index(indices);
        //            // The get_flat_index function already checks bounds, but an extra check is safe.
        //            if (flat_index >= arr_ptr->data.size()) {
        //                throw std::out_of_range("Calculated index is out of bounds.");
        //            }
        //            BasicValue next_val = arr_ptr->data[flat_index];
        //            current_value = std::move(next_val);
        //            //current_value = arr_ptr->data[flat_index]; // Update current value with the element
        //        }
        //        catch (const std::exception&) {
        //            // This catches errors from get_flat_index (dimension mismatch or out of bounds)
        //            Error::set(10, runtime_current_line, "Array index out of bounds or dimension mismatch.");
        //            return {};
        //        }
        //    }
        //    else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
        //        if (indices.size() != 1) {
        //            Error::set(15, runtime_current_line, "Multi-dimensional indexing is not supported for JSON objects."); return {};
        //        }
        //        size_t index = indices[0];
        //        const auto& json_ptr = std::get<std::shared_ptr<JsonObject>>(current_value);
        //        if (!json_ptr || !json_ptr->data.is_array() || index >= json_ptr->data.size()) { Error::set(10, runtime_current_line, "JSON index out of bounds."); return {}; }
        //        current_value = json_to_basic_value(json_ptr->data[index]);
        //    }
        //    else { Error::set(15, runtime_current_line, "Indexing '[]' can only be used on an Array."); return {}; }

        //}
        if (accessor_token == Tokens::ID::C_LEFTBRACKET) { // Array index access: e.g., A[i, j]
            pcode++; // Consume '['

            // This now supports vectorized indexing for read access.
            std::vector<BasicValue> index_expressions;
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
                while (true) {
                    index_expressions.push_back(evaluate_expression());
                    if (Error::get() != 0) return {};

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

                // NEW: Check if this is a vectorized or scalar read operation
                bool is_vectorized_read = false;
                for (const auto& expr : index_expressions) {
                    if (std::holds_alternative<std::shared_ptr<Array>>(expr)) {
                        is_vectorized_read = true;
                        break;
                    }
                }

                if (is_vectorized_read) {
                    // Vectorized Read Logic
                    long long num_reads = -1;
                    for (const auto& expr : index_expressions) {
                        if (std::holds_alternative<std::shared_ptr<Array>>(expr)) {
                            const auto& index_arr = std::get<std::shared_ptr<Array>>(expr);
                            if (num_reads == -1) {
                                num_reads = index_arr->data.size();
                            }
                            else if (num_reads != static_cast<long long>(index_arr->data.size())) {
                                Error::set(15, runtime_current_line, "Index arrays for reading must have the same length.");
                                return {};
                            }
                        }
                    }

                    if (num_reads == -1) {
                        Error::set(15, runtime_current_line, "Vectorized read requires at least one index array.");
                        return {};
                    }

                    auto result_ptr = std::make_shared<Array>();
                    result_ptr->shape = { (size_t)num_reads }; // The result is always a 1D vector
                    result_ptr->data.reserve(num_reads);

                    std::vector<size_t> current_coords(index_expressions.size());
                    for (long long i = 0; i < num_reads; ++i) {
                        for (size_t dim = 0; dim < index_expressions.size(); ++dim) {
                            if (std::holds_alternative<std::shared_ptr<Array>>(index_expressions[dim])) {
                                current_coords[dim] = static_cast<size_t>(to_double(std::get<std::shared_ptr<Array>>(index_expressions[dim])->data[i]));
                            }
                            else {
                                current_coords[dim] = static_cast<size_t>(to_double(index_expressions[dim]));
                            }
                        }
                        try {
                            size_t flat_index = arr_ptr->get_flat_index(current_coords);
                            result_ptr->data.push_back(arr_ptr->data[flat_index]);
                        }
                        catch (const std::exception&) {
                            Error::set(10, runtime_current_line, "Array index out of bounds during vectorized read.");
                            return {};
                        }
                    }
                    current_value = result_ptr; // The result of the operation is the new array
                }
                else {
                    // Original logic for scalar read
                    std::vector<size_t> indices;
                    for (const auto& expr : index_expressions) {
                        indices.push_back(static_cast<size_t>(to_double(expr)));
                    }
                    try {
                        size_t flat_index = arr_ptr->get_flat_index(indices);
                        if (flat_index >= arr_ptr->data.size()) {
                            throw std::out_of_range("Calculated index is out of bounds.");
                        }
                        BasicValue next_val = arr_ptr->data[flat_index];
                        current_value = std::move(next_val);
                    }
                    catch (const std::exception&) {
                        Error::set(10, runtime_current_line, "Array index out of bounds or dimension mismatch.");
                        return {};
                    }
                }
            }
            else if (std::holds_alternative<std::shared_ptr<JsonObject>>(current_value)) {
                if (index_expressions.size() != 1) {
                    Error::set(15, runtime_current_line, "Multi-dimensional indexing is not supported for JSON objects."); return {};
                }
                size_t index = static_cast<size_t>(to_double(index_expressions[0]));
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
        //        else if (accessor_token == Tokens::ID::C_DOT) {
        //            pcode++; // Consume '.'
        //            Tokens::ID member_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        //            if (member_token != Tokens::ID::VARIANT && member_token != Tokens::ID::INT && member_token != Tokens::ID::STRVAR) {
        //                Error::set(1, runtime_current_line, "Expected member name after '.'"); return {};
        //            }
        //            pcode++;
        //            std::string member_name = to_upper(read_string(*this));
        //
        //            if (std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
        //                const auto& map_ptr = std::get<std::shared_ptr<Map>>(current_value);
        //                if (!map_ptr || map_ptr->data.find(member_name) == map_ptr->data.end()) { Error::set(3, runtime_current_line, "Member '" + member_name + "' not found in object."); return {}; }
        //                current_value = map_ptr->data.at(member_name);
        //            }
        //#ifdef JDCOM
        //            else if (std::holds_alternative<ComObject>(current_value)) {
        //                IDispatchPtr pDisp = std::get<ComObject>(current_value).ptr;
        //                _variant_t result_vt;
        //                HRESULT hr = invoke_com_method(pDisp, member_name, {}, result_vt, DISPATCH_PROPERTYGET);
        //                if (FAILED(hr)) { Error::set(12, runtime_current_line, "COM property '" + member_name + "' not found or failed to get."); return {}; }
        //                current_value = variant_t_to_basic_value(result_vt, *this);
        //            }
        //#endif
        //            else { Error::set(15, runtime_current_line, "Member access '.' can only be used on an object."); return {}; }
        //        }
        else if (accessor_token == Tokens::ID::C_DOT) {
            pcode++; // Consume '.'
            Tokens::ID member_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
            if (member_token != Tokens::ID::VARIANT && member_token != Tokens::ID::INT && member_token != Tokens::ID::STRVAR && member_token != Tokens::ID::CALLFUNC) {
                Error::set(1, runtime_current_line, "Expected member name after '.'"); return {};
            }
            pcode++; // Consume the variant/int/strvar token
            std::string member_name = to_upper(read_string(*this));

            // --- Look ahead for a function call ---
            Tokens::ID after_member_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

            if (after_member_token == Tokens::ID::C_LEFTPAREN) {
                // --- Case A: It's a METHOD CALL, e.g., .GETALL() ---
                if (!std::holds_alternative<std::shared_ptr<Map>>(current_value)) {
                    Error::set(15, runtime_current_line, "Methods can only be called on objects.");
                    return {};
                }
                auto object_instance_ptr = std::get<std::shared_ptr<Map>>(current_value);
                if (!object_instance_ptr) { Error::set(1, runtime_current_line, "Cannot call method on a null object."); return {}; }

                std::string type_name = object_instance_ptr->type_name_if_udt;
                if (type_name.empty() || !user_defined_types.count(type_name)) {
                    Error::set(15, runtime_current_line, "Object has no type information for method call."); return {};
                }
                const auto& type_info = user_defined_types.at(type_name);
                if (!type_info.methods.count(member_name)) {
                    Error::set(22, runtime_current_line, "Method '" + member_name + "' not found in type '" + type_name + "'."); return {};
                }

                std::string mangled_name = type_name + "." + member_name;
                const auto& func_info = active_function_table->at(mangled_name);

                auto args = parse_argument_list(); // This parses the '()' and arguments
                if (Error::get() != 0) return {};

                this_stack.push_back(object_instance_ptr);
                current_value = execute_function_for_value(func_info, args); // Execute and update current_value
                this_stack.pop_back();

            }
            else {
                // --- Case B: It's a DATA MEMBER ACCESS, e.g., .Name ---
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

        if (task_completed.count(task_id_to_await)) {
            auto& task_to_await = task_completed.at(task_id_to_await);
            if (task_to_await->status == TaskStatus::COMPLETED) {
                auto r = task_to_await->result;
                task_completed.erase(task_id_to_await);
                return r;
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
        BasicValue value = parse_unary(); // Evaluate the expression first

        // If the value is a string, split it into an array of characters.
        if (std::holds_alternative<std::string>(value)) {
            const std::string& s = std::get<std::string>(value);
            auto result_ptr = std::make_shared<Array>();
            result_ptr->shape = { s.length() };
            result_ptr->data.reserve(s.length());
            for (char c : s) {
                result_ptr->data.push_back(std::string(1, c));
            }
            return result_ptr;
        }
        else {
            // Otherwise, perform the original numeric negation.
            return -to_double(value);
        }
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
        if (op == Tokens::ID::C_ASTR || op == Tokens::ID::C_SLASH || op == Tokens::ID::MOD || op == Tokens::ID::FUNCREF) {
            pcode++;

            // --- Handle user-defined operators ---
            if (op == Tokens::ID::FUNCREF) {
                std::string func_name = to_upper(read_string(*this));

                BasicValue right = parse_power();

                if (active_function_table->count(func_name)) {
                    const auto& func_info = active_function_table->at(func_name);
                    if (func_info.arity != 2) {
                        Error::set(26, runtime_current_line, "Custom operator function '" + func_name + "' must take 2 arguments.");
                        return {};
                    }
                    // Case 1: Array @ Scalar
                    if (std::holds_alternative<std::shared_ptr<Array>>(left) && !std::holds_alternative<std::shared_ptr<Array>>(right)) {
                        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(left);
                        auto result_ptr = std::make_shared<Array>(); // Create a new array for the results
                        result_ptr->shape = arr_ptr->shape; // The result has the same shape

                        for (const auto& element : arr_ptr->data) {
                            // Call the BASIC function for each element against the scalar
                            BasicValue element_result = execute_function_for_value(func_info, { element, right });
                            if (Error::get() != 0) return {};
                            result_ptr->data.push_back(element_result);
                        }
                        left = result_ptr; // The new array is now our result
                    }
                    // Case 2: Scalar @ Array
                    else if (!std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
                        const auto& arr_ptr = std::get<std::shared_ptr<Array>>(right);
                        auto result_ptr = std::make_shared<Array>();
                        result_ptr->shape = arr_ptr->shape;

                        for (const auto& element : arr_ptr->data) {
                            // Call the BASIC function for the scalar against each element
                            BasicValue element_result = execute_function_for_value(func_info, { left, element });
                            if (Error::get() != 0) return {};
                            result_ptr->data.push_back(element_result);
                        }
                        left = result_ptr;
                    }
                    // Case 3: Array @ Array
                    else if (std::holds_alternative<std::shared_ptr<Array>>(left) && std::holds_alternative<std::shared_ptr<Array>>(right)) {
                        const auto& left_arr_ptr = std::get<std::shared_ptr<Array>>(left);
                        const auto& right_arr_ptr = std::get<std::shared_ptr<Array>>(right);

                        if (left_arr_ptr->shape != right_arr_ptr->shape) {
                            Error::set(15, runtime_current_line, "Array shape mismatch for operator '" + func_name + "'");
                            return {};
                        }

                        auto result_ptr = std::make_shared<Array>();
                        result_ptr->shape = left_arr_ptr->shape;

                        for (size_t i = 0; i < left_arr_ptr->data.size(); ++i) {
                            // Call the BASIC function for each pair of elements
                            BasicValue element_result = execute_function_for_value(func_info, { left_arr_ptr->data[i], right_arr_ptr->data[i] });
                            if (Error::get() != 0) return {};
                            result_ptr->data.push_back(element_result);
                        }
                        left = result_ptr;
                    }
                    // Case 4: Scalar @ Scalar (The original behavior)
                    else {
                        left = execute_function_for_value(func_info, { left, right });
                        if (Error::get() != 0) return {};
                    }

                }
                else {
                    Error::set(22, runtime_current_line, "Undefined function used as operator: " + func_name);
                    return {};
                }
            }
            else
            {
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

                        if constexpr ((std::is_same_v<LeftT, std::string> && !std::is_same_v<RightT, std::string> && !std::is_same_v<RightT, std::shared_ptr<Array>>) ||
                            (!std::is_same_v<LeftT, std::string> && !std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::string>)) {

                            if (op == Tokens::ID::C_ASTR) { // String repetition
                                std::string s;
                                int count;
                                if constexpr (std::is_same_v<LeftT, std::string>) {
                                    s = l;
                                    count = static_cast<int>(to_double(r));
                                }
                                else {
                                    s = r;
                                    count = static_cast<int>(to_double(l));
                                }
                                if (count < 0) count = 0;
                                std::stringstream ss;
                                for (int i = 0; i < count; ++i) {
                                    ss << s;
                                }
                                return ss.str();
                            }

                            if (op == Tokens::ID::C_SLASH) { // String slicing
                                if constexpr (std::is_same_v<LeftT, std::string>) { // e.g. "Atomi" / 2
                                    std::string s = l;
                                    int count = static_cast<int>(to_double(r));
                                    if (count < 0) count = 0;
                                    if (static_cast<size_t>(count) > s.length()) return s;
                                    return s.substr(s.length() - count);
                                }
                                else { // e.g. 2 / "Atomi"
                                    std::string s = r;
                                    int count = static_cast<int>(to_double(l));
                                    if (count < 0) count = 0;
                                    return s.substr(0, count);
                                }
                            }
                        }

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
                            if constexpr (std::is_same_v<LeftT, double> || std::is_same_v<RightT, double>) {
                                double val_l = to_double(l); double val_r = to_double(r);
                                if (op == Tokens::ID::C_ASTR) return val_l * val_r;
                                if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                                if (op == Tokens::ID::MOD) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                            }
                            else {
                                if (op == Tokens::ID::C_ASTR) {
                                    int left_i = to_int(l);
                                    int right_i = to_int(r);
                                    return left_i * right_i;
                                }
                                double val_l = to_double(l); double val_r = to_double(r);
                                if (op == Tokens::ID::C_SLASH) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return val_l / val_r; }
                                if (op == Tokens::ID::MOD) { if (val_r == 0.0) { Error::set(2, runtime_current_line); return false; } return static_cast<double>(static_cast<long long>(val_l) % static_cast<long long>(val_r)); }
                            }
                        }
                        return false;
                    }, left, right);
                }
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
                        if (op == Tokens::ID::C_PLUS) {
                            return to_string(l) + to_string(r);
                        }
                        else { // Subtraction is now string replacement
                            std::string s_left = to_string(l);
                            std::string s_right = to_string(r);
                            if (s_right.empty()) return s_left; // Avoid infinite loop
                            size_t pos = s_left.find(s_right);
                            while (pos != std::string::npos) {
                                s_left.erase(pos, s_right.length());
                                pos = s_left.find(s_right, pos);
                            }
                            return s_left;
                        }
                    }
                    else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
                        // This uses the same array_add/subtract helpers from BuiltinFunctions.cpp
                        // Ensure they are globally accessible or duplicate the logic here.
                        if (!l || !r || l->data.empty()) { // Handle null or empty arrays
                            if (op == Tokens::ID::C_PLUS) return r; else return l;
                        }
                        // Check if either array contains strings to decide the operation type
                        bool is_string_op = std::holds_alternative<std::string>(l->data[0]) || std::holds_alternative<std::string>(r->data[0]);

                        if (op == Tokens::ID::C_PLUS && is_string_op) {
                            // Perform element-wise string concatenation
                            if (l->shape != r->shape) { Error::set(15, 0, "Array shapes must match for element-wise operation."); return {}; }
                            auto result_ptr = std::make_shared<Array>();
                            result_ptr->shape = l->shape;
                            result_ptr->data.reserve(l->data.size());
                            for (size_t i = 0; i < l->data.size(); ++i) {
                                result_ptr->data.push_back(to_string(l->data[i]) + to_string(r->data[i]));
                            }
                            return result_ptr;
                        }
                        else {
                            // Fallback to existing numeric operations
                            if (op == Tokens::ID::C_PLUS) return array_add(l, r); else return array_subtract(l, r);
                        }
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
                        if constexpr (std::is_same_v<LeftT, double> || std::is_same_v<RightT, double>) {
                            double left_d = to_double(l);
                            double right_d = to_double(r);
                            if (op == Tokens::ID::C_PLUS) return left_d + right_d;
                            else return left_d - right_d;
                        }
                        // Otherwise, perform integer math
                        else {
                            int left_i = to_int(l);
                            int right_i = to_int(r);
                            if (op == Tokens::ID::C_PLUS) return left_i + right_i;
                            else return left_i - right_i;
                        }
                        //if (op == Tokens::ID::C_PLUS) return to_double(l) + to_double(r);
                        //else return to_double(l) - to_double(r);
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

BasicValue NeReLaBasic::parse_pipe() {
    BasicValue left = parse_comparison();

    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_PIPE) {
        pcode++; // Consume '|>'

        // Set the pipe context before evaluating the RHS
        is_in_pipe_call = true;
        piped_value_for_call = left;

        // The RHS is a full expression, which will typically be a function call.
        // Our modified argument parser will now see the context.
        left = parse_comparison();

        // Reset the pipe context after the RHS has been evaluated
        is_in_pipe_call = false;

        if (Error::get() != 0) return {};
    }
    return left;
}

// Level 1c: Bitwise AND (Highest bitwise precedence)
BasicValue NeReLaBasic::parse_bitwise_and() {
    BasicValue left = parse_pipe(); // Calls the next higher precedence level
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::BAND) {
        pcode++;
        BasicValue right = parse_pipe();

        left = std::visit([](auto&& l, auto&& r) -> BasicValue {
            // Vectorized logic for bitwise operators
            auto bitwise_op = [](const BasicValue& v1, const BasicValue& v2) {
                long long n1 = static_cast<long long>(to_double(v1));
                long long n2 = static_cast<long long>(to_double(v2));
                return static_cast<double>(n1 & n2);
                };
            return apply_binary_op(l, r, bitwise_op);
            }, left, right);
    }
    return left;
}

// Level 1b: Bitwise XOR
BasicValue NeReLaBasic::parse_bitwise_xor() {
    BasicValue left = parse_bitwise_and(); // Calls the next higher precedence level
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::BXOR) {
        pcode++;
        BasicValue right = parse_bitwise_and();

        left = std::visit([](auto&& l, auto&& r) -> BasicValue {
            auto bitwise_op = [](const BasicValue& v1, const BasicValue& v2) {
                long long n1 = static_cast<long long>(to_double(v1));
                long long n2 = static_cast<long long>(to_double(v2));
                return static_cast<double>(n1 ^ n2);
                };
            return apply_binary_op(l, r, bitwise_op);
            }, left, right);
    }
    return left;
}

// Level 1a: Bitwise OR (Lowest bitwise precedence)
BasicValue NeReLaBasic::parse_bitwise_or() {
    BasicValue left = parse_bitwise_xor(); // Calls the next higher precedence level
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::BOR) {
        pcode++;
        BasicValue right = parse_bitwise_xor();

        left = std::visit([](auto&& l, auto&& r) -> BasicValue {
            auto bitwise_op = [](const BasicValue& v1, const BasicValue& v2) {
                long long n1 = static_cast<long long>(to_double(v1));
                long long n2 = static_cast<long long>(to_double(v2));
                return static_cast<double>(n1 | n2);
                };
            return apply_binary_op(l, r, bitwise_op);
            }, left, right);
    }
    return left;
}

// --- The Expression Skipping Implementation ---
// Base Case: Skips a primary element like a literal, variable, function call, or parenthesized expression.
void NeReLaBasic::skip_primary() {
    if (pcode >= active_p_code->size()) return;

    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);

    // Most tokens are a single byte, so we can pre-increment.
    // Cases that need more work will advance pcode further.
    pcode++;

    switch (token) {
    case Tokens::ID::NUMBER:
        pcode += sizeof(double); // Skip the 8-byte double literal
        break;
    case Tokens::ID::INTEGER_LITERAL:
        pcode += sizeof(int); // Skip the 8-byte double literal
        break;
    case Tokens::ID::STRING:
    case Tokens::ID::VARIANT:
    case Tokens::ID::FUNCREF:
    case Tokens::ID::CONSTANT:
        read_string(*this); // Reads the string to advance pcode past it
        break;
    case Tokens::ID::CALLFUNC:
        read_string(*this); // Skip function name
        // Now, explicitly skip the argument list that MUST follow.
        if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_LEFTPAREN) {
            pcode++; // Skip '('
            if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTPAREN) {
                while (true) {
                    skip_expression(); // Recursively skip each argument expression
                    if (pcode >= active_p_code->size() || static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTPAREN) break;
                    pcode++; // Skip comma
                }
            }
            if (pcode < active_p_code->size()) pcode++; // Skip ')'
        }
        break;

        // CORRECTED LOGIC: C_LEFTPAREN is only for parenthesized expressions.
    case Tokens::ID::C_LEFTPAREN:
        skip_expression(); // Skip the inner expression
        if (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTPAREN) {
            pcode++; // Skip ')'
        }
        break;
    case Tokens::ID::C_LEFTBRACKET: // Skip Array Literal
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACKET) {
            while (true) {
                skip_expression();
                if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTBRACKET) break;
                pcode++; // Skip comma
            }
        }
        pcode++; // Skip ']'
        break;
    case Tokens::ID::C_LEFTBRACE: // Skip Map Literal
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_RIGHTBRACE) {
            while (true) {
                skip_expression(); // Skip key
                pcode++; // Skip colon
                skip_expression(); // Skip value
                if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_RIGHTBRACE) break;
                pcode++; // Skip comma
            }
        }
        pcode++; // Skip '}'
        break;
        // For simple tokens (TRUE, FALSE, THIS, etc.), just consuming the token is enough.
    default:
        break;
    }

    // After skipping the primary, we must also skip any chained accessors like `[...]`, `{. A..}`, or `.MEMBER`
    while (true) {
        Tokens::ID accessor_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (accessor_token == Tokens::ID::C_LEFTBRACKET || accessor_token == Tokens::ID::C_LEFTBRACE) {
            pcode++; // consume '[' or '{'
            skip_expression();
            pcode++; // consume ']' or '}'
        }
        else if (accessor_token == Tokens::ID::C_DOT) {
            pcode++; // consume '.'
            pcode++; // consume VARIANT/CALLFUNC token
            read_string(*this); // consume member name
        }
        else {
            break;
        }
    }
}

// Skips unary operators and their operands
void NeReLaBasic::skip_unary() {
    Tokens::ID token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
    if (token == Tokens::ID::C_MINUS || token == Tokens::ID::NOT || token == Tokens::ID::AWAIT) {
        pcode++; // Skip the unary operator
    }
    skip_primary();
}

// Forward declarations for the new parsing chain


// Skips a chain of binary operations for a given precedence level.
void NeReLaBasic::skip_binary_op_chain(std::function<void()> skip_higher_precedence, const std::vector<Tokens::ID>& operators) {
    skip_higher_precedence(); // Skip the first operand
    while (pcode < active_p_code->size())  {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        bool is_op = false;
        for (auto valid_op : operators) {
            if (op == valid_op) {
                is_op = true;
                break;
            }
        }

        if (is_op) {
            pcode++; // Consume the operator
            if (op == Tokens::ID::FUNCREF) { read_string(*this); } // Custom ops have a name
            skip_higher_precedence(); // Skip the next operand
        }
        else {
            break;
        }
    }
}

void NeReLaBasic::skip_power() { skip_binary_op_chain([this] { skip_unary(); }, { Tokens::ID::C_CARET }); }
void NeReLaBasic::skip_factor() { skip_binary_op_chain([this] { skip_power(); }, { Tokens::ID::C_ASTR, Tokens::ID::C_SLASH, Tokens::ID::MOD, Tokens::ID::FUNCREF }); }
void NeReLaBasic::skip_term() { skip_binary_op_chain([this] { skip_factor(); }, { Tokens::ID::C_PLUS, Tokens::ID::C_MINUS }); }
void NeReLaBasic::skip_comparison() { skip_binary_op_chain([this] { skip_term(); }, { Tokens::ID::C_EQ, Tokens::ID::C_NE, Tokens::ID::C_LT, Tokens::ID::C_GT, Tokens::ID::C_LE, Tokens::ID::C_GE }); }
void NeReLaBasic::skip_pipe() { skip_binary_op_chain([this] { skip_comparison(); }, { Tokens::ID::C_PIPE }); }
void NeReLaBasic::skip_bitwise_and() { skip_binary_op_chain([this] { skip_pipe(); }, { Tokens::ID::BAND }); }
void NeReLaBasic::skip_bitwise_xor() { skip_binary_op_chain([this] { skip_bitwise_and(); }, { Tokens::ID::BXOR }); }
void NeReLaBasic::skip_bitwise_or() { skip_binary_op_chain([this] { skip_bitwise_xor(); }, { Tokens::ID::BOR }); }
void NeReLaBasic::skip_logical_and() { skip_binary_op_chain([this] { skip_bitwise_or(); }, { Tokens::ID::AND, Tokens::ID::ANDALSO }); }
void NeReLaBasic::skip_logical_or() { skip_binary_op_chain([this] { skip_logical_and(); }, { Tokens::ID::OR, Tokens::ID::ORELSE }); }

// Top-level function, now correctly structured
void NeReLaBasic::skip_expression() {
    skip_logical_or();
    while (pcode < active_p_code->size() && static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::XOR) {
        pcode++;
        skip_logical_or();
    }
}

// Level for logical AND / ANDALSO
BasicValue NeReLaBasic::parse_logical_and() {
    BasicValue left = parse_bitwise_or();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::AND) {
            pcode++;
            BasicValue right = parse_bitwise_or();
            auto and_op = [](const BasicValue& v1, const BasicValue& v2) { return to_bool(v1) && to_bool(v2); };
            left = apply_binary_op(left, right, and_op);
        }
        else if (op == Tokens::ID::ANDALSO) {
            pcode++;
            if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
                Error::set(15, runtime_current_line, "ANDALSO operator does not support array operands.");
                return {};
            }
            if (to_bool(left)) {
                left = parse_bitwise_or(); // Evaluate RHS
            }
            else {
                skip_expression(); // Skip RHS
            }
        }
        else {
            break;
        }
    }
    return left;
}

BasicValue NeReLaBasic::parse_logical_or() {
    BasicValue left = parse_logical_and();
    while (true) {
        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        if (op == Tokens::ID::OR) {
            pcode++;
            BasicValue right = parse_logical_and();
            auto or_op = [](const BasicValue& v1, const BasicValue& v2) { return to_bool(v1) || to_bool(v2); };
            left = apply_binary_op(left, right, or_op);
        }
        else if (op == Tokens::ID::ORELSE) {
            pcode++;
            if (std::holds_alternative<std::shared_ptr<Array>>(left)) {
                Error::set(15, runtime_current_line, "ORELSE operator does not support array operands.");
                return {};
            }
            if (!to_bool(left)) {
                left = parse_logical_and(); // Evaluate RHS
            }
            else {
                skip_expression(); // Skip RHS
            }
        }
        else {
            break;
        }
    }
    return left;
}

// Top-level expression function
BasicValue NeReLaBasic::evaluate_expression() {
    BasicValue left = parse_logical_or();

    // The loop now only handles XOR, which is non-short-circuiting
    while (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::XOR) {
        pcode++;
        BasicValue right = parse_logical_or();
        // Use the generic helper for vectorized XOR
        auto xor_op = [](const BasicValue& v1, const BasicValue& v2) -> BasicValue {
            return to_bool(v1) != to_bool(v2);
            };
        left = apply_binary_op(left, right, xor_op);
    }
    return left;
}

//// Level 1: Handles AND, OR, XOR with element-wise array support
//BasicValue NeReLaBasic::evaluate_expression() {
//    //BasicValue left = parse_comparison();
//    BasicValue left = parse_bitwise_or();
//    while (true) {
//        Tokens::ID op = static_cast<Tokens::ID>((*active_p_code)[pcode]);
//        if (op == Tokens::ID::AND || op == Tokens::ID::OR || op == Tokens::ID::XOR) {
//            pcode++;
//            BasicValue right = parse_comparison();
//
//            left = std::visit([op, this](auto&& l, auto&& r) -> BasicValue {
//                using LeftT = std::decay_t<decltype(l)>;
//                using RightT = std::decay_t<decltype(r)>;
//
//                // Helper lambda for Array-Scalar operations
//                auto array_scalar_op = [op, this](const auto& arr_ptr, const auto& scalar_val) -> BasicValue {
//                    if (!arr_ptr) { Error::set(15, runtime_current_line, "Operation on null array."); return false; }
//                    bool scalar_bool = to_bool(scalar_val);
//                    auto result_ptr = std::make_shared<Array>();
//                    result_ptr->shape = arr_ptr->shape;
//                    result_ptr->data.reserve(arr_ptr->data.size());
//
//                    for (const auto& elem : arr_ptr->data) {
//                        bool elem_bool = to_bool(elem);
//                        bool result = false;
//                        if (op == Tokens::ID::AND) result = elem_bool && scalar_bool;
//                        else if (op == Tokens::ID::OR) result = elem_bool || scalar_bool;
//                        else if (op == Tokens::ID::XOR) result = elem_bool != scalar_bool;
//                        result_ptr->data.push_back(result);
//                    }
//                    return result_ptr;
//                    };
//
//                // Case 1: Array AND/OR/XOR Array
//                if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>> && std::is_same_v<RightT, std::shared_ptr<Array>>) {
//                    if (!l || !r) { Error::set(15, runtime_current_line, "Operation on null array."); return false; }
//                    if (l->shape != r->shape) { Error::set(15, runtime_current_line, "Array shape mismatch in logical operation."); return false; }
//
//                    auto result_ptr = std::make_shared<Array>();
//                    result_ptr->shape = l->shape;
//                    result_ptr->data.reserve(l->data.size());
//
//                    for (size_t i = 0; i < l->data.size(); ++i) {
//                        bool left_bool = to_bool(l->data[i]);
//                        bool right_bool = to_bool(r->data[i]);
//                        bool result = false;
//                        if (op == Tokens::ID::AND) result = left_bool && right_bool;
//                        else if (op == Tokens::ID::OR) result = left_bool || right_bool;
//                        else if (op == Tokens::ID::XOR) result = left_bool != right_bool; // Logical XOR
//                        result_ptr->data.push_back(result);
//                    }
//                    return result_ptr;
//                }
//                // Case 2: Array op Scalar
//                else if constexpr (std::is_same_v<LeftT, std::shared_ptr<Array>>) {
//                    return array_scalar_op(l, r);
//                }
//                // Case 3: Scalar op Array
//                else if constexpr (std::is_same_v<RightT, std::shared_ptr<Array>>) {
//                    return array_scalar_op(r, l);
//                }
//                // Case 4: Scalar op Scalar (fallback)
//                else {
//                    bool left_bool = to_bool(l);
//                    bool right_bool = to_bool(r);
//                    if (op == Tokens::ID::AND) return left_bool && right_bool;
//                    if (op == Tokens::ID::OR) return left_bool || right_bool;
//                    if (op == Tokens::ID::XOR) return left_bool != right_bool; // Logical XOR
//                }
//
//                return false; // Should not be reached
//                }, left, right);
//        }
//        else {
//            break;
//        }
//    }
//    return left;
//}