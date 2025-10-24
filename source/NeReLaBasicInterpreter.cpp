// NeReLaBasicInterpreter.cpp
// Preprocessing: SDL3; HTTP; JDCOM; NOMINMAX; _WINSOCKAPI_; CPPHTTPLIB_OPENSSL_SUPPORT
// Linker: SDL3.lib; SDL3_ttf.lib; libssl.lib;libcrypto.lib;
#include "NeReLaBasic.hpp"
#include "Commands.hpp" // Required for Commands::do_run
#include "Error.hpp"    // Required for Error::print
#include "TextIO.hpp"
#include "DAPHandler.hpp" 
#include <iostream>
#include <string>
#include <fstream>
#ifdef _WIN32
#include <windows.h> 
#include <conio.h>
#else
//#include <ncurses.h>
#endif
#ifdef JDCOM         // also define: NOMINMAX!!!!!
#include <objbase.h> // Required for CoInitializeEx, CoUninitialize
#endif 

#ifdef __EMSCRIPTEN__
#include <stdio.h>
#include <stdlib.h>
#include "Compiler.hpp"
#include <emscripten.h>

// A single interpreter instance for the web page
NeReLaBasic interpreter;

VmState ems_state = VmState::IDLE;

extern "C" {

EMSCRIPTEN_KEEPALIVE
void run_repl_line(const char* cmd) {
    std::string command_line(cmd);
    
    // If a program is running, the first command should stop it.
    /*
    if (ems_state == VmState::RUNNING) {
        ems_state = VmState::IDLE;
        interpreter.program_ended = false;
#ifdef SDL3
            interpreter.graphics_system.shutdown();
            interpreter.sound_system.shutdown();
#endif
#ifdef HTTP
            interpreter.network_manager.stopServer();
#endif            
            emscripten_run_script("window.set_graphics_mode(false);");
            interpreter.active_function_table = &interpreter.main_function_table;        
        TextIO::print("\n*** STOPPED ***\n");
        TextIO::print("Ready\n> ");
        return;
    }
    */
    if (command_line.empty()) {
        TextIO::print("> ");
        return;
    }

    Error::clear();
    interpreter.direct_p_code.clear();

    // Special handling for the RUN command to switch to animation mode
    std::string upper_cmd = command_line;
    std::transform(upper_cmd.begin(), upper_cmd.end(), upper_cmd.begin(), ::toupper);
    if (upper_cmd == "RUN") {
        interpreter.m_internal_vm_state == VmState::RUNNING;
        Commands::do_compile(interpreter);
        if (!interpreter.compiler->do_loop_stack.empty()) {
            interpreter.m_internal_vm_state = VmState::IDLE;
            // There are unclosed DO loops. Get the line number of the last one.
            uint16_t error_line = interpreter.compiler->do_loop_stack.back().source_line;
            Error::set(14, error_line); // Unclosed loop
        }
        if (!interpreter.compiler->if_stack.empty()) {
            interpreter.m_internal_vm_state = VmState::IDLE;
            // There are unclosed Ifs. Get the line number of the last one.
            uint16_t error_line = interpreter.compiler->if_stack.back().source_line;
            Error::set(4, error_line); // Unclosed for
        }
        if (Error::get() != 0) {
            interpreter.m_internal_vm_state = VmState::IDLE;
            Error::print();
        } else {
            // Reset state and switch the VM to RUNNING mode
            // Clear variables and prepare for a clean run
            interpreter.variables.clear();
            interpreter.call_stack.clear();
            interpreter.for_stack.clear();
            interpreter.reactive_graph.clear();
            interpreter.react_variables.clear();
            Error::clear();
            interpreter.is_stopped = false; // Reset the stopped state   
            interpreter.pcode = 0;
            interpreter.active_p_code = &interpreter.program_p_code;
            interpreter.active_function_table = &interpreter.main_function_table;
            interpreter.runtime_current_line = (*interpreter.active_p_code)[interpreter.pcode] | ((*interpreter.active_p_code)[interpreter.pcode + 1] << 8);
            interpreter.pcode += 2;

            ems_state = VmState::RUNNING;
        }
        return; // Don't print a prompt, the loop will start
    }
    
    interpreter.active_function_table = &interpreter.main_function_table;
    if (interpreter.compiler->tokenize(interpreter, command_line, 0, interpreter.direct_p_code, *interpreter.active_function_table,false,true) != 0) {
        Error::print();
        return;
    }
   
    interpreter.execute_synchronous_block(interpreter.direct_p_code);
            
    if (interpreter.program_ended) { // Check if the END command was executed
        Error::clear();
        interpreter.program_ended = false;
    }

    if (Error::get() != 0) {
        Error::print();
    }
    
    TextIO::print("Ready\n> ");
}
} //Extern "C"

/*
EM_JS(void, enter_input_mode, (), {
    window.enter_input_mode();
});
*/

// The main loop function called by the browser on every animation frame
// This constant controls performance. Higher numbers mean faster execution but
// potentially less UI responsiveness during very long, continuous calculations.
// A value between 1,000 and 50,000 is a good starting point.
const int OPERATIONS_PER_TICK = 10000;

void main_loop_tick() {
    // If the state is not RUNNING, do nothing. This correctly handles IDLE and PAUSED states.
    if (ems_state != VmState::RUNNING) {
        return;
    }
    interpreter.yielded_for_frame = false;
    // --- High-Speed Execution Batch ---
    // Execute a large number of statements in a tight loop for performance.
    for (int i = 0; i < OPERATIONS_PER_TICK; ++i) {
        
        interpreter.execute_one_frame();

        if (interpreter.yielded_for_frame==true) {
            break;
        }

        // After EVERY statement, check if the interpreter requested a pause.
        if (interpreter.m_internal_vm_state == VmState::PAUSED_FOR_INPUT) {
            ems_state = VmState::PAUSED_FOR_INPUT; // Update the global state to pause the loop.
            emscripten_run_script("window.enter_input_mode();");
            
            // IMPORTANT: Exit the loop and the function immediately to yield control to the browser.
            return; 
        }

        // Also check if the program has ended (e.g., an END command was hit).
        if (interpreter.program_ended) {
            break; // Exit the batch loop.
        }
    }

    // --- Post-Batch Cleanup ---
    // This code runs after the batch is finished (or was broken by an END command).
    if (interpreter.program_ended) {
        ems_state = VmState::IDLE;
        interpreter.program_ended = false;
#ifdef SDL3
            interpreter.graphics_system.shutdown();
            interpreter.sound_system.shutdown();
#endif
#ifdef HTTP
            interpreter.network_manager.stopServer();
#endif            
            emscripten_run_script("window.set_graphics_mode(false);");
            interpreter.active_function_table = &interpreter.main_function_table;
        TextIO::print("\nReady\n> ");
    }
}

// This C-style function will be callable from JavaScript
extern "C" {

EMSCRIPTEN_KEEPALIVE
void resume_from_input(const char* text) {
    interpreter.assign_input_value(std::string(text));
    // Reset both states to resume execution
    interpreter.m_internal_vm_state = VmState::RUNNING;
    ems_state = VmState::RUNNING;
}

EMSCRIPTEN_KEEPALIVE
void init_repl() {
    interpreter.init_system();
    interpreter.init_basic();
    interpreter.init_screen();
    //js_set_input_mode("idle");
    TextIO::print("Ready\n> ");
    //interpreter.start();
}

EMSCRIPTEN_KEEPALIVE
void update_source_code(const char* new_code_c_str) {
    std::string new_code(new_code_c_str);
    interpreter.source_lines.clear();

    std::stringstream ss(new_code);
    std::string line;
    while (std::getline(ss, line, '\n')) {
        // Remove potential '\r' from Windows line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        interpreter.source_lines.push_back(line);
    }

    TextIO::print("Source code updated. Type LIST to see changes or RUN to execute.\n> ");
}

} // extern "C"

int main() {
    emscripten_set_main_loop(main_loop_tick, 0, 1);
    init_repl();
    return 0;
}
#else
#ifndef XLSLIB
int main(int argc, char* argv[]) {
    // Initialize COM for the current thread
    // COINIT_APARTMENTTHREADED for single-threaded apartment (most common for UI components like Excel)
    // COINIT_MULTITHREADED for multi-threaded apartment (less common for OLE Automation)
#ifdef JDCOM
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        // Handle COM initialization error (e.g., print a message and exit)
        MessageBoxA(NULL, "Failed to initialize COM!", "Error", MB_ICONERROR);
        return 1;
    }
#endif

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#else
    // TextIO::init();
    // curs_set(1); 
#endif

    // Create an instance of our interpreter
    NeReLaBasic interpreter;

    // Check for a DAP flag
    bool dap_mode = false;
    int dap_port = 4711; // Default DAP port
    std::string filename_arg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        interpreter.command_line_args.push_back(argv[i]);
        if (arg == "--debug") {
            dap_mode = true;
            if (i + 1 < argc && isdigit(argv[i + 1][0])) {
                dap_port = std::stoi(argv[++i]);
            }
        }
        else {
            // Capture the first non-flag argument as a potential filename
            if (filename_arg.empty()) {
                filename_arg = arg;
                interpreter.program_to_debug = filename_arg;
            }
        }
    }

    if (dap_mode) {
        DAPHandler dap_server(interpreter);

        interpreter.dap_handler = &dap_server;

        dap_server.start(dap_port);

        TextIO::print("DAP mode is active. Waiting for client to connect and send launch request..."); TextIO::nl();
        interpreter.init_screen();
        interpreter.init_system();
        interpreter.init_basic();
        // Get the future from the promise before the server potentially fulfills it
        auto launch_future = interpreter.dap_launch_promise.get_future();

        // Wait for the on_launch handler to be called and set the promise
        bool launch_ok = launch_future.get();

        if (launch_ok) {
            TextIO::print("Launch request received. Starting execution..."); TextIO::nl();
            dap_server.send_output_message("jdBasic REPL is Ready\n");
            dap_server.send_output_message("Type your command and <enter>\n");
            // The file is now loaded and compiled. Start the execution loop.
            // The loop will immediately pause and wait for a 'continue' or 'step' command.

            interpreter.execute_main_program(interpreter.program_p_code, false);
            if (Error::get() != 0) Error::print();

        }
        else {
            TextIO::print("? DAP Error: Launch failed. Shutting down."); TextIO::nl();
        }

        TextIO::print("\n--- ENDED (Press any key to exit or 'i' to interpret) ---"); TextIO::nl();
#if defined(_WIN32)      
        const char ch = _getch();
        if (ch == 'i') 
            interpreter.start();
#elif defined(__EMSCRIPTEN__)        
#else
        const char ch = getch();
        if (ch == 'i')
            interpreter.start();
#endif        
        dap_server.stop();
    }
    else {

        // Check if a command-line argument (a filename) was provided
        if (argc > 1) {
            std::string filename = argv[1];

            // Use the new method to load the source file
            if (interpreter.loadSourceFromFile(filename)) {
                // File loaded successfully. Now, we can execute the same logic
                // as the RUN command to compile and execute the code.
                interpreter.init_screen();
                interpreter.init_system();
                interpreter.init_basic();
                Commands::do_run(interpreter);

                TextIO::print("\n--- ENDED (Press any key to exit) ---"); TextIO::nl();
#if defined(_WIN32)      
        _getch();
#elif defined(__EMSCRIPTEN__)        
#else
                getch();
#endif     
            }
            // Note: If do_run encounters a runtime error, it is handled internally
            // by the Error::print() call within the execution loop.
        }
        else {
            // No filename was provided, so start the standard interactive REPL
            interpreter.start();
        }
    }

    // Uninitialize COM when the application exits
#ifdef JDCOM
    CoUninitialize();
#endif
#ifndef _WIN32    
    //TextIO::deinit();
#endif
    return 0;
}
#endif //  XLSLIB
#endif