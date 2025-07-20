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
#include <ncurses.h>
#endif
#ifdef JDCOM         // also define: NOMINMAX!!!!!
#include <objbase.h> // Required for CoInitializeEx, CoUninitialize
#endif 

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
    // initscr();
    // cbreak();       // Line buffering disabled
    // noecho();       // Don't echo() while we do getch
#endif

    // Create an instance of our interpreter
    NeReLaBasic interpreter;

    // Check for a DAP flag
    bool dap_mode = false;
    int dap_port = 4711; // Default DAP port
    std::string filename_arg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
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

        TextIO::print("DAP mode is active. Waiting for client to connect and send launch request...\n");
        interpreter.init_screen();
        interpreter.init_system();
        interpreter.init_basic();
        // Get the future from the promise before the server potentially fulfills it
        auto launch_future = interpreter.dap_launch_promise.get_future();

        // Wait for the on_launch handler to be called and set the promise
        bool launch_ok = launch_future.get();

        if (launch_ok) {
            TextIO::print("Launch request received. Starting execution...\n");
            dap_server.send_output_message("jdBasic REPL is Ready\n");
            dap_server.send_output_message("Type your command and <enter>\n");
            // The file is now loaded and compiled. Start the execution loop.
            // The loop will immediately pause and wait for a 'continue' or 'step' command.

            interpreter.execute_main_program(interpreter.program_p_code, false);
        }
        else {
            TextIO::print("? DAP Error: Launch failed. Shutting down.\n");
        }

        dap_server.stop();

        TextIO::print("\n--- ENDED (Press any key to exit) ---\n");
#ifdef _WIN32        
        _getch();
#else
        getch();
#endif        
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

                TextIO::print("\n--- ENDED (Press any key to exit) ---\n");
#ifdef _WIN32        
                _getch();
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

    return 0;
}