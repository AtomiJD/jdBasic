// NeReLaBasic.cpp
#include <sstream>     
#include "Commands.hpp"
#include "jdConsole.hpp"
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
#if defined(_WIN32)
#include <conio.h>
// Helper to convert UTF-8 from JSON to std::wstring for Python Config
std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
// Helper to expand Windows environment variables like % LOCALAPPDATA %
std::wstring ExpandEnvVars(const std::wstring & input) {
    DWORD size_needed = ExpandEnvironmentStringsW(input.c_str(), NULL, 0);
    if (size_needed == 0) return input; // Return original if expansion fails

    std::wstring expanded(size_needed, L'\0');
    ExpandEnvironmentStringsW(input.c_str(), &expanded[0], size_needed);

    // Remove the trailing null character included in size_needed
    expanded.pop_back();
    return expanded;
}

#elif defined(__EMSCRIPTEN__)
extern std::vector<int> g_ems_key_buffer;
std::deque<int> g_inkey_buffer;
#else
//#include <ncurses.h>
#include <readline/readline.h> // For readline()
#include <readline/history.h>  // For add_history()
#endif
#include <algorithm> // for std::transform, std::find_if
#include <cctype>    // for std::isspace, std::toupper

#if defined(PYTHON)
#ifdef _DEBUG
#define JDBASIC_RESTORE_DEBUG
#undef _DEBUG
#endif

#include <Python.h>

// --- Restore _DEBUG for the rest of your C++ project ---
#ifdef JDBASIC_RESTORE_DEBUG
#define _DEBUG
#undef JDBASIC_RESTORE_DEBUG
#endif
#endif

// Forward declarations for tensor math functions from AIFunctions.cpp
BasicValue tensor_add(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_subtract(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_elementwise_multiply(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
BasicValue tensor_power(NeReLaBasic& vm, const BasicValue& base, const BasicValue& exponent);
BasicValue tensor_scalar_divide(NeReLaBasic& vm, const BasicValue& a, const BasicValue& b);
std::shared_ptr<Array> array_add(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);
std::shared_ptr<Array> array_subtract(const std::shared_ptr<Array>& a, const std::shared_ptr<Array>& b);

const std::string NERELA_VERSION = "1.0.1";

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
#ifdef HTTP
NeReLaBasic::NeReLaBasic() : network_manager(*this) {
#else
NeReLaBasic::NeReLaBasic() {
#endif
    buffer.reserve(64);
    lineinput.reserve(160);
    filename.reserve(40);
    
    active_function_table = &main_function_table;
    compiler = std::make_unique<Compiler>();

    register_builtin_functions(*this, *active_function_table);
    srand(static_cast<unsigned int>(time(nullptr)));

    builtin_constants["VBNEWLINE"] = std::string("\n");
    builtin_constants["PI"] = 3.14159265358979323846;
    
    debug_state = DebugState::RUNNING;
    step_over_stack_depth = 0;
    step_out_stack_depth = 0;
    last_keyboard_check_time = std::chrono::steady_clock::now();

#if defined(PYTHON)
    PyConfig config;
    PyConfig_InitPythonConfig(&config);

#if defined(_WIN32)
    // 1. Get the path of the current executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    // 2. Strip the executable name to get the directory
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash != nullptr) {
        *lastSlash = L'\0';
    }

    // 3. Construct the path to the jdbasic.config file
    std::wstring configFilePath = std::wstring(exePath) + L"\\jdbasic.config";
    std::wstring pythonHome;

    // 4. Read and parse the JSON config file
    std::ifstream configFile(configFilePath);
    if (configFile.is_open()) {
        try {
            nlohmann::json j;
            configFile >> j;

            // Extract and expand the python path
            if (j.contains("python_home") && j["python_home"].is_string()) {
                std::string utf8Path = j["python_home"];
                pythonHome = ExpandEnvVars(Utf8ToWstring(utf8Path));
            }

            // Future config values can be read here...

        }
        catch (const nlohmann::json::exception& e) {
            // Handle JSON parsing errors if needed
        }
        configFile.close();
    }

    // 5. Apply the path from config, or use a fallback
    PyStatus status;
    if (!pythonHome.empty()) {
        status = PyConfig_SetString(&config, &config.home, pythonHome.c_str());
    }
    else {
        // Fallback if config file is missing, invalid, or lacks the key
        // Also expanding the fallback just in case
        std::wstring fallback = ExpandEnvVars(L"%LOCALAPPDATA%\\python\\pythoncore-3.14-64");
        status = PyConfig_SetString(&config, &config.home, fallback.c_str());
    }

    if (PyStatus_Exception(status)) {
        // Fallback or error handling if the path is bad
        PyConfig_Clear(&config);
    }
#endif

    // Boot up the interpreter using the config
    status = Py_InitializeFromConfig(&config);

    // Clean up the config struct (Python has already copied what it needs)
    PyConfig_Clear(&config);

    if (PyStatus_Exception(status)) {
        // Handle fatal initialization error
    }
#endif
}

NeReLaBasic::~NeReLaBasic() {
    // --- Unload any dynamically loaded libraries on exit ---
    for (auto& lib_handle : loaded_libraries) {
#if defined(_WIN32)  
        FreeLibrary(lib_handle);
#elif defined(__EMSCRIPTEN__)
#else
        dlclose(lib_handle);
#endif
    }
#if defined(PYTHON)
    Py_Finalize();
#endif
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    endwin(); // Add this to restore the terminal
#endif
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

#if !defined(__EMSCRIPTEN__)
// --- Implementation for loading a dynamic module ---
bool NeReLaBasic::load_dynamic_module(const std::string& module_path) {
    std::string full_path = module_path;

    // --- Define the new registration function pointer type ---
    using RegisterModuleWithServicesFunc = void(*)(NeReLaBasic*, ModuleServices*);

#if defined(_WIN32)
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

    // --- Create and populate the services struct ---
    ModuleServices services;
    services.error_set = &Error::set;
    services.to_upper = &to_upper; // This assumes to_upper is a free function.
    services.to_string = &to_string;
    services.exec_sync_func = &NeReLaBasic::execute_synchronous_function;

    // --- Call the registration function, passing the services ---
    register_func(this, &services);

    loaded_libraries.push_back(lib_handle);
    TextIO::print("Successfully imported module: " + full_path); TextIO::nl();
    return true;
}
#endif

bool NeReLaBasic::loadSourceFromFile(const std::string& filename, bool verbose) {
    std::ifstream infile(filename);
    if (!infile) {
        TextIO::print("Error: File not found -> " + filename); TextIO::nl();
        return false;
    }
    if (!verbose && !verbose_mode)
        TextIO::print("LOADING " + filename); TextIO::nl();
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
    if (verbose_mode == true) return;

    TextIO::setColor(fgcolor, bgcolor);
    TextIO::clearScreen();
#if  defined(_WIN32)
        std::string sos = "win32 64 bit";
#elif defined(__EMSCRIPTEN__)
        std::string sos = "web WASM";
#elif defined(MACOS)
    std::string sos = "macOS";
#else
        std::string sos = "linux";
#endif    
    TextIO::print("jdBasic v " + NERELA_VERSION + ", OS: " + sos ); TextIO::nl();
    TextIO::print("Copyright (c) 2025-2026 Computerwelt AI Solutions LLC."); TextIO::nl();
    TextIO::print("All Rights Reserved."); TextIO::nl();
    TextIO::print("Type HELP for more infos."); TextIO::nl();
    TextIO::nl();
#if defined(__EMSCRIPTEN__)
    TextIO::print("This web version is limited in functions (like INKEY$, ON ... CALL, graphics, etc.)"); TextIO::nl();
    TextIO::print("First steps:"); TextIO::nl();
    TextIO::print("TYPE: : CD \"dev\" + enter"); TextIO::nl();
    TextIO::print("TYPE: : DIR \"*.jdb\" + enter"); TextIO::nl();
    TextIO::print("TYPE: : LOAD \"A LISTED FILE NAME.jdb\" + enter"); TextIO::nl();
    TextIO::print("TYPE: : RUN + enter"); TextIO::nl();
    TextIO::print("To change a program:"); TextIO::nl();
    TextIO::print("TYPE: : EDIT"); TextIO::nl();
#endif
}

void NeReLaBasic::init_system() {
    pcode = 0;
    trace = 0;
    
    //TextIO::print("Trace is:  ");
    //TextIO::print_uw(trace);
    //TextIO::nl();
}

void NeReLaBasic::init_basic() {
    if (verbose_mode == true) return;
    TextIO::nl();
    //TextIO::print("Ready"); TextIO::nl();
}

#ifdef HTTP
// --- Implementation of server event methods ---

void NeReLaBasic::queue_http_request(std::shared_ptr<ServerRequestEvent> request) {
    std::lock_guard<std::mutex> lock(http_queue_mutex);
    http_request_queue.push_back(request);
}

void NeReLaBasic::process_http_requests() {
    std::shared_ptr<ServerRequestEvent> request;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(http_queue_mutex);
            if (http_request_queue.empty()) {
                break;
            }
            request = http_request_queue.front();
            http_request_queue.pop_front();
        }

        if (request) {
            std::string handler_function_name = network_manager.getRouteHandler(request->method, request->path);
            auto it = main_function_table.find(to_upper(handler_function_name));

            if (!handler_function_name.empty() && it != main_function_table.end()) {
                // ... (code to create the request_map is unchanged)
                auto request_map = std::make_shared<Map>();
                request_map->data["path"] = request->path;
                request_map->data["method"] = request->method;
                request_map->data["body"] = request->body;
                auto headers_map = std::make_shared<Map>();
                for (const auto& header : request->headers) {
                    headers_map->data[header.first] = header.second;
                }
                request_map->data["headers"] = headers_map;

                std::vector<BasicValue> args = { request_map };
                BasicValue result = execute_synchronous_function(it->second, args);



                // --- RESPONSE HANDLING ---
                // Write the result directly into the event struct's response fields
                if (const auto& map_ptr = std::get_if<std::shared_ptr<Map>>(&result)) {
                    // Check for the advanced response format: map with "body" and "content_type"
                    if ((*map_ptr)->data.count("body") && (*map_ptr)->data.count("content_type")) {
                        request->response_body = to_string((*map_ptr)->data["body"]);
                        request->content_type = to_string((*map_ptr)->data["content_type"]);
                    }
                    else {
                        // Fallback: treat the whole map as a JSON body
                        nlohmann::json j = basic_to_json_value(result);
                        request->response_body = j.dump();
                        request->content_type = "application/json";
                    }
                }
                else {
                    // **THE FIX**: If the result is a simple string, assume it's HTML.
                    request->response_body = to_string(result);
                    request->content_type = "text/html"; // Changed from "text/plain"
                }
                request->status_code = 200; // OK
            }
            else {
                request->status_code = 500; // Internal Server Error
                request->response_body = "Handler function '" + handler_function_name + "' not found in BASIC code.";
            }

            // Notify the waiting server thread that the request has been handled
            {
                std::lock_guard<std::mutex> lock(*request->mtx);
                request->handled = true;
            }
            request->cv->notify_one();
        }
    }
}
#endif

void NeReLaBasic::process_system_events() {
    // 1. Process the internal event queue (for events raised by RAISEEVENT)
    
    process_event_queue();

#ifdef SDL3
    if (graphics_system.is_initialized) {
        if (!graphics_system.handle_events(*this)) {
            program_ended = true;
        }
    }
#endif

    auto current_time = std::chrono::steady_clock::now();
    if (current_time - last_keyboard_check_time < keyboard_check_interval) {
        // Not enough time has passed, so skip the keyboard check for this loop iteration.
        return;
    }
    //Lets try to make the HTTP request not in every event loop
#ifdef HTTP
    process_http_requests();
#endif

    evaluate_recurring_tasks();

    // --- It's time to check the keyboard; reset the timer for the next interval ---
    last_keyboard_check_time = current_time;

    // 2. Process system-level keyboard events if not paused by OPTION "NOPAUSE"
#ifdef _WIN32
    if (!nopause_active && _kbhit()) {
        char key = _getch();
#else
    //char c = TextIO::jdgetch();
    char c = 0;

#ifdef __EMSCRIPTEN__
    if (!g_ems_key_buffer.empty()) {
        int val = g_ems_key_buffer.front();
        g_ems_key_buffer.erase(g_ems_key_buffer.begin());
        c = static_cast<char>(val);

        // FIX: Also store it in the inkey buffer so INKEY$ can find it later.
        // Cap the size to prevent infinite growth if INKEY$ is not used.
        if (g_inkey_buffer.size() > 256) {
            g_inkey_buffer.pop_front();
        }
        g_inkey_buffer.push_back(val);
    }
#endif

    if (c > 0) {
        char key = c;
#endif
        auto key_data = std::make_shared<Map>();
        key_data->data["key"] = std::string(1, key);

        int scancode = static_cast<unsigned char>(key);
        // On Windows, extended keys (arrows, etc.) send a 224 prefix.
        // We read the second byte and map it to the custom scancodes our BASIC program expects.
        if (scancode == 224) {
#if defined(_WIN32)
            int extended_code = _getch();
#elif defined(__EMSCRIPTEN__)
            int extended_code = 0;
#else
            int extended_code = getch();
#endif           
            switch (extended_code) {
            case 72: scancode = 273; break; // Up Arrow
            case 80: scancode = 274; break; // Down Arrow
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
    //TextIO::cleanup();
    //return;

}

// The main REPL
void NeReLaBasic::start() {
    init_screen();
    init_system();
    init_basic();


#ifdef  JDREPL
    jdConsole console(*this);
    console.run();
#else
    std::string inputLine;

    while (true) {
        g_vm_instance_ptr = this;
        Error::clear();
        direct_p_code.clear();
        linenr = 0;
        
#if defined(_WIN32)
        if (verbose_mode == false) 
            TextIO::print("Ready\n" + prompt);
        if (!std::getline(std::cin, inputLine) || inputLine.empty()) {
            std::cin.clear();
            continue;
        }
#elif defined(__EMSCRIPTEN__)
        if (verbose_mode == false)
            TextIO::print("Ready\n" + prompt);
        //inputLine = TextIO::jdgets();
        //TextIO::print("I got: " + inputLine);
#else
        std::string full_prompt = prompt;
        if (verbose_mode == false)
            std::string full_prompt = "Ready\n\r" + prompt;
        // Use readline() to get user input
        char* c_inputLine = readline(full_prompt.c_str());
        // Check for EOF (Ctrl+D), which returns NULL
        if (c_inputLine == NULL) {
            TextIO::nl();
            continue;
        }

        // Convert C-style string to std::string
        std::string inputLine(c_inputLine);

        // Add non-empty lines to history and free the memory
        if (!inputLine.empty()) {
            add_history(c_inputLine);
        }
        free(c_inputLine); // IMPORTANT: Free the memory allocated by readline
#endif

        // --- Special handling for RESUME ---
        std::string temp_line = inputLine;
        StringUtils::trim(temp_line);
        if (StringUtils::to_upper(temp_line) == "RESUME") {
            if (is_stopped) {
                TextIO::print("Resuming..."); TextIO::nl();
                is_stopped = false;
                execute_main_program(program_p_code, true); // Continues from the saved pcode
                if (Error::get() != 0) Error::print();
            }
            else {
                TextIO::print("?Nothing to resume."); TextIO::nl();
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
#endif
}

#ifdef __EMSCRIPTEN__
void NeReLaBasic::assign_input_value(const std::string& value_str) {
    std::string var_name = m_input_variable_name;
    m_input_variable_name.clear(); // Clear for next time

    if (var_name.empty()) return; // Should not happen

    // Your logic to convert string to number if needed and set the variable
    if (var_name.back() == '$') {
        set_variable(*this, var_name, value_str);
    } else {
        try {
            set_variable(*this, var_name, std::stod(value_str));
        } catch (const std::exception&) {
            set_variable(*this, var_name, 0.0);
        }
    }
}

// Implement the frame-by-frame execution logic for Emscripten

void NeReLaBasic::execute_one_frame() {
    // === 1. Pre-Execution Checks ===
    // If the program is already marked as ended, or the pcode is out of bounds, do nothing.
    if (program_ended || !active_p_code || pcode >= active_p_code->size() ) {
        program_ended = true;
        return;
    }
    // Default the internal state to RUNNING for this frame.
    // If a command like INPUT needs to pause, it will change this state.
    this->m_internal_vm_state = VmState::RUNNING;

    // === 2. Execute a Single Statement ===
    // Read the line number for debugging and error reporting.
    //runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
    //pcode += 2;

colon_case_handling:
    // Execute one statement. Skips empty lines (where the token is a carriage return).
    if (static_cast<Tokens::ID>((*active_p_code)[pcode]) != Tokens::ID::C_CR) {
        try {
            statement(); // This is the call that will set m_internal_vm_state to PAUSED if it's an INPUT command.
        } catch (const std::exception& e) {
            Error::set(1, runtime_current_line, "Exception: " + std::string(e.what()));
        }
    }

    // === 3. Post-Execution and Error Handling ===
    // The main execution logic from your original interpreter should be here.
    // Check if the statement caused an error.
    if (jump_to_catch_pending) {
        pcode = pending_catch_address;
        jump_to_catch_pending = false;
        Error::clear();
    } else if (Error::get() != 0) {
        // An unhandled error occurred. Stop the program.
        program_ended = true;
        return;
    }
    
    // Check if the statement caused the task to finish (e.g., RETURN from main).
    if (current_task && current_task->status != TaskStatus::RUNNING) {
        program_ended = true;
        return;
    }

    // === 4. Advance Program Counter for the Next Frame ===
    // This part is crucial. It moves the pcode past the statement/line separators
    // so the next call to this function starts at the right place.
    if (pcode < active_p_code->size()) {
        Tokens::ID next_token = static_cast<Tokens::ID>((*active_p_code)[pcode]);
        
        // If it's a colon, it means there's another statement on the same line.
        // We just consume the colon and return. The next frame will execute the next statement.
        if (next_token == Tokens::ID::C_COLON) {
            pcode++;
            goto colon_case_handling;
        }
        // If it's a carriage return, consume it to move to the next line for the next frame.
        else if (next_token == Tokens::ID::C_CR) {
            pcode++;
            // Read the line number for debugging and error reporting.
            runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
            pcode += 2;
        }
    }
}
#endif

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

static int debug_line = 0;

// The generic debug handler now uses `this->debug_state`.
void NeReLaBasic::handle_debug_events() {
    // If no debugger is attached or we are running freely, do nothing.
    if (!dap_handler)  {
        return;
    }

    // Get the line number for the *next* statement to be executed.
    //runtime_current_line = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
    if (runtime_current_line == debug_line)
    {
        return;
    }
    debug_line = runtime_current_line;
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

/**
 * @brief Recompiles the program's source code "in-place" without resetting the runtime environment.
 * Preserves variables and the call stack, and attempts to reposition the program counter.
 */
void NeReLaBasic::recompile(int vs_code_current_line) {
    if (dap_handler) {
        dap_handler->send_output_message("Recompiling...\n");
    }

    // 1. Remember the line number from VS Code, not our internal one.
    uint16_t target_line = (vs_code_current_line > 0) ? static_cast<uint16_t>(vs_code_current_line) : 0;

    // 2. Perform the recompilation. (This part is unchanged)
    if (this->compiler->recompile_program(*this) != 0) {
        if (dap_handler) {
            dap_handler->send_output_message("? Recompilation failed. Execution halted.\n");
        }
        this->is_stopped = true;
        return;
    }

    // --- THIS LOGIC IS MODIFIED ---
    // 3. Try to find the new p-code address for our TARGET line.
    if (target_line > 0 && this->line_to_pcode_map.count(target_line)) {
        this->pcode = this->line_to_pcode_map[target_line];
        if (dap_handler) {
            dap_handler->send_output_message("Recompilation successful. Resuming at line " + std::to_string(target_line) + ".\n");
        }
    }
    else {
        // The line we were on was deleted or not found.
        // A safe fallback is to go to the start of the program.
        this->pcode = 0;
        if (dap_handler) {
            dap_handler->send_output_message("Recompilation successful. Target line not found, resuming at start.\n");
        }
    }

    // Ensure we skip the initial line number bytes at the new pcode position.
    if (this->pcode < this->program_p_code.size() - 2) {
        this->runtime_current_line = this->program_p_code[pcode] | (this->program_p_code[pcode + 1] << 8);
        this->pcode += 2;
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

int NeReLaBasic::add_recur_task(int interval_ms, const std::string& code) {
    RecurTask task;
    task.id = next_recur_id++; // Assign unique ID and increment
    task.interval_ms = interval_ms;
    task.last_run = std::chrono::steady_clock::now();

    if (this->compiler->tokenize(*this, code, 0, task.pcode, this->main_function_table, false, true) != 0) {
        return -1;
    }

    recurring_tasks.push_back(task);
    return task.id; // Return the explicit ID
}

void NeReLaBasic::remove_recur_task(int id) {
    // We only mark it as inactive here. Erasing it directly while the vector 
    // is potentially being iterated over in the background would cause a crash.
    for (auto& task : recurring_tasks) {
        if (task.id == id) {
            task.active = false;
            break;
        }
    }
}

void NeReLaBasic::evaluate_recurring_tasks() {
    if (recurring_tasks.empty()) return;

    // 1. Safely garbage collect any tasks marked as inactive
    recurring_tasks.erase(
        std::remove_if(recurring_tasks.begin(), recurring_tasks.end(),
            [](const RecurTask& t) { return !t.active; }),
        recurring_tasks.end()
    );

    auto now = std::chrono::steady_clock::now();

    for (auto& task : recurring_tasks) {
        if (!task.active) continue; // Skip if it was marked dead during this exact loop

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - task.last_run).count();
        if (elapsed >= task.interval_ms) {
            task.last_run = now;

            int saved_linenr = this->linenr;
            bool saved_ended = this->program_ended;
            auto* saved_table = this->active_function_table;

            this->execute_synchronous_block(task.pcode);

            if (Error::get() != 0) {
                Error::clear();
            }

            this->linenr = saved_linenr;
            this->program_ended = saved_ended;
            this->active_function_table = saved_table;
        }
    }
}

// Wrapper for synchronous function calls from the expression parser
BasicValue NeReLaBasic::execute_function_for_value(const FunctionInfo& func_info, const std::vector<BasicValue>& args, std::shared_ptr<Map> closure_env) {
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
        return execute_synchronous_function(func_info, args, closure_env);
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
        //process_system_events(); 
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
// We should do a second for REPL and optimize this for speed!
BasicValue NeReLaBasic::execute_synchronous_function(const FunctionInfo& func_info, const std::vector<BasicValue>& args, std::shared_ptr<Map> closure_env) {
    size_t initial_stack_depth = call_stack.size();
    // --- Properly initialize the stack frame ---
    StackFrame frame;
    frame.return_p_code_ptr = this->active_p_code;
    frame.return_pcode = this->pcode;
    frame.previous_function_table_ptr = this->active_function_table;
    frame.loop_stack_size_on_entry = this->loop_stack.size(); // Use the new unified stack
    frame.function_name = func_info.name;
    frame.linenr = runtime_current_line;
    frame.is_async_call = func_info.is_async;

    // --- INJECT CLOSURE VARIABLES ---
    if (closure_env) {
        for (const auto& [key, val] : closure_env->data) {
            frame.local_variables[key] = val;
        }
    }

    for (size_t i = 0; i < func_info.parameter_names.size(); ++i) {
        if (i < args.size()) {
            frame.local_variables[func_info.parameter_names[i]] = args[i];
        }
    }
    call_stack.push_back(frame);

    // --- Context switch ---
    auto prev_active_func_table = this->active_function_table;
    auto prev_active_p_code = this->active_p_code;

    if (func_info.module_name != "REPL" && !func_info.module_name.empty() && compiled_modules.count(func_info.module_name)) {
        this->active_p_code = &this->compiled_modules.at(func_info.module_name).p_code;
        this->active_function_table = &this->compiled_modules.at(func_info.module_name).function_table;
    }
    else {
        if (func_info.module_name != "REPL")
            this->active_p_code = &this->program_p_code;
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
    this->active_function_table = prev_active_func_table;
    this->active_p_code = prev_active_p_code;

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
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    TextIO::initKey();
#endif

    if (dap_handler) {
        debug_state = DebugState::PAUSED;
        dap_handler->send_stopped_message("entry", 1, this->program_to_debug);
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
                if (current_task->result_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    try {
                        current_task->result = current_task->result_future.get();
                        current_task->status = TaskStatus::COMPLETED;
                        int task_id_to_delete = current_task->id;
                        task_completed[task_id_to_delete] = task_queue.at(task_id_to_delete);
                        it = task_queue.erase(it);
                        task_removed = true;
                    }
                    catch (const std::exception& e) {
                        current_task->result = "Error: " + std::string(e.what());
                        current_task->status = TaskStatus::ERRORED;
                    }
                }
                if (!task_removed) ++it;
                continue;
            }

            // --- Context Switch: Load ---
            this->pcode = current_task->p_code_counter;
            this->active_p_code = current_task->p_code_ptr;
            this->call_stack = current_task->call_stack;
            this->loop_stack = current_task->loop_stack; // Use the new unified stack
            if (!this->call_stack.empty()) {
                this->active_function_table = this->call_stack.back().previous_function_table_ptr;
            }
            else {
                this->active_function_table = &this->main_function_table;
            }
            current_task->yielded_execution = false;

            // --- Task Execution Logic ---
            if (current_task->status == TaskStatus::RUNNING) {
                if (pcode >= active_p_code->size()) {
                    current_task->status = TaskStatus::COMPLETED;
                    handle_debug_events();
                }
                else {
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
                                Error::set(1, 1, "Exception " + std::string(e.what()));
                            }

                        }
                        if (program_ended) {
                            line_is_done = true;
                            break;
                        }
                        if (jump_to_catch_pending) {
                            pcode = pending_catch_address;
                            jump_to_catch_pending = false;
                            Error::clear();
                            continue;
                        }
                        if (Error::get() != 0) {
                            if (dap_handler) {
                                Error::print();
                                dap_handler->send_stopped_message("exception", runtime_current_line, this->program_to_debug);
                                pause_for_debugger();
                                Error::clear();
                                continue;
                            }
                            else {
                                current_task->status = TaskStatus::ERRORED;
                                line_is_done = true;
                                continue;
                            }
                        }
                        if (current_task->status != TaskStatus::RUNNING || current_task->yielded_execution) {
                            line_is_done = true;
                            continue;
                        }

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
            current_task->loop_stack = this->loop_stack; // Use the new unified stack

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
    if (sound_system.is_initialized) {
        sound_system.shutdown();
    }

    graphics_system.shutdown();
#endif
#ifdef HTTP
    network_manager.stopServer();
#endif
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    TextIO::deinitKey();
#endif    
    current_task = nullptr;
}

/**
 * @brief Registers a variable as being 'LIVE', initializing its node in the reactive graph.
 * This is called by the DIM command at runtime.
 */
void NeReLaBasic::register_react_variable(const std::string& name) {
    react_variables.insert(name);
    if (reactive_graph.find(name) == reactive_graph.end()) {
        ReactiveNode node;
        node.name = name;
        reactive_graph[name] = node;
    }
}

/**
 * @brief Handles the OP_SET_REACTIVE_DEPENDENCY p-code instruction.
 * This reads the reactive assignment from p-code and builds the dependency links.
 */
void NeReLaBasic::set_reactive_dependency(const std::string& dependent_name) {
    // Read the expression p-code from the main p-code stream
    uint16_t expr_size = (*active_p_code)[pcode] | ((*active_p_code)[pcode + 1] << 8);
    pcode += 2;
    expr_size -= 2;
    std::vector<uint8_t> expression_pcode(active_p_code->begin() + pcode, active_p_code->begin() + pcode + expr_size);
    pcode += expr_size;

    // Ensure the dependent variable is registered as live
    size_t dot_pos = dependent_name.find('.');
    if (dot_pos != std::string::npos) {
        //std::string enum_name = var_or_qual_name.substr(0, dot_pos);
        //std::string member_name = var_or_qual_name.substr(dot_pos + 1);
        auto vartofind = dependent_name.substr(0, dot_pos);;
        if (react_variables.find(vartofind) == react_variables.end()) {
            Error::set(100, runtime_current_line, "Variable '" + vartofind + "' must be declared as LIVE to be used in a reactive assignment.");
            return;
        }
    }
    else {
        if (react_variables.find(dependent_name) == react_variables.end()) {
            Error::set(100, runtime_current_line, "Variable '" + dependent_name + "' must be declared as LIVE to be used in a reactive assignment.");
            return;
        }
    }
    // Add EOL to expression code
    expression_pcode.push_back(0);
    // Store the expression and analyze it to build the graph
    reactive_graph[dependent_name].expression_pcode = expression_pcode;
    analyze_and_build_dependencies(dependent_name, expression_pcode);

    // Initial evaluation
    propagate_changes(dependent_name);
}

/**
 * @brief Scans an expression's p-code to find all variable dependencies (OP_LOAD_VAR).
 * It then updates the graph to link the dependent and its dependencies.
 */
void NeReLaBasic::analyze_and_build_dependencies(const std::string& dependent_name, const std::vector<uint8_t>& expression_pcode) {
    ReactiveNode& node = reactive_graph[dependent_name];
    // Clear old dependencies before re-analyzing
    node.dependencies.clear();

    // This is the worst method to get the dependencies!
    // We should use the skipterm() logic to get the right variables in an expression
    int i = 0;
    while (i < expression_pcode.size()) {
        Tokens::ID token = static_cast<Tokens::ID>(expression_pcode[i]);
        if (token == Tokens::ID::CALLFUNC) {
            while (i < expression_pcode.size()) {
                Tokens::ID token = static_cast<Tokens::ID>(expression_pcode[i]);
                if (token == Tokens::ID::C_LEFTPAREN) break;
                i++;
            }
        }
        if (expression_pcode[i] == 0x22) { // Skip "....."
            while (i < expression_pcode.size()) {
                if (expression_pcode[i] == 0x22) break;
                i++;
            }
        }
        if (token == Tokens::ID::VARIANT) {
            i++; // Move past the token
            std::string  dependency_name;
            while (expression_pcode[i] != 0) {
                dependency_name += expression_pcode[i++];
            }
            i++;

            // We found a dependency!
            node.dependencies.push_back(dependency_name);

            // Now, update the other side of the link: add this node as a dependent to the source variable
            if (reactive_graph.find(dependency_name) != reactive_graph.end()) {
                reactive_graph[dependency_name].dependents.push_back(dependent_name);
            }
            else {
                //Maybe it is a dot var
                size_t dot_pos = dependency_name.find('.');
                if (dot_pos != std::string::npos) {
                    auto vartofind = dependency_name.substr(0, dot_pos);;
                    if (react_variables.find(vartofind) == react_variables.end()) {
                        Error::set(100, runtime_current_line, "Variable '" + vartofind + "' must be declared as LIVE to be used in a reactive assignment.");
                        return;
                    }
                    register_react_variable(dependency_name);
                    reactive_graph[dependency_name].dependents.push_back(dependent_name);
                }
            }

        }
        else {
            // Skip other tokens based on their p-code structure (this part needs to be robust)
            // For simplicity, we just increment. A full implementation would need to know the size of each instruction.
            i++;
        }
    }
}

/**
 * @brief Propagates changes through the graph starting from a changed variable.
 * Uses a topological sort approach (via a queue) to ensure correct evaluation order.
 */
void NeReLaBasic::propagate_changes(const std::string& changed_variable_name, const std::string& key) {
    std::deque<std::string> evaluation_queue;
    std::unordered_set<std::string> visited;

    // Start the propagation with the direct dependents of the variable that just changed.
    if (reactive_graph.count(changed_variable_name)) {
        for (const auto& dependent : reactive_graph.at(changed_variable_name).dependents) {
            if (visited.find(dependent) == visited.end()) {
                evaluation_queue.push_back(dependent);
                visited.insert(dependent);
            }
        }
    }

    // Process the queue until all affected nodes have been re-evaluated.
    while (!evaluation_queue.empty()) {
        std::string node_to_evaluate = evaluation_queue.front();
        evaluation_queue.pop_front();

        // 1. Re-evaluate the node's expression
        ReactiveNode& node = reactive_graph[node_to_evaluate];
        const auto* old_p_code_ptr = active_p_code;
        int old_pcode_pos = pcode;

        active_p_code = &node.expression_pcode; // Temporarily switch context
        pcode = 0;

        BasicValue new_value = evaluate_expression(); // This is your existing expression evaluator

        active_p_code = old_p_code_ptr; // Restore context
        pcode = old_pcode_pos;

        // 2. Check if the value actually changed
        if (new_value != node.last_value) {
            // 3. If it changed, update the variable and trigger further propagation
            size_t dot_pos = node_to_evaluate.find('.');
            if (dot_pos != std::string::npos || !key.empty()) {
                auto vartochange = node_to_evaluate.substr(0, dot_pos);
                auto keytochange = node_to_evaluate.substr(dot_pos + 1);
                if (!key.empty()) {
                    keytochange = key;
                    vartochange = node_to_evaluate;
                }
                // We have a map
                BasicValue& map_var = get_variable(*this, vartochange);
                if (!std::holds_alternative<std::shared_ptr<Map>>(map_var)) {
                    Error::set(15, runtime_current_line); return;
                }
                const auto& map_ptr = std::get<std::shared_ptr<Map>>(map_var);
                if (!map_ptr) { Error::set(15, runtime_current_line); return; }

                // Perform the map insertion/update
                map_ptr->data[keytochange] = new_value;
            }
            else {
                variables[node_to_evaluate] = new_value;
                node.last_value = new_value;
            }

            // Add this node's own dependents to the queue
            for (const auto& next_dependent : node.dependents) {
                if (visited.find(next_dependent) == visited.end()) {
                    evaluation_queue.push_back(next_dependent);
                    visited.insert(next_dependent);
                }
            }
        }
    }
}


void NeReLaBasic::statement() {
#ifdef __EMSCRIPTEN__            
    //m_internal_vm_state = VmState::RUNNING;
#endif    
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
#ifdef __EMSCRIPTEN__        
    case Tokens::ID::INPUT: {
        m_internal_vm_state = VmState::PAUSED_FOR_INPUT;
        pcode++; // Consume INPUT

        // Handle optional prompt string
        if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::STRING) {
            pcode++; // Consume STRING token
            std::string prompt = read_string(*this);
            TextIO::print(prompt);
            if (static_cast<Tokens::ID>((*active_p_code)[pcode]) == Tokens::ID::C_SEMICOLON) {
                pcode++; // Consume ;
            }
        }

        // Get the variable name
        pcode++; // Consume variable type token
        std::string var_to_set = to_upper(read_string(*this));
        // This is now a simple, synchronous call.
        Commands::do_input(*this, var_to_set);
        break;
    }
#else    
    case Tokens::ID::INPUT:
        pcode++;
        Commands::do_input(*this);
        break;
#endif
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
    case Tokens::ID::DESTRUCTURE_ASSIGN:
        pcode++;
        Commands::do_destructure_assign(*this);
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
    case Tokens::ID::FOR_EACH :
        pcode++;
        Commands::do_for_each(*this);
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
    case Tokens::ID::JD_RETURN:
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
    case Tokens::ID::SWITCH:
        pcode++;
        Commands::do_switch(*this);
        break;
    case Tokens::ID::CASE:
        pcode++;
        Commands::do_case(*this);
        break;
    case Tokens::ID::DEFAULT:
        pcode++;
        Commands::do_default(*this);
        break;
    case Tokens::ID::ENDSWITCH:
        pcode++;
        Commands::do_endswitch(*this);
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
    case Tokens::ID::EXIT_SUB:
        pcode++;
        Commands::do_exitsub(*this);
        break;
    case Tokens::ID::EDIT:
        pcode++;
        Commands::do_edit(*this);
        break;
#if !defined(__EMSCRIPTEN__)                
    case Tokens::ID::DLLIMPORT:
        pcode++;
        Commands::do_dllimport(*this);
        break;
#endif
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
    case Tokens::ID::DOS_CD:
        pcode++;
        Commands::do_cd(*this);
        break;
    case Tokens::ID::DOS_DIR:
        pcode++;
        Commands::do_dir(*this);
        break;
    case Tokens::ID::DOS_MKDIR:
        pcode++;
        Commands::do_mkdir(*this);
        break;
    case Tokens::ID::DOS_KILL:
        pcode++;
        Commands::do_kill(*this);
        break;
    case Tokens::ID::DOS_LOADWS:
        pcode++;
        Commands::do_loadws(*this);
        break;
    case Tokens::ID::DOS_SAVEWS:
        pcode++;
        Commands::do_savews(*this);
        break;
    case Tokens::ID::DOS_PRETTY:
        pcode++;
        Commands::do_pretty(*this);
        break;
    case Tokens::ID::OPTION:
        pcode++;
        Commands::do_option(*this);
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
        // NeReLaBasic.cpp

        // ... inside void NeReLaBasic::statement() switch(token) ...

    case Tokens::ID::C_DOT: {
        // Handle Method Chaining: .method()
        pcode++; // Consume '.'
        Commands::do_dot(*this);
        break;
    }

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
#ifdef HTTP
    network_manager(*this),
#endif
    // --- 1. Copy Program Definition Data ---
    source_lines(other.source_lines),
    program_p_code(other.program_p_code),
    direct_p_code(),
    main_function_table(other.main_function_table),
    module_function_tables(other.module_function_tables),
    compiled_modules(other.compiled_modules),
    user_defined_types(other.user_defined_types),
    builtin_constants(other.builtin_constants),
    nopause_active(other.nopause_active)
{
    // --- 2. Initialize Runtime State to Defaults ---
    pcode = 0;
    prgptr = 0;
    linenr = 0;
    runtime_current_line = 0;
    current_source_line = 0;
    current_statement_start_pcode = 0;
    is_stopped = false;
    trace = 0;
    buffer.reserve(64);
    lineinput.reserve(160);
    filename.reserve(40);
    graphmode = 0;
    fgcolor = 2;
    bgcolor = 0;

    // Stacks (must be empty for the new thread)
    variables.clear();
    loop_stack.clear(); // Use the new unified stack
    call_stack.clear();
    func_stack.clear();
    handler_stack.clear();

    // Asynchronous Task System
    task_queue.clear();
    task_completed.clear();
    next_task_id = 0;
    current_task = nullptr;

    // Error Handling State
    error_handler_active = false;
    jump_to_error_handler = false;
    error_handler_function_name = "";
    err_code = 0.0;
    erl_line = 0.0;

    // Debugger State
    dap_handler = nullptr;
    debug_state = DebugState::RUNNING;
    step_over_stack_depth = 0;
    breakpoints.clear();

    // --- 3. Handle Members Requiring Special Initialization ---
    compiler = std::make_unique<Compiler>();
    active_p_code = &this->program_p_code;
    active_function_table = &this->main_function_table;
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

