#include "dap.h"
#include "vm.h"
#include "errors.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define closesocket close
#endif

// Base64 so free-text fields (paths, REPL output, variable values) survive the
// newline-delimited wire protocol intact - they may contain spaces or newlines
// that would otherwise be split into bogus tokens or dropped lines.
static std::string dap_b64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) | c;
        bits += 8;
        while (bits >= 0) { out += T[(val >> bits) & 0x3F]; bits -= 6; }
    }
    if (bits > -6) out += T[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    // base64 of the empty string is empty; emit a marker so the field never
    // collapses into a missing token on the wire.
    return out.empty() ? "=" : out;
}

// Decode a base64 field received from the client (inverse of dap_b64). The
// lone "=" marker (and any all-padding token) decodes to the empty string.
static std::string dap_unb64(const std::string& in) {
    static int8_t T[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) T[i] = -1;
        const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) T[(unsigned char)A[i]] = (int8_t)i;
        init = true;
    }
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (c == '=' || T[c] < 0) continue;
        val = (val << 6) | T[c];
        bits += 6;
        if (bits >= 0) { out += (char)((val >> bits) & 0xFF); bits -= 8; }
    }
    return out;
}

// ── DebugInfo pause/resume ──────────────────────────────────────

void DebugInfo::pause() {
    std::unique_lock<std::mutex> lock(mtx);
    command_received = false;
    cv.wait(lock, [this] { return command_received; });
    command_received = false;
}

void DebugInfo::resume() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        state = DebugState::RUNNING;
        command_received = true;
    }
    cv.notify_one();
}

void DebugInfo::step_over(size_t call_depth) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        state = DebugState::STEP_OVER;
        step_over_depth = call_depth;
        command_received = true;
    }
    cv.notify_one();
}

void DebugInfo::step_in() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        state = DebugState::STEP_IN;
        command_received = true;
    }
    cv.notify_one();
}

void DebugInfo::step_out(size_t call_depth) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        state = DebugState::STEP_OUT;
        step_out_depth = call_depth;
        command_received = true;
    }
    cv.notify_one();
}

// ── DAPHandler ──────────────────────────────────────────────────

DAPHandler::DAPHandler(VM& vm_instance) : vm(vm_instance) {
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
#ifdef _WIN32
    if (listen_socket != (uintptr_t)~0) closesocket((SOCKET)listen_socket);
    if (client_socket != (uintptr_t)~0) closesocket((SOCKET)client_socket);
    listen_socket = ~(uintptr_t)0;
    client_socket = ~(uintptr_t)0;
#else
    if (listen_socket != -1) closesocket(listen_socket);
    if (client_socket != -1) closesocket(client_socket);
    listen_socket = -1;
    client_socket = -1;
#endif
    if (server_thread.joinable()) server_thread.join();
}

void DAPHandler::server_loop(int port) {
#ifdef _WIN32
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return;
    listen_socket = (uintptr_t)ls;
#else
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == -1) return;
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Allow port reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt((SOCKET)listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    if (bind((SOCKET)listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) return;
    if (listen((SOCKET)listen_socket, 1) == SOCKET_ERROR) return;
#else
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) return;
    if (listen(listen_socket, 1) == -1) return;
#endif

    std::cerr << "DAP: Listening on port " << port << std::endl;

    while (server_running) {
#ifdef _WIN32
        SOCKET cs = accept((SOCKET)listen_socket, nullptr, nullptr);
        if (cs == INVALID_SOCKET) continue;
        client_socket = (uintptr_t)cs;
#else
        client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == -1) continue;
#endif
        std::cerr << "DAP: Client connected." << std::endl;
        client_session();
        std::cerr << "DAP: Client disconnected." << std::endl;
    }
}

// ── Receiving ───────────────────────────────────────────────────

void DAPHandler::client_session() {
    // Announce the wire-protocol version first thing, before any stopped/var
    // message can race out from the VM thread. v2 = base64-encoded free-text
    // fields. An old client that ignores this still works; a new client uses
    // it to pick base64 vs legacy plain parsing.
    send_message("proto 2");

    std::string buffer;
    while (server_running.load()) {
        char read_buf[1024];
#ifdef _WIN32
        int bytes = recv((SOCKET)client_socket, read_buf, sizeof(read_buf), 0);
#else
        int bytes = recv(client_socket, read_buf, sizeof(read_buf), 0);
#endif
        if (bytes <= 0) {
#ifdef _WIN32
            closesocket((SOCKET)client_socket);
            client_socket = ~(uintptr_t)0;
#else
            closesocket(client_socket);
            client_socket = -1;
#endif
            break;
        }
        buffer.append(read_buf, bytes);

        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buffer.erase(0, nl + 1);
            if (!line.empty()) process_command(line);
        }
    }
}

// ── Command dispatch ────────────────────────────────────────────

void DAPHandler::process_command(const std::string& command_line) {
    std::stringstream ss(command_line);
    std::string command;
    ss >> command;

    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg) args.push_back(arg);

    if (command == "start") {
        on_start();
    }
    else if (command == "continue") {
        vm.debug->resume();
    }
    else if (command == "next") {
        vm.debug->step_over(vm.debug_call_depth());
    }
    else if (command == "stepin") {
        vm.debug->step_in();
    }
    else if (command == "stepout") {
        vm.debug->step_out(vm.debug_call_depth());
    }
    else if (command == "set_breakpoint") {
        on_set_breakpoint(args);
    }
    else if (command == "clear_all_breakpoints") {
        on_clear_all_breakpoints();
    }
    else if (command == "clear_breakpoints") {
        // clear_breakpoints <file_path>
        if (!args.empty()) {
            // Rejoin args in case file path has spaces
            std::string file = args[0];
            for (size_t i = 1; i < args.size(); i++) file += " " + args[i];
            on_clear_breakpoints(file);
        }
    }
    else if (command == "get_stacktrace") {
        on_get_stacktrace();
    }
    else if (command == "get_vars") {
        on_get_vars(args);
    }
    else if (command == "get_children") {
        int ref = args.empty() ? 0 : atoi(args[0].c_str());
        on_get_children(ref);
    }
    else if (command == "repl") {
        std::string rest = command_line.substr(command_line.find("repl ") + 5);
        on_repl_cmd(rest);
    }
    else if (command == "watch") {
        std::string rest = command_line.substr(command_line.find("watch ") + 6);
        on_watch(rest);
    }
    else if (command == "recompile") {
        int line = -1;
        if (!args.empty()) { try { line = std::stoi(args[0]); } catch (...) {} }
        // Recompile the file and reposition; the callback reports errors via
        // an output message. Either way we re-issue a stopped event so the
        // client refreshes its stack/variables against the (possibly) new code.
        bool ok = on_recompile ? on_recompile(vm, program_path, line) : false;
        (void)ok;
        int report = line > 0 ? line : vm.debug_current_line();
        send_stopped_message("goto", report > 0 ? report : 1, program_path);
    }
    else if (command == "goto") {
        int line = -1;
        if (!args.empty()) { try { line = std::stoi(args[0]); } catch (...) {} }
        on_goto(line);
        // Resume briefly so the interpreter re-enters debug_check at the new line
        // and immediately pauses again (state is still PAUSED)
    }
    else if (command == "exit") {
        vm.debug->resume(); // Unblock interpreter
        stop();
    }
}

// ── Command handlers ────────────────────────────────────────────

void DAPHandler::on_start() {
    // Signal that the client wants to start — the main thread handles
    // compilation and sets up the VM
    {
        std::lock_guard<std::mutex> lock(vm.debug->launch_mtx);
        vm.debug->launch_ready = true;
    }
    vm.debug->launch_cv.notify_one();

    // Wait for compilation result from main thread
    {
        std::unique_lock<std::mutex> lock(vm.debug->launch_mtx);
        vm.debug->launch_cv.wait(lock, [this] { return vm.debug->launch_success || !server_running.load(); });
    }

    if (!vm.debug->launch_success) {
        send_output_message("DAP Error: Compilation failed.\n");
        send_message("exit");
        return;
    }

    // The interpreter will pause at line 1 via debug_check()
    // and send the "stopped entry" message itself.
    send_message("initialized");
}

void DAPHandler::on_set_breakpoint(const std::vector<std::string>& args) {
    // v2: set_breakpoint <file...> <line> <b64cond> <b64hit> <b64log>
    // legacy: set_breakpoint <file...> <line>
    if (args.size() < 2) return;
    try {
        BreakpointInfo bi;
        size_t line_idx;
        if (args.size() >= 5) {
            size_t n = args.size();
            bi.log_message  = dap_unb64(args[n - 1]);
            bi.hit_condition = dap_unb64(args[n - 2]);
            bi.condition    = dap_unb64(args[n - 3]);
            line_idx = n - 4;
        } else {
            line_idx = args.size() - 1;  // legacy: line is the last token
        }
        int line = std::stoi(args[line_idx]);
        std::string file;
        for (size_t i = 0; i < line_idx; i++) {
            if (!file.empty()) file += " ";
            file += args[i];
        }
        if (line > 0) vm.debug->breakpoints[normalize_path(file)][line] = bi;
    } catch (...) {}
}

void DAPHandler::on_clear_all_breakpoints() {
    vm.debug->breakpoints.clear();
}

void DAPHandler::on_clear_breakpoints(const std::string& file) {
    vm.debug->breakpoints.erase(normalize_path(file));
}

void DAPHandler::on_get_stacktrace() {
    // Get call frames from VM — each has {line, func_name, source_file}
    auto frames = vm.debug_get_stack_frames();
    int total = (int)frames.size();
    for (int i = total - 1; i >= 0; i--) {
        const std::string& file = frames[i].file.empty() ? program_path : frames[i].file;
        send_stack_frame_message(i + 1, total, frames[i].line, frames[i].name, file);
    }
    // Add global scope as last frame — use current frame's file
    std::string cur_file = vm.debug_current_file();
    send_stack_frame_message(0, total, vm.debug_current_line(), "[Global]",
        cur_file.empty() ? program_path : cur_file);
}

void DAPHandler::on_get_vars(const std::vector<std::string>& args) {
    if (args.empty()) return;
    std::vector<VM::DebugVar> vars;
    if (args[0] == "local") {
        // get_vars local <frameIndex>: locals of a specific stack frame.
        int frame_index = (args.size() >= 2) ? atoi(args[1].c_str()) : -1;
        vars = vm.debug_vars_local(frame_index);
    } else {
        vars = vm.debug_vars_global();
    }
    for (auto& dv : vars) send_variable_message(dv.name, dv.value, dv.eval_name, dv.ref);
    send_message("varsdone");
}

void DAPHandler::on_get_children(int ref) {
    auto vars = vm.debug_var_children(ref);
    for (auto& dv : vars) send_variable_message(dv.name, dv.value, dv.eval_name, dv.ref);
    send_message("varsdone");
}

void DAPHandler::on_watch(const std::string& input) {
    std::string expr = input;
    while (!expr.empty() && (expr.back() == ' ' || expr.back() == '\r' || expr.back() == '\n'))
        expr.pop_back();
    while (!expr.empty() && (expr.front() == ' ' || expr.front() == '\t'))
        expr.erase(0, 1);
    if (expr.empty()) { send_repl_message("", 0); return; }

    // Full expression resolution: plain names, array indexing, field/key
    // access (sees frame locals), with a global-scope eval fallback. An
    // array/map/UDT result comes back with an expandable handle.
    auto [value, ref] = vm.debug_eval_watch(expr);
    send_repl_message(value, ref);
}

void DAPHandler::on_repl_cmd(const std::string& input) {
    if (!on_repl_eval) {
        send_repl_message("REPL not available.");
        return;
    }

    std::string result = on_repl_eval(vm, input);

    // Trim trailing newlines
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    if (result.empty()) result = "Ready.";
    send_repl_message(result);
}

void DAPHandler::on_goto(int line) {
    if (line > 0 && vm.debug_goto_line(line)) {
        send_stopped_message("goto", line, program_path);
    } else {
        send_output_message("Cannot jump to line " + std::to_string(line) + "\n");
        send_stopped_message("goto", vm.debug_current_line(), program_path);
    }
}

// ── Sending ─────────────────────────────────────────────────────

void DAPHandler::send_message(const std::string& msg) {
#ifdef _WIN32
    if (client_socket != (uintptr_t)~0 && server_running.load()) {
        std::string payload = msg + "\n";
        send((SOCKET)client_socket, payload.c_str(), (int)payload.length(), 0);
    }
#else
    if (client_socket != -1 && server_running.load()) {
        std::string payload = msg + "\n";
        ::send(client_socket, payload.c_str(), (int)payload.length(), 0);
    }
#endif
}

void DAPHandler::send_stopped_message(const std::string& reason, int line, const std::string& path) {
    // Fixed positions: reason line b64(path). path is base64 so spaces in
    // Windows paths don't fragment the token.
    send_message("stopped " + reason + " " + std::to_string(line > 0 ? line : 0) +
                 " " + dap_b64(path));
}

void DAPHandler::send_output_message(const std::string& msg) {
    send_message("output " + dap_b64(msg));
}

void DAPHandler::send_exception_message(const std::string& msg) {
    send_message("exception: " + dap_b64(msg));
}

void DAPHandler::send_repl_message(const std::string& msg, int ref) {
    send_message("repl: " + dap_b64(msg) + " " + std::to_string(ref));
}

void DAPHandler::send_program_ended_message() {
    send_message("ended");
}

void DAPHandler::send_stack_frame_message(int index, int frames, int line,
                                           const std::string& func_name, const std::string& path) {
    send_message("stack: " + std::to_string(index) + " " + std::to_string(frames) +
                 " " + std::to_string(line) + " " + dap_b64(func_name) + " " + dap_b64(path));
}

void DAPHandler::send_variable_message(const std::string& name, const std::string& value,
                                        const std::string& eval_name, int ref) {
    // Format: var: b64(name) b64(value) b64(eval_name) ref. All free-text
    // fields are base64 so multi-line values and spaces survive intact; ref
    // is the expandable-child handle (0 = leaf).
    send_message("var: " + dap_b64(name) + " " + dap_b64(value) + " " +
                 dap_b64(eval_name) + " " + std::to_string(ref));
}
