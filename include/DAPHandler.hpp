// DAPHandler.hpp
#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <vector>

// Forward-declare the main interpreter class to avoid circular includes
class NeReLaBasic;

/**
 * @class DAPHandler
 * @brief Manages the TCP connection and communicates with the debugging client
 * using the custom jdBASIC plain-text protocol.
 */
class DAPHandler {
public:
    DAPHandler(NeReLaBasic& vm_instance);
    ~DAPHandler();

    void start(int port);
    void stop();

    // --- Methods for the Runtime to Send Messages TO the Client ---
    void send_stopped_message(const std::string& reason, int line, const std::string& path = "");
    void send_output_message(const std::string& message);
    void send_repl_message(const std::string& message);
    void send_program_ended_message();
    void send_stack_frame_message(int index, int frames, int line, const std::string& func_name, const std::string& path);
    void send_variable_message(const std::string& scope, const std::string& name, const std::string& value);

private:
    NeReLaBasic& vm;
    std::thread server_thread;
    std::atomic<bool> server_running;
    int listen_socket;
    int client_socket;

    // --- Core Session and Command Processing ---
    void server_loop(int port);
    void client_session();
    void process_command(const std::string& command_line);

    // --- Handlers for Commands FROM the Client ---
    void on_start();
    void on_set_breakpoint(const std::vector<std::string>& args);
    void on_clear_all_breakpoints();
    void on_get_stacktrace();
    void on_get_vars(const std::vector<std::string>& args);

    // Low-level function to send a plain-text message line to the client.
    void send_message(const std::string& message);

    void on_repl(const std::string& inputLine);
};