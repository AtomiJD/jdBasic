// DAPHandler.cpp
#include "DAPHandler.hpp"
#include "NeReLaBasic.hpp"
#include "Compiler.hpp"
#include "TextIO.hpp" // For logging/debugging only
#include "Commands.hpp" // For to_string
#include "Error.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Platform-specific socket includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#define closesocket close
#endif

// --- Constructor / Destructor / Server Management (Unchanged) ---

DAPHandler::DAPHandler(NeReLaBasic& vm_instance)
    : vm(vm_instance), server_running(false), listen_socket(-1), client_socket(-1) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

DAPHandler::~DAPHandler() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

void DAPHandler::start(int port) {
    if (server_running) return;
    server_running = true;
    server_thread = std::thread(&DAPHandler::server_loop, this, port);
}

void DAPHandler::stop() {
    server_running = false;
    if (listen_socket != -1) closesocket(listen_socket);
    if (client_socket != -1) closesocket(client_socket);
    listen_socket = -1;
    client_socket = -1;
    if (server_thread.joinable()) server_thread.join();
}

void DAPHandler::server_loop(int port) {
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == -1) return;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) return;
    if (listen(listen_socket, 1) == -1) return;

    send_output_message("Debugger listening on port " + std::to_string(port) + "\n");

    while (server_running) {
        client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == -1) continue;

        send_output_message("Debugger client connected.\n");
        client_session();
        send_output_message("Debugger client disconnected.\n");
    }
}

// --- Receiving and Processing Logic ---

void DAPHandler::client_session() {
    std::string buffer;
    while (server_running.load() && client_socket != -1) {
        char read_buffer[1024];
        int bytes_read = recv(client_socket, read_buffer, sizeof(read_buffer), 0);
        if (bytes_read <= 0) {
            closesocket(client_socket);
            client_socket = -1;
            break;
        }
        buffer.append(read_buffer, bytes_read);

        size_t newline_pos;
        while ((newline_pos = buffer.find('\n')) != std::string::npos) {
            std::string command_line = buffer.substr(0, newline_pos);
            if (!command_line.empty() && command_line.back() == '\r') {
                command_line.pop_back();
            }
            buffer.erase(0, newline_pos + 1);
            if (!command_line.empty()) {
                process_command(command_line);
            }
        }
    }
}

void DAPHandler::process_command(const std::string& command_line) {
    //send_output_message("Debugger Command Received: '" + command_line + "'\n");
    std::stringstream ss(command_line);
    std::string command;
    ss >> command;
    //TextIO::print("Debugger: command: " + command + "'\n");

    // Collect arguments
    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg) {
        //TextIO::print("Debugger: arg: " + arg + "'\n");
        args.push_back(arg);
    }

    // Command Dispatcher
    if (command == "start") {
        // 'start' begins the entire debugging session.
        on_start();
    }
    else if (command == "continue") {
        // 'continue' resumes from a paused state.
        vm.resume_from_debugger();
    }
    else if (command == "next") {
        vm.step_over();
    }
    else if (command == "stepin") {
        vm.step_in();
    }
    else if (command == "stepout") {
        vm.step_out();
    }
    else if (command == "set_breakpoint") {
        on_set_breakpoint(args);
    }
    else if (command == "clear_all_breakpoints") {
        on_clear_all_breakpoints();
    }
    else if (command == "get_stacktrace") {
        on_get_stacktrace();
    }
    else if (command == "get_vars") {
        on_get_vars(args);
    }
    else if (command == "repl") {
        const std::string prefix = "repl ";
        std::string arguments = command_line.substr(prefix.length());
        //send_output_message("repl command: '" + arguments + "'\n");
        on_repl(arguments);
    }
    else if (command == "exit") {
        vm.is_stopped = true; // Tell interpreter to stop
        vm.resume_from_debugger(); // Unblock it so it can terminate
        stop();
    }
}

// --- Handlers for Specific Commands ---
void DAPHandler::on_start() {

    // 1. Load the source code from the file provided by the client
    if (!vm.loadSourceFromFile(vm.program_to_debug)) {
        send_output_message("? DAP Error: Failed to load source file: " + vm.program_to_debug + "\n");
        vm.dap_launch_promise.set_value(false); // Signal failure
        send_message("exit");
        return;
    }

    // 2. Compile the loaded source code
    Commands::do_compile(vm);
    if (Error::get() != 0) {
        send_output_message("? DAP Error: Compilation failed.\n");
        Error::print(); // Show the compilation error
        vm.dap_launch_promise.set_value(false); // Signal failure
        send_message("exit");
        return;
    }

    // 3. Set the debugger to pause at the very first line of code
    vm.debug_state = NeReLaBasic::DebugState::PAUSED;

    // 5. Unblock the main thread so it can call vm.execute()
    vm.dap_launch_promise.set_value(true);
    // This logic is adapted from the old on_launch method.
    // Its job is to kick off the interpreter's execute() loop, which is waiting in the main thread.
    send_stopped_message("entry", 1, vm.program_to_debug);
    send_message("initialized");
}

void DAPHandler::on_set_breakpoint(const std::vector<std::string>& args) {
    // Expects: <path> <line>
    //TextIO::print("Breakpoint: " + args[0]);
    if (args.size() < 2) return;
    try {
        // The path argument (args[0]) is available if needed, but for now
        // we only need the line number for our simple breakpoint map.
        int line = std::stoi(args[1]);
        if (line > 0) {
            vm.breakpoints[line] = true;
            //send_output_message("Breakpoint set at line " + std::to_string(line) + "\n");
        }
    }
    catch (const std::exception&) {
        // Failed to parse line number
    }
}

void DAPHandler::on_clear_all_breakpoints() {
    vm.breakpoints.clear();
    //send_output_message("All breakpoints cleared.\n");
}

void DAPHandler::on_get_stacktrace() {
    // The protocol requires sending one message per stack frame.
    // We send from the innermost frame outwards.
    int i;
    int frames = vm.call_stack.size();
    for (i = frames - 1; i >= 0; --i) {
        const auto& frame = vm.call_stack[i];
        //send_output_message("Stackframe call stack: " + frame.function_name + " " + std::to_string(frame.linenr) + "\n");
        send_stack_frame_message(i+1, frames, frame.linenr, frame.function_name, vm.program_to_debug );
    }
    // Add the current global scope as the last frame
    send_stack_frame_message(i+1, frames, vm.runtime_current_line, "[Global]", vm.program_to_debug);
}

void DAPHandler::on_get_vars(const std::vector<std::string>& args) {
    // Expects: <scope_id> (e.g., "global", "local_0", "local_1")
    if (args.empty()) return;
    const std::string& scope_id = args[0];
    //TextIO::print("Debugger: on_get_vars: arg: " + scope_id + "\n");

    for (const auto& pair : vm.variables) {
        send_variable_message("2 ", pair.first, to_string(pair.second));
    }
    if (!vm.call_stack.empty()) {
        const auto& locals = vm.call_stack.back().local_variables;
        
        if (!locals.empty()) {
            const auto& funcname = vm.call_stack.back().function_name;
            for (const auto& pair : locals) {
                send_variable_message("1 ", funcname + "_" + pair.first, to_string(pair.second));
            }
        }
    }
    send_message("varsdone");
}

// --- Sending Logic ---

void DAPHandler::send_message(const std::string& message) {
    if (client_socket != -1 && server_running.load()) {
        std::string payload = message + "\n";
        send(client_socket, payload.c_str(), (int)payload.length(), 0);
    }
}

void DAPHandler::send_stopped_message(const std::string& reason, int line, const std::string& path) {
    std::string msg = "stopped " + reason;
    if (line > 0) {
        msg += " " + std::to_string(line);
    }
    if (!path.empty()) {
        msg += " " + path;
    }
    send_message(msg);
    //TextIO::print("Debugger Event Sent: '" + msg + "'\n");
}

void DAPHandler::send_output_message(const std::string& message) {
    send_message("output " + message);
}

void DAPHandler::send_repl_message(const std::string& message) {
    send_message("repl: " + message);
}


void DAPHandler::send_program_ended_message() {
    send_message("ended");
    //TextIO::print("Debugger Event Sent: 'ended'\n");
}

void DAPHandler::send_stack_frame_message(int index, int frames, int line, const std::string& func_name, const std::string& path) {
    send_message("stack: " + std::to_string(index) + " " + std::to_string(frames) + " " + std::to_string(line) + " " + func_name + " " + path );
}

void DAPHandler::send_variable_message(const std::string& scope, const std::string& name, const std::string& value) {
    //TextIO::print("Debugger Event Sent: var: '" + name + "'\n");
    send_message("var: " + scope + name + " = " + value);
}

void DAPHandler::on_repl(const std::string& inputLine) {
    std::string captured_output;
    Error::clear();

    // The CoutRedirector captures any `std::cout` from the executed command.
    // The scope block ensures its destructor is called, restoring cout.
    {
        CoutRedirector redirector;

        // Tokenize the REPL input line into its own temporary p-code buffer.
        vm.direct_p_code.clear();
        if (vm.compiler->tokenize(vm, inputLine, 0, vm.direct_p_code, *vm.active_function_table) != 0) {
            // If tokenization fails, print the syntax error to the redirected string.
            Error::print();
        }
        else {
            // *** CALL THE NEW, SAFE REPL EXECUTION FUNCTION ***
            // This runs the command immediately without affecting the debugger's pause state.
            vm.execute_repl_command(vm.direct_p_code);
        }

        // If the command caused a runtime error, print it.
        if (Error::get() != 0) {
            Error::print();
        }

        // Get the captured result.
        captured_output = redirector.getString();
    }

    // If the command produced no output (e.g., an assignment like 'a=1'),
    // provide a default response so the user knows it was processed.
    if (captured_output.empty()) {
        captured_output = "Ready.\n";
    }

    // Send the captured output back to the client.
    send_repl_message(captured_output);
}
