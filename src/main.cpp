#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <algorithm>
#include <set>
#include <functional>
#include <filesystem>
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "vm.h"
#include "console.h"
#include "editor.h"
#include "errors.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef COM
#include "com.h"
#include <objbase.h>
#endif
#ifdef HTTP
#include "http.h"
#endif
#ifdef USE_SERIAL
#include "serial.h"
#endif
#ifdef GFX
#include "graphics.h"
#endif
#ifdef IMGUI
#include "gui.h"
#endif
#include "sound.h"
#include "dap.h"
#include "ffi.h"
#include "ai.h"
#include "llm.h"
#include "version.h"
#ifdef LLVM_CODEGEN
#include "llvm_codegen.h"
#endif
#ifdef MCPSERVER
#include "mcp_stdio.h"
#endif

static std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void run_on_vm(VM& vm, const std::string& source);
static void setup_dynamic_code(VM& vm);

// Module file reader: tries modulename.jdb in base_dir, then cwd
static std::string g_base_dir; // directory of main source file

static Parser::FileReader make_module_reader() {
    return [](const std::string& module_name) -> std::pair<std::string, std::string> {
        // Try: base_dir/MODULE.jdb, base_dir/module.jdb, ./MODULE.jdb, ./module.jdb
        std::string upper = module_name;
        std::string lower = module_name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        std::vector<std::string> candidates;
        if (!g_base_dir.empty()) {
            candidates.push_back(g_base_dir + "/" + upper + ".jdb");
            candidates.push_back(g_base_dir + "/" + lower + ".jdb");
            candidates.push_back(g_base_dir + "\\" + upper + ".jdb");
            candidates.push_back(g_base_dir + "\\" + lower + ".jdb");
        }
        candidates.push_back(upper + ".jdb");
        candidates.push_back(lower + ".jdb");

        for (auto& cand : candidates) {
            std::ifstream f(cand);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                // Resolve to absolute path for debugger file matching
                char abs_buf[4096];
#ifdef _WIN32
                DWORD len = GetFullPathNameA(cand.c_str(), sizeof(abs_buf), abs_buf, nullptr);
                std::string resolved = (len > 0 && len < sizeof(abs_buf)) ? std::string(abs_buf) : cand;
#else
                std::string resolved = realpath(cand.c_str(), abs_buf) ? std::string(abs_buf) : cand;
#endif
                return {ss.str(), resolved};
            }
        }
        return {"", ""}; // not found
    };
}

void setup_parser_modules(Parser& parser, const std::string& source_file = "") {
    parser.file_reader = make_module_reader();
    parser.current_source_file = source_file;
}
static void set_os_args(VM& vm, int argc, char* argv[]);
static int g_argc = 0;
static char** g_argv = nullptr;

// Help system (forward declaration for use in setup_dynamic_code)
static std::unordered_map<std::string, std::string> g_help_topics;
static void load_help_file();

// Run source with a fresh VM (for file execution)
static void run_source(const std::string& source, bool show_timing) {
    auto t0 = std::chrono::high_resolution_clock::now();

    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    auto t1 = std::chrono::high_resolution_clock::now();

    Parser parser(tokens);
    setup_parser_modules(parser);
    auto ast = parser.parse();
    auto t2 = std::chrono::high_resolution_clock::now();

    Compiler compiler;
    compiler.compile(ast);
    auto t3 = std::chrono::high_resolution_clock::now();

    VM vm;
    setup_dynamic_code(vm);
    set_os_args(vm, g_argc, g_argv);
    vm.load(compiler.main_chunk(), compiler.functions());
    vm.run();
#ifdef GFX
    sound_shutdown();
    gfx_shutdown();
#endif
    auto t4 = std::chrono::high_resolution_clock::now();

    if (show_timing) {
        auto lex_ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
        auto parse_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        auto comp_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
        auto exec_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();
        auto total_ms = std::chrono::duration<double, std::milli>(t4 - t0).count();
        std::cerr << "\n--- Timing ---\n";
        std::cerr << "Lexer:    " << lex_ms  << " ms\n";
        std::cerr << "Parser:   " << parse_ms << " ms\n";
        std::cerr << "Compiler: " << comp_ms << " ms\n";
        std::cerr << "VM:       " << exec_ms << " ms\n";
        std::cerr << "Total:    " << total_ms << " ms\n";
    }
}

// Run source on an existing VM (keeps state)
void run_on_vm(VM& vm, const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    setup_parser_modules(parser);  // enable IMPORT in REPL/EXECUTE/EVAL
    auto ast = parser.parse();
    Compiler compiler;
    compiler.compile(ast);
    vm.run_code(compiler.main_chunk(), compiler.functions());
}

static void setup_dynamic_code(VM& vm) {
    // EXECUTE: compile and run a string of code
    vm.on_execute = [](VM& v, const std::string& code) {
        run_on_vm(v, code + "\n");
    };

    // JDB.CHECK$ — Lex + Parse only. No compile, no run, no VM mutation.
    // Returns "" on success or the error message on failure.
    vm.on_check = [](VM& /*v*/, const std::string& code) -> std::string {
        try {
            Lexer lexer(code + "\n");
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            setup_parser_modules(parser);
            (void)parser.parse();
            return "";
        } catch (const std::exception& e) {
            return e.what();
        }
    };

    // VARS — list global variables. Available in both console and script
    // modes (the MCP server in mcp/server.jdb relies on it). Filters out
    // names starting with __ which are language internals.
    vm.register_native("VARS", [&vm](const std::vector<Value>& args) -> Value {
        (void)args;
        auto& names = vm.get_global_names();
        auto& globals = vm.get_globals();
        if (names.empty()) { vm.emit("No variables defined.\n"); }
        else {
            for (auto& [name, slot] : names)
                if (slot < globals.size() && name.substr(0,2) != "__")
                    vm.emit("  " + name + " = " + globals[slot].to_string() + "\n");
        }
        return Value::make_none();
    });

    // FUNCS — list user-defined FUNC / SUB / ASYNC FUNC, one per line, with
    // the parameter signature. Companion to VARS — the MCP server's
    // jdb_funcs tool captures this output and filters out boot-time
    // helpers, leaving just what the user has defined in this session.
    vm.register_native("FUNCS", [&vm](const std::vector<Value>& args) -> Value {
        (void)args;
        const auto& funcs = vm.get_funcs();
        if (funcs.empty()) { vm.emit("No functions defined.\n"); return Value::make_none(); }
        for (const auto& f : funcs) {
            if (f.name.size() >= 2 && f.name[0] == '_' && f.name[1] == '_') continue;
            std::string kind = f.is_sub ? "SUB" : (f.is_async ? "ASYNC FUNC" : "FUNC");
            std::string sig = "(";
            for (size_t i = 0; i < f.param_names.size(); i++) {
                if (i > 0) sig += ", ";
                sig += f.param_names[i];
            }
            sig += ")";
            vm.emit("  " + f.name + sig + "  " + kind + "\n");
        }
        return Value::make_none();
    });

    // EVAL: compile "PRINT expr" but intercept the PRINT to capture the value
    // Simpler: compile the expression, wrap in a tiny program that stores result
    vm.on_eval = [](VM& v, const std::string& expr) -> Value {
        std::string code = "LET __EVAL_RESULT__ = " + expr + "\n";
        Lexer lexer(code);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        setup_parser_modules(parser);
        auto ast = parser.parse();
        Compiler compiler;
        compiler.compile(ast);
        v.run_code(compiler.main_chunk(), compiler.functions());
        // Read the result from the global
        auto& names = v.get_global_names();
        auto& globals = v.get_globals();
        auto it = names.find("__EVAL_RESULT__");
        if (it != names.end() && it->second < globals.size())
            return globals[it->second];
        return Value::make_none();
    };
#ifdef COM
    register_com_builtins(vm);
#endif
#ifdef HTTP
    register_http_builtins(vm);
#endif
#ifdef USE_SERIAL
    register_serial_builtins(vm);
#endif
#ifdef GFX
    register_graphics_builtins(vm);
#endif
#ifdef IMGUI
    register_gui_builtins(vm);
#endif
    register_sound_builtins(vm);
    register_ffi_builtins(vm);
    register_ai_builtins(vm);
    register_llm_builtins(vm);

    // HELP (available in both file and console mode)
    vm.register_native("HELP", [&vm](const std::vector<Value>& args) -> Value {
        if (g_help_topics.empty()) load_help_file();
        if (args.empty() || args[0].type == ValueType::NONE ||
            (args[0].type == ValueType::STRING && args[0].as_string()->data.empty())) {
            std::vector<std::string> sorted;
            for (auto& [k, v] : g_help_topics) sorted.push_back(k);
            std::sort(sorted.begin(), sorted.end());
            int col = 0;
            for (auto& t : sorted) {
                char buf[20]; snprintf(buf, sizeof(buf), "%-18s", t.c_str());
                vm.emit(buf);
                if (++col % 4 == 0) vm.emit("\n");
            }
            if (col % 4 != 0) vm.emit("\n");
            vm.emit(std::to_string(sorted.size()) + " topics. Type HELP \"topic\" for details.\n");
        } else {
            std::string topic = args[0].as_string()->data;
            std::transform(topic.begin(), topic.end(), topic.begin(), ::toupper);
            auto it = g_help_topics.find(topic);
            if (it != g_help_topics.end()) {
                vm.emit("[" + topic + "]\n");
                vm.emit(it->second);
            } else {
                vm.emit("No help for: " + topic + "\n");
            }
        }
        return Value::make_none();
    });
    vm.register_native("HELP$", [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (g_help_topics.empty()) load_help_file();
        Value r = Value::make_array();
        for (auto& [k, v] : g_help_topics)
            r.as_array()->elements.push_back(Value::make_string(k));
        return r;
    });
}

static void set_os_args(VM& vm, int argc, char* argv[]) {
    // OS.ARGS exposes everything after the exe name. The exe path itself
    // is dropped — scripts only care about flags + filename + their own
    // args. This matches old jdBasic and lets cowsay.jdb's `Args[0] =
    // "--verbose"` check work as expected.
    Value args_arr = Value::make_array();
    for (int i = 1; i < argc; i++)
        args_arr.as_array()->elements.push_back(Value::make_string(argv[i]));
    vm.register_native("OS.ARGS", [args_arr](const std::vector<Value>& a) -> Value {
        (void)a; return args_arr;
    });
}

// ── Workspace save/load ──────────────────────────────────────

// Non-static so the MCP-stdio server can expose them as `jdb_savews` /
// `jdb_loadws` tools — same persistence the REPL has, callable over MCP.
void save_workspace(VM& vm, const std::string& program_buffer, const std::string& name) {
    std::string filename = name + ".jdws";
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Error: Cannot write to " << filename << std::endl;
        return;
    }

    // Save variables
    out << "[VARIABLES]\n";
    auto& names = vm.get_global_names();
    auto& globals = vm.get_globals();
    for (auto& [vname, slot] : names) {
        if (slot < globals.size()) {
            auto& val = globals[slot];
            out << vname << "\t" << (int)val.type << "\t" << val.to_string() << "\n";
        }
    }

    // Save program buffer
    out << "[PROGRAM]\n";
    out << program_buffer;
    if (!program_buffer.empty() && program_buffer.back() != '\n') out << "\n";

    out.close();
    vm.emit("Workspace saved: " + filename + "\n");
}

void load_workspace(VM& vm, std::string& program_buffer, const std::string& name) {
    std::string filename = name + ".jdws";
    std::ifstream in(filename);
    if (!in.is_open()) {
        std::cerr << "Error: Cannot open " << filename << std::endl;
        return;
    }

    vm.reset();
    program_buffer.clear();

    std::string line;
    enum Section { NONE, VARS, PROGRAM } section = NONE;

    while (std::getline(in, line)) {
        if (line == "[VARIABLES]") { section = VARS; continue; }
        if (line == "[PROGRAM]") { section = PROGRAM; continue; }

        if (section == VARS && !line.empty()) {
            // Parse: name\ttype\tvalue
            size_t t1 = line.find('\t');
            size_t t2 = line.find('\t', t1 + 1);
            if (t1 != std::string::npos && t2 != std::string::npos) {
                std::string vname = line.substr(0, t1);
                int type_id = std::stoi(line.substr(t1 + 1, t2 - t1 - 1));
                std::string val_str = line.substr(t2 + 1);
                ValueType vt = static_cast<ValueType>(type_id);

                Value val;
                switch (vt) {
                    case ValueType::BOOLEAN:
                        val = Value::make_bool(val_str == "TRUE");
                        break;
                    case ValueType::BYTE:
                    case ValueType::INT16:
                    case ValueType::INT32:
                    case ValueType::INT64:
                        try { val = Value::make_i64(std::stoll(val_str)); } catch (...) { val = Value::make_i64(0); }
                        break;
                    case ValueType::FLOAT16:
                    case ValueType::FLOAT32:
                    case ValueType::FLOAT64:
                        try { val = Value::make_f64(std::stod(val_str)); } catch (...) { val = Value::make_f64(0); }
                        break;
                    case ValueType::STRING:
                        val = Value::make_string(val_str);
                        break;
                    default:
                        val = Value::make_string(val_str);
                        break;
                }
                vm.set_global(vname, std::move(val));
            }
        }
        else if (section == PROGRAM) {
            program_buffer += line + "\n";
        }
    }
    in.close();

    // Re-compile and register functions from program buffer
    if (!program_buffer.empty()) {
        try {
            Lexer lexer(program_buffer);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            setup_parser_modules(parser);
            auto ast = parser.parse();

            // Only compile SUB/FUNCTION declarations (don't execute main code)
            Compiler compiler;
            compiler.compile(ast);
            // Register the functions without executing
            auto& funcs = compiler.functions();
            // Create a dummy empty chunk
            Chunk empty;
            empty.emit(OpCode::HALT, 0);
            // Merge functions by running empty code
            vm.run_code(empty, funcs);
        } catch (...) {
            // Ignore errors in function restoration
        }
    }

    vm.emit("Workspace loaded: " + filename + "\n");
}

// ── Console executor ─────────────────────────────────────────

// Helper: strip quotes from argument
static std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// Helper: extract arg after command keyword
static std::string cmd_arg(const std::string& cmd) {
    size_t p = cmd.find_first_of(" \t");
    if (p == std::string::npos) return "";
    size_t s = cmd.find_first_not_of(" \t", p);
    if (s == std::string::npos) return "";
    return strip_quotes(cmd.substr(s));
}

// ── Help system ──────────────────────────────────────────────

static void load_help_file() {
    // Try multiple locations for help.txt
    std::vector<std::string> paths = {"help.txt", "D:\\usr\\dev\\cc\\help.txt"};
    for (auto& p : paths) {
        std::ifstream f(p);
        if (!f) continue;
        std::string line, topic, body;
        while (std::getline(f, line)) {
            if (line.size() >= 3 && line[0] == '[' && line.back() == ']') {
                if (!topic.empty()) g_help_topics[topic] = body;
                topic = line.substr(1, line.size() - 2);
                body.clear();
            } else {
                body += line + "\n";
            }
        }
        if (!topic.empty()) g_help_topics[topic] = body;
        break;
    }
}

// ── Register console commands as native functions ────────────

static std::string* g_program_buffer_ptr = nullptr;
// Last filename a console LOAD/RUN <file> brought in. The editor (EDIT
// without an explicit filename) defaults to this so Ctrl+S writes back
// to the same file instead of prompting again.
static std::string g_loaded_filename;

static void register_console_builtins(VM& vm) {
    auto& pbuf = g_program_buffer_ptr;

    // LOAD
    vm.register_native("LOAD", 1, 1, [&pbuf, &vm](const std::vector<Value>& args) -> Value {
        if (args[0].type != ValueType::STRING)
            throw std::runtime_error("LOAD: filename must be a string");
        std::string filename = args[0].as_string()->data;
        if (filename.empty())
            throw std::runtime_error("LOAD: filename is empty");
        if (filename.find('.') == std::string::npos) filename += ".jdb";
        std::ifstream f(filename);
        if (!f) throw std::runtime_error("Cannot open: " + filename);
        std::ostringstream ss; ss << f.rdbuf();
        if (pbuf) *pbuf = ss.str();
        g_loaded_filename = filename;
        int lines = (int)std::count(pbuf->begin(), pbuf->end(), '\n') + 1;
        vm.emit("Loaded: " + filename + " (" + std::to_string(lines) + " lines)\n");
        return Value::make_none();
    });

    // SAVE
    vm.register_native("SAVE", 1, 1, [&pbuf, &vm](const std::vector<Value>& args) -> Value {
        if (args[0].type != ValueType::STRING)
            throw std::runtime_error("SAVE: filename must be a string");
        std::string filename = args[0].as_string()->data;
        if (filename.empty())
            throw std::runtime_error("SAVE: filename is empty");
        if (filename.find('.') == std::string::npos) filename += ".jdb";
        if (!pbuf || pbuf->empty()) throw std::runtime_error("No program to save");
        std::ofstream out(filename);
        if (!out) throw std::runtime_error("Cannot write: " + filename);
        out << *pbuf;
        vm.emit("Saved: " + filename + "\n");
        return Value::make_none();
    });

    // LIST
    vm.register_native("LIST", [&pbuf, &vm](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!pbuf || pbuf->empty()) { vm.emit("No program loaded.\n"); return Value::make_none(); }
        std::istringstream ss(*pbuf);
        std::string line; int ln = 1;
        while (std::getline(ss, line)) vm.emit(std::to_string(ln++) + "  " + line + "\n");
        return Value::make_none();
    });

    // HELP is registered in setup_dynamic_code (available in both modes)
    // VARS is registered in setup_dynamic_code (so MCP server can use it)
    // HELP$ is registered in setup_dynamic_code
}

// ── Console executor (simplified) ────────────────────────────

static void console_execute(const std::string& cmd, VM& vm, std::string& program_buffer) {
    g_program_buffer_ptr = &program_buffer;
    std::string upper = cmd;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    size_t fs = upper.find_first_not_of(" \t");
    std::string cmd_upper = (fs != std::string::npos) ? upper.substr(fs) : "";

    // ── File commands (need raw string args, not expression parsing) ──
    // Bare `load` / `save` with no argument: print usage instead of letting
    // it fall through to expression parsing (where it would invoke LOAD()
    // with no args and rely on arity-check throwing).
    if (cmd_upper == "LOAD") {
        vm.emit("Usage: LOAD <filename>\n");
        return;
    }
    if (cmd_upper == "SAVE") {
        vm.emit("Usage: SAVE <filename>\n");
        return;
    }
    if (cmd_upper.substr(0, 5) == "LOAD " && cmd_upper != "LOAD") {
        std::string filename = cmd_arg(cmd);
        if (filename.find('.') == std::string::npos) filename += ".jdb";
        try {
            std::ifstream f(filename);
            if (!f) throw std::runtime_error("Cannot open: " + filename);
            std::ostringstream ss; ss << f.rdbuf();
            program_buffer = ss.str();
            g_loaded_filename = filename;
            int lines = (int)std::count(program_buffer.begin(), program_buffer.end(), '\n') + 1;
            vm.emit("Loaded: " + filename + " (" + std::to_string(lines) + " lines)\n");
        } catch (const std::exception& e) { print_error(ErrCode::FILE_NOT_FOUND, e.what()); }
        return;
    }
    if (cmd_upper.substr(0, 5) == "SAVE " && cmd_upper != "SAVE") {
        std::string filename = cmd_arg(cmd);
        if (filename.find('.') == std::string::npos) filename += ".jdb";
        if (program_buffer.empty()) { vm.emit("No program to save.\n"); return; }
        std::ofstream out(filename);
        if (!out) { print_error(ErrCode::FILE_WRITE_ERROR, "Cannot write: " + filename); return; }
        out << program_buffer;
        vm.emit("Saved: " + filename + "\n");
        return;
    }

    // ── Filesystem commands (raw string args) ─────────────────
    if (cmd_upper.substr(0, 3) == "CD " && cmd_upper != "CD") {
        try { vm.call_function("CD", {Value::make_string(cmd_arg(cmd))}); }
        catch (const std::exception& e) { print_error(ErrCode::RUNTIME_ERROR, e.what()); }
        return;
    }
    if (cmd_upper.substr(0, 6) == "MKDIR ") {
        try { vm.call_function("MKDIR", {Value::make_string(cmd_arg(cmd))}); }
        catch (const std::exception& e) { print_error(ErrCode::RUNTIME_ERROR, e.what()); }
        return;
    }
    if (cmd_upper.substr(0, 5) == "KILL ") {
        try { vm.call_function("KILL", {Value::make_string(cmd_arg(cmd))}); }
        catch (const std::exception& e) { print_error(ErrCode::RUNTIME_ERROR, e.what()); }
        return;
    }
    if (cmd_upper.substr(0, 4) == "DIR " || cmd_upper == "DIR") {
        std::string pattern = (cmd_upper.size() > 4) ? cmd_arg(cmd) : "*";
        try { vm.call_function("DIR", {Value::make_string(pattern)}); }
        catch (const std::exception& e) { print_error(ErrCode::RUNTIME_ERROR, e.what()); }
        return;
    }

    // ── Workspace persistence ────────────────────────────────
    if (cmd_upper.substr(0, 7) == "SAVEWS ") {
        save_workspace(vm, program_buffer, cmd_arg(cmd)); return;
    }
    if (cmd_upper.substr(0, 7) == "LOADWS ") {
        load_workspace(vm, program_buffer, cmd_arg(cmd)); return;
    }

    // ── RUN ──────────────────────────────────────────────────
    if (cmd_upper.substr(0, 4) == "RUN " && cmd_upper != "RUN") {
        std::string filename = cmd_arg(cmd);
        if (filename.find('.') == std::string::npos) filename += ".jdb";
        try { run_on_vm(vm, read_file(filename)); }
        catch (const std::exception& e) { std::cerr << "Error: " << e.what() << std::endl; }
        // The script may have called END — clear the halt flag so the
        // REPL accepts further commands.
        vm.is_halted = false;
        return;
    }
    if (cmd_upper == "RUN") {
        if (!program_buffer.empty()) {
            try { run_on_vm(vm, program_buffer); }
            catch (const std::exception& e) { std::cerr << "Error: " << e.what() << std::endl; }
            vm.is_halted = false;
        } else { vm.emit("No program loaded.\n"); }
        return;
    }

    // ── COMPILE (compile only, no run) ───────────────────────
    if (cmd_upper == "COMPILE") {
        if (program_buffer.empty()) { vm.emit("No program to compile.\n"); return; }
        try {
            Lexer lexer(program_buffer);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            setup_parser_modules(parser);
            auto ast = parser.parse();
            Compiler compiler;
            compiler.compile(ast);
            // Merge functions into VM without executing main code
            Chunk empty; empty.emit(OpCode::HALT, 0);
            auto& funcs = compiler.functions();
            vm.run_code(empty, funcs);
            vm.emit("Compiled OK. " + std::to_string(funcs.size()) + " function(s) registered.\n");
        } catch (const std::exception& e) { std::cerr << "Compile error: " << e.what() << std::endl; }
        return;
    }

    // LIST is now a native function

    // ── NEW (clear source + functions, keep globals) ─────────
    if (cmd_upper == "NEW") {
        program_buffer.clear();
        vm.emit("Program cleared.\n");
        return;
    }

    // ── CLEARWS (clear everything) ───────────────────────────
    // LIST_RECUR as console command
    if (cmd_upper == "LIST_RECUR") {
        try { run_on_vm(vm, "LIST_RECUR()\n"); } catch (...) {}
        return;
    }

    if (cmd_upper == "CLEARWS") {
        program_buffer.clear();
        vm.reset();
#ifdef GFX
        gfx_shutdown();
#endif
        vm.emit("Workspace cleared (source + variables + functions).\n");
        return;
    }

    // ── RESUME ───────────────────────────────────────────────
    if (cmd_upper == "RESUME") {
        if (!vm.resume()) vm.emit("Nothing to resume.\n");
        return;
    }

    // ── TRON / TROFF ─────────────────────────────────────────
    if (cmd_upper == "TRON") { vm.trace_enabled = true; vm.emit("Trace ON\n"); return; }
    if (cmd_upper == "TROFF") { vm.trace_enabled = false; vm.emit("Trace OFF\n"); return; }

    // ── PWD (bare command prints, PWD() in code returns silently) ─
    if (cmd_upper == "PWD") {
        try { run_on_vm(vm, "PRINT PWD()\n"); } catch (...) {}
        return;
    }

    // ── CD (bare command in console: change dir and print) ───
    if (cmd_upper.substr(0, 3) == "CD " || cmd_upper == "CD") {
        std::string arg;
        if (cmd.length() > 3) {
            arg = cmd.substr(3);
            // Trim
            while (!arg.empty() && (arg.front() == ' ' || arg.front() == '"')) arg.erase(0, 1);
            while (!arg.empty() && (arg.back() == ' ' || arg.back() == '"')) arg.pop_back();
        }
        try {
            if (arg.empty()) {
                run_on_vm(vm, "PRINT CD()\n");
            } else {
                // Escape backslashes for the BASIC string literal
                std::string esc;
                for (char c : arg) { if (c == '\\') esc += "\\\\"; else esc += c; }
                run_on_vm(vm, "PRINT CD(\"" + esc + "\")\n");
            }
        } catch (const std::exception& e) {
            std::cerr << "CD error: " << e.what() << std::endl;
        }
        return;
    }

    // CLS is now a native function

    // ── DUMP ─────────────────────────────────────────────────
    if (cmd_upper == "DUMP" || cmd_upper.substr(0, 5) == "DUMP ") {
        std::string arg = cmd_arg(cmd);
        std::string arg_upper = arg;
        std::transform(arg_upper.begin(), arg_upper.end(), arg_upper.begin(), ::toupper);

        if (arg_upper == "REACT") {
            auto& bindings = vm.reactive_bindings;
            if (bindings.empty()) { vm.emit("No reactive bindings.\n"); }
            else {
                vm.emit("--- Reactive Graph (" + std::to_string(bindings.size()) + " bindings) ---\n");
                for (auto& [name, b] : bindings) {
                    std::string line = "  " + name + " -> " + b.formula + "  [deps: ";
                    for (size_t i = 0; i < b.dependencies.size(); i++) {
                        if (i > 0) line += ", ";
                        line += b.dependencies[i];
                    }
                    line += "]\n";
                    vm.emit(line);
                }
            }
            return;
        }
        if (arg_upper == "GLOBAL" || arg_upper == "VARS") {
            auto& names = vm.get_global_names();
            auto& globals = vm.get_globals();
            vm.emit("--- Global Variables (" + std::to_string(names.size()) + ") ---\n");
            for (auto& [name, slot] : names) {
                if (slot < globals.size() && name.substr(0,2) != "__")
                    vm.emit("  " + name + " = " + globals[slot].to_string() + "\n");
            }
        } else if (arg_upper == "STACK") {
            vm.emit("--- Call Stack ---\n");
            vm.emit("  (not in active execution)\n");
        } else {
            // Dump bytecode hex
            if (program_buffer.empty()) { vm.emit("No program compiled.\n"); return; }
            try {
                Lexer lexer(program_buffer);
                auto tokens = lexer.tokenize();
                Parser parser(tokens);
                setup_parser_modules(parser);
                auto ast = parser.parse();
                Compiler compiler;
                compiler.compile(ast);

                auto hex_dump = [&vm](const std::string& label, const std::vector<uint8_t>& code) {
                    vm.emit("Dumping p_code for '" + label + "' (" + std::to_string(code.size()) + " bytes):\n");
                    for (size_t i = 0; i < code.size(); i += 16) {
                        char buf[128];
                        int pos = snprintf(buf, sizeof(buf), "0x%04X : ", (unsigned)i);
                        for (size_t j = 0; j < 16; j++) {
                            if (i + j < code.size()) pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", code[i + j]);
                            else pos += snprintf(buf + pos, sizeof(buf) - pos, "   ");
                        }
                        pos += snprintf(buf + pos, sizeof(buf) - pos, ": ");
                        for (size_t j = 0; j < 16 && i + j < code.size(); j++) {
                            uint8_t c = code[i + j];
                            buf[pos++] = (c >= 32 && c < 127) ? c : '.';
                        }
                        buf[pos++] = '\n'; buf[pos] = '\0';
                        vm.emit(buf);
                    }
                };

                hex_dump("main program", compiler.main_chunk().code);
                for (auto& f : compiler.functions()) {
                    vm.emit("\n");
                    hex_dump(f.name + " (" + std::to_string(f.arity) + " params)", f.chunk.code);
                }
            } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << std::endl; }
        }
        return;
    }

    // ── PRETTY ───────────────────────────────────────────────
    if (cmd_upper.substr(0, 6) == "PRETTY") {
        if (program_buffer.empty()) { vm.emit("No program loaded.\n"); return; }
        bool preview = (cmd_upper.find("PREVIEW") != std::string::npos);
        bool vb_style = (cmd_upper.find("VB") != std::string::npos);

        std::istringstream ss(program_buffer);
        std::string line;
        std::string result;
        int indent = 0;
        std::string tab = "    ";

        while (std::getline(ss, line)) {
            // Trim
            size_t s = line.find_first_not_of(" \t");
            std::string trimmed = (s != std::string::npos) ? line.substr(s) : "";
            std::string upper_trimmed = trimmed;
            std::transform(upper_trimmed.begin(), upper_trimmed.end(), upper_trimmed.begin(), ::toupper);

            // Decrease indent for closing keywords
            if (upper_trimmed.substr(0,4) == "END " || upper_trimmed.substr(0,7) == "ENDFUNC" ||
                upper_trimmed.substr(0,6) == "ENDSUB" || upper_trimmed.substr(0,5) == "ENDIF" ||
                upper_trimmed.substr(0,7) == "ENDTYPE" || upper_trimmed.substr(0,9) == "ENDSWITCH" ||
                upper_trimmed.substr(0,6) == "ENDTRY" || upper_trimmed.substr(0,7) == "ENDENUM" ||
                upper_trimmed.substr(0,4) == "NEXT" || upper_trimmed.substr(0,4) == "LOOP" ||
                upper_trimmed.substr(0,5) == "CATCH" || upper_trimmed.substr(0,7) == "FINALLY" ||
                upper_trimmed.substr(0,6) == "ELSEIF" || upper_trimmed.substr(0,4) == "ELSE" ||
                upper_trimmed.substr(0,7) == "DEFAULT" || upper_trimmed.substr(0,4) == "CASE")
                if (indent > 0) indent--;

            // Apply indent
            std::string indented;
            for (int i = 0; i < indent; i++) indented += tab;

            // Optionally uppercase keywords
            if (vb_style) {
                // Keep as-is (VB style = mixed case, original)
                indented += trimmed;
            } else {
                indented += trimmed;
            }

            result += indented + "\n";

            // Increase indent for opening keywords
            if (upper_trimmed.substr(0,3) == "IF " || upper_trimmed.substr(0,4) == "SUB " ||
                upper_trimmed.substr(0,9) == "FUNCTION " || upper_trimmed.substr(0,5) == "FUNC " ||
                upper_trimmed.substr(0,4) == "FOR " || upper_trimmed.substr(0,3) == "DO " ||
                upper_trimmed == "DO" || upper_trimmed.substr(0,5) == "TYPE " ||
                upper_trimmed.substr(0,7) == "SWITCH " || upper_trimmed.substr(0,5) == "ENUM " ||
                upper_trimmed == "TRY" || upper_trimmed.substr(0,5) == "CATCH" ||
                upper_trimmed.substr(0,7) == "FINALLY" || upper_trimmed.substr(0,6) == "ELSEIF" ||
                upper_trimmed == "ELSE" || upper_trimmed.substr(0,4) == "CASE" ||
                upper_trimmed == "DEFAULT")
                indent++;
        }

        if (preview) {
            vm.emit(result);
        } else {
            program_buffer = result;
            vm.emit("Source formatted.\n");
        }
        return;
    }

    // ── LINT ─────────────────────────────────────────────────
    if (cmd_upper == "LINT") {
        if (program_buffer.empty()) { vm.emit("No program loaded.\n"); return; }
        try {
            Lexer lexer(program_buffer);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            setup_parser_modules(parser);
            auto ast = parser.parse();

            // Per-file OPTION state mirrors the LLVM codegen pre-pass —
            // EXPLICIT/STRICT are file-scoped, so an imported loose module
            // stays lintable against a strict main file.
            std::set<std::string> explicit_files, strict_files;
            for (auto& s : ast) {
                if (s && s->kind == StmtKind::OPTION_STMT && s->expr &&
                    s->expr->kind == ExprKind::LITERAL_STRING) {
                    std::string opt = s->expr->str_val;
                    std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
                    if (opt == "EXPLICIT")        explicit_files.insert(s->source_file);
                    else if (opt == "STRICT")     strict_files.insert(s->source_file);
                    else if (opt == "EXPLICITOFF" || opt == "NOEXPLICIT")
                                                  explicit_files.erase(s->source_file);
                    else if (opt == "NOSTRICT" || opt == "STRICTOFF")
                                                  strict_files.erase(s->source_file);
                }
            }

            // Pass 1: collect all declared names (globals + locals, flat scope).
            // We do not model lexical scopes here — the LINT view is "would any
            // name resolve somewhere?" which matches BASIC's mostly-flat
            // visibility model and keeps false positives low.
            std::set<std::string> declared;
            std::vector<std::string> defined_funcs;
            std::function<void(const std::vector<StmtPtr>&)> collect;
            collect = [&](const std::vector<StmtPtr>& stmts) {
                for (auto& s : stmts) {
                    if (!s) continue;
                    switch (s->kind) {
                        case StmtKind::LET:
                        case StmtKind::DIM:
                            declared.insert(s->var_name);
                            break;
                        case StmtKind::SUB:
                        case StmtKind::FUNCTION:
                            declared.insert(s->func_name);
                            defined_funcs.push_back(s->func_name);
                            for (auto& p : s->params) declared.insert(p.name);
                            collect(s->body);
                            break;
                        case StmtKind::FOR_LOOP:
                        case StmtKind::FOR_EACH:
                            declared.insert(s->var_name);
                            collect(s->body);
                            break;
                        case StmtKind::DESTRUCTURE:
                            for (auto& v : s->destruct_vars) declared.insert(v);
                            break;
                        case StmtKind::IF:
                        case StmtKind::SWITCH_STMT:
                            for (auto& br : s->branches) collect(br.body);
                            break;
                        case StmtKind::DO_LOOP:
                            collect(s->body);
                            break;
                        case StmtKind::TRY_CATCH:
                            collect(s->body);
                            collect(s->catch_body);
                            collect(s->finally_body);
                            break;
                        case StmtKind::TYPE_DECL:
                            declared.insert(s->func_name);
                            collect(s->body);
                            break;
                        case StmtKind::ENUM_DECL:
                            declared.insert(s->func_name);
                            for (auto& m : s->enum_members) declared.insert(m.first);
                            break;
                        default: break;
                    }
                }
            };
            collect(ast);

            // Built-in natives + a few bare-word constants are always in scope.
            for (auto& n : vm.native_names()) declared.insert(n);
            for (const char* k : {"PI","E","TRUE","FALSE","NULL","VBNEWLINE","NOTHING",
                                  "ERR","ERRMSG$","ERRLINE"})
                declared.insert(k);

            // Pass 2: walk every expression, collecting undeclared VARIABLE refs
            // and (under STRICT) DIMs with no type annotation.
            std::vector<std::string> undeclared;

            auto head_name = [](const std::string& n) {
                // Dotted lookups (enums, modules, UDTs) resolve via the head
                // identifier — "Direction.NORTH" is OK if "Direction" is
                // declared. We only need to prove the entry point exists.
                auto dot = n.find('.');
                return dot == std::string::npos ? n : n.substr(0, dot);
            };
            std::function<void(const Expr*)> walk_expr = [&](const Expr* e) {
                if (!e) return;
                if (e->kind == ExprKind::VARIABLE && !e->str_val.empty()) {
                    std::string head = head_name(e->str_val);
                    if (!declared.count(head) && !declared.count(e->str_val))
                        undeclared.push_back(e->str_val + " (line " + std::to_string(e->line) + ")");
                }
                walk_expr(e->left.get());
                walk_expr(e->right.get());
                for (auto& a : e->args) walk_expr(a.get());
            };

            std::function<void(const std::vector<StmtPtr>&)> walk_stmts;
            walk_stmts = [&](const std::vector<StmtPtr>& stmts) {
                for (auto& s : stmts) {
                    if (!s) continue;
                    bool in_explicit = explicit_files.count(s->source_file) > 0;
                    bool in_strict   = strict_files.count(s->source_file) > 0;
                    if (in_explicit) {
                        // Bare ASSIGN to an undeclared name is the classic
                        // "OPTION EXPLICIT" violation the codegen errors on.
                        if ((s->kind == StmtKind::ASSIGN || s->kind == StmtKind::INDEX_ASSIGN) &&
                            !s->var_name.empty()) {
                            std::string head = head_name(s->var_name);
                            if (!declared.count(head) && !declared.count(s->var_name))
                                undeclared.push_back(s->var_name + " (line " + std::to_string(s->line) + ")");
                        }
                        walk_expr(s->expr.get());
                        for (auto& pe : s->print_exprs) walk_expr(pe.get());
                        for (auto& ix : s->index_chain) walk_expr(ix.get());
                        walk_expr(s->loop_cond.get());
                        walk_expr(s->end_expr.get());
                        walk_expr(s->step_expr.get());
                        for (auto& br : s->branches) walk_expr(br.condition.get());
                    }
                    (void)in_strict; // STRICT type-mismatch checks live in the codegen
                    walk_stmts(s->body);
                    walk_stmts(s->catch_body);
                    walk_stmts(s->finally_body);
                    for (auto& br : s->branches) walk_stmts(br.body);
                }
            };
            walk_stmts(ast);

            vm.emit("LINT: Parsed OK.\n");
            vm.emit("  " + std::to_string(ast.size()) + " top-level statements\n");
            vm.emit("  " + std::to_string(defined_funcs.size()) + " function/sub definitions\n");

            if (!explicit_files.empty() || !strict_files.empty()) {
                vm.emit("  OPTION flags active:\n");
                std::set<std::string> all_files = explicit_files;
                for (auto& f : strict_files) all_files.insert(f);
                for (auto& f : all_files) {
                    std::string modes;
                    if (explicit_files.count(f)) modes += "EXPLICIT ";
                    if (strict_files.count(f))   modes += "STRICT";
                    vm.emit("    " + (f.empty() ? std::string("(main)") : f) + ": " + modes + "\n");
                }
            } else {
                vm.emit("  No OPTION EXPLICIT or STRICT detected.\n");
            }

            int warnings = 0;
            if (!undeclared.empty()) {
                warnings += (int)undeclared.size();
                vm.emit("  EXPLICIT undeclared refs: " + std::to_string(undeclared.size()) + "\n");
                int shown = 0;
                for (auto& u : undeclared) {
                    if (++shown > 10) {
                        vm.emit("    ... (+" + std::to_string(undeclared.size() - 10) + " more)\n");
                        break;
                    }
                    vm.emit("    " + u + "\n");
                }
            }
            if (warnings == 0) vm.emit("  No warnings.\n");
            else vm.emit("  Total warnings: " + std::to_string(warnings) + "\n");
        } catch (const std::exception& e) {
            std::cerr << "LINT error: " << e.what() << std::endl;
        }
        return;
    }

    // ── EDIT ─────────────────────────────────────────────────
    if (cmd_upper == "EDIT" || cmd_upper.substr(0, 5) == "EDIT ") {
        std::string edit_file;
        if (cmd_upper.size() > 5) {
            edit_file = cmd_arg(cmd);
            if (edit_file.find('.') == std::string::npos) edit_file += ".jdb";
        }
        std::vector<std::string> lines;
        if (!edit_file.empty()) {
            std::ifstream in(edit_file);
            if (in.is_open()) { std::string l; while (std::getline(in, l)) lines.push_back(l); }
        } else if (!program_buffer.empty()) {
            std::istringstream ss(program_buffer);
            std::string l; while (std::getline(ss, l)) lines.push_back(l);
        }
        if (lines.empty()) lines.push_back("");
        // Default the editor's filename to the last LOAD'd file so Ctrl+S
        // writes back to it instead of prompting.
        std::string editor_filename = !edit_file.empty() ? edit_file : g_loaded_filename;
        Editor editor(lines, editor_filename);
        editor.run();
        std::string new_buf;
        for (size_t i = 0; i < lines.size(); i++) {
            new_buf += lines[i];
            if (i + 1 < lines.size()) new_buf += "\n";
        }
        program_buffer = new_buf;
        // F5 in the editor: compile + run the buffer (no save).
        if (editor.wants_run()) {
            try { run_on_vm(vm, program_buffer); }
            catch (const jdError& e) { print_error(e.code, e.what(), e.line); }
            catch (const std::exception& e) {
                print_error(ErrCode::RUNTIME_ERROR, e.what());
            }
            vm.is_halted = false;
        }
        return;
    }

    // VARS is now a native function
    // HELP is now a native function with help.txt parsing

    // ── Direct jdBasic execution on persistent VM ────────────
    try {
        run_on_vm(vm, cmd + "\n");
        vm.is_halted = false;
    } catch (const jdError& e) {
        print_error(e.code, e.what(), e.line);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        // Classify common errors
        if (msg.find("Parse error") != std::string::npos)
            print_error(ErrCode::SYNTAX_ERROR, msg);
        else if (msg.find("Undefined function") != std::string::npos)
            print_error(ErrCode::UNDEFINED_FUNCTION, msg);
        else
            print_error(ErrCode::RUNTIME_ERROR, msg);
    } catch (const std::exception& e) {
        print_error(ErrCode::RUNTIME_ERROR, e.what());
    }
}

int main(int argc, char* argv[]) {
    g_argc = argc; g_argv = argv;
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
#ifdef COM
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif

    if (argc < 2) {
        // Clear screen and show banner
        std::cout << "\033[2J\033[H";
        std::cout << jdbasic_banner() << std::endl;

        Console console;
        Console* pCon = &console;

        // Setup all workspace VMs
        console.for_each_vm([&](VM& vm) {
            setup_dynamic_code(vm);
            register_console_builtins(vm);
            set_os_args(vm, argc, argv);

            // RECUR task natives
            vm.register_native("RECUR", 2, 2, [pCon](const std::vector<Value>& args) -> Value {
                if (args[1].type != ValueType::STRING)
                    throw std::runtime_error("RECUR: code must be a string");
                int interval = (int)args[0].to_int();
                std::string code = args[1].as_string()->data;
                int id = pCon->add_recur_task(interval, code);
                return Value::make_i64(id);
            });
            vm.register_native("CLEAR_RECUR", 1, 1, [pCon](const std::vector<Value>& args) -> Value {
                pCon->clear_recur_task((int)args[0].to_int());
                return Value::make_none();
            });
            vm.register_native("LIST_RECUR", [pCon](const std::vector<Value>& args) -> Value {
                (void)args;
                pCon->list_recur_tasks();
                return Value::make_none();
            });

            // on_tick: process RECUR tasks during program execution
            vm.on_tick = [pCon]() {
                pCon->process_recur_tasks();
                if (!pCon->pending_executions.empty()) {
                    std::vector<std::string> to_run;
                    { std::lock_guard<std::mutex> lock(pCon->recur_mutex);
                      to_run.swap(pCon->pending_executions); }
                    for (auto& code : to_run) {
                        try { run_on_vm(pCon->active_vm(), code + "\n"); } catch (...) {}
                    }
                }
            };
        });

        console.set_executor(console_execute);
        console.run();
        return 0;
    }

    // Parse flags first. Stop at the FIRST non-flag argument — that's the
    // script filename. Anything after the filename is passed through to
    // OS.ARGS verbatim, so `jdbasic cowsay.jdb --foo bar` makes both
    // "--foo" and "bar" reachable from the script.
    std::string filename;
    bool timing = false;
    int debug_port = 0;
    bool compile_native = false;
    bool emit_ir_only = false;
    bool lint_mode = false;
    bool mcp_mode = false;
    std::string compile_output;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h" || a == "-?") {
            std::cout <<
              "jdBasic v" JDBASIC_VERSION " (Build " JDBASIC_BUILD_NUM ", " JDBASIC_BUILD_DATE ")\n"
              "Features: " << jdbasic_features() << "   OS: " << jdbasic_os() << "\n"
              "\n"
              "USAGE\n"
              "  jdbasic                        Start interactive REPL / workspace\n"
              "  jdbasic <file.jdb> [args...]   Run a script in the interpreter\n"
              "  jdbasic -c <file.jdb>          Compile to a native .exe (LLVM)\n"
              "  jdbasic --lint <file.jdb>      Static analysis only (no execution)\n"
              "  jdbasic --mcp                  Run as MCP server over stdio (requires MCPSERVER build)\n"
              "  jdbasic -d [port] <file.jdb>   Run under the DAP debugger (default 4711)\n"
              "\n"
              "FLAGS\n"
              "  -h, --help, -?           Show this help and exit\n"
              "  -V, --version            Show version info and exit\n"
              "  -t, --time               Print execution timing after the script\n"
              "  -d, --debug [port]       Start the DAP debug server on <port> (default 4711)\n"
              "  -c, --compile            Compile the script to native code (requires NATIVEC)\n"
              "  -o, --output <file>      Write the compiled .exe to <file> (default: script name)\n"
              "      --lint               Parse + typecheck only, do not run\n"
              "      --mcp                Speak the Model Context Protocol on stdio\n"
              "      --emit-ir            Emit LLVM IR to stdout instead of an .exe\n"
              "      --trace              Compile with codegen trace logging on\n"
              "\n"
              "EXAMPLES\n"
              "  jdbasic my_script.jdb arg1 arg2        # interpret, args visible via OS.ARGS\n"
              "  jdbasic -c my_script.jdb               # → my_script.exe + jdbrt.dll runtime\n"
              "  jdbasic -c -o build/app.exe app.jdb    # named output\n"
              "  jdbasic --lint module.jdb              # CI-friendly syntax check\n"
              "  jdbasic -d 5678 my_script.jdb          # wait for DAP attach on :5678\n"
              "\n"
              "NOTES\n"
              "  Compiled .exes need jdbrt.dll next to them at runtime — copy it from build/.\n"
              "  Everything after the script filename is passed through to the script via OS.ARGS.\n"
              "  Run without arguments to drop into the interactive workspace (see HELP inside).\n";
            return 0;
        }
        if (a == "--version" || a == "-V") {
            std::cout << "jdBasic v" JDBASIC_VERSION " (Build " JDBASIC_BUILD_NUM ", " JDBASIC_BUILD_DATE ")\n"
                      << "Features: " << jdbasic_features() << "\n"
                      << "OS:       " << jdbasic_os() << "\n";
            return 0;
        }
        if (a == "--time" || a == "-t") { timing = true; continue; }
        if (a == "--verbose" || a == "-v") { /* old-style flag, ignore */ continue; }
        if (a == "--debug" || a == "-d") {
            debug_port = 4711;
            if (i + 1 < argc) {
                try { int p = std::stoi(argv[i + 1]); debug_port = p; i++; } catch (...) {}
            }
            continue;
        }
        if (a == "--compile" || a == "-c") { compile_native = true; continue; }
        if (a == "--lint") { lint_mode = true; continue; }
        if (a == "--mcp") { mcp_mode = true; continue; }
        if (a == "--emit-ir") { emit_ir_only = true; continue; }
        if (a == "--trace") { compile_native = true; /* set debug_log below */ continue; }
        if ((a == "-o" || a == "--output") && i + 1 < argc) { compile_output = argv[++i]; continue; }
        filename = a;
        break;
    }

    // ── MCP server mode (stdio) — no filename required ──────────
    if (mcp_mode) {
#ifdef MCPSERVER
        VM vm;
        setup_dynamic_code(vm);
        return run_mcp_stdio(vm);
#else
        std::cerr << "MCP server mode not available (build with MCPSERVER=1)." << std::endl;
        return 1;
#endif
    }

    if (filename.empty()) {
        std::cerr << "Error: no input file specified. Run `jdbasic --help` for usage." << std::endl;
        return 1;
    }

    // ── LINT (static analysis only, no execution) ───────────────
    if (lint_mode) {
        {
            size_t sep = filename.find_last_of("/\\");
            g_base_dir = (sep != std::string::npos) ? filename.substr(0, sep) : ".";
        }
        std::string program_buffer;
        try { program_buffer = read_file(filename); }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
        VM vm;
        setup_dynamic_code(vm);
        console_execute("LINT", vm, program_buffer);
        return 0;
    }

    // ── Native compilation (LLVM) ────────────────────────────────
    if (compile_native || emit_ir_only) {
#ifdef LLVM_CODEGEN
        // Phase markers + try/catch on every step — without these the
        // process used to die silently on the deployment machine when
        // any phase threw (parser, IMPORT-resolve, LLVM-C.dll loader,
        // codegen). std::cerr is unbuffered, but we flush after each
        // marker so an abort()/terminate() in LLVM still leaves a trail.
        auto mark = [](const char* msg) {
            std::cerr << "[jdbasic -c] " << msg << std::endl;
            std::cerr.flush();
        };

        mark(("Compiling: " + filename).c_str());

        // Set base dir for IMPORT module resolution
        {
            size_t sep = filename.find_last_of("/\\");
            g_base_dir = (sep != std::string::npos) ? filename.substr(0, sep) : ".";
        }
        std::string source;
        try { source = read_file(filename); }
        catch (const std::exception& e) {
            std::cerr << "Read error: " << e.what() << std::endl;
            return 1;
        }

        std::vector<StmtPtr> ast;
        try {
            mark("Lexing...");
            Lexer lexer(source);
            auto tokens = lexer.tokenize();
            mark("Parsing...");
            Parser parser(tokens);
            setup_parser_modules(parser);
            ast = parser.parse();
        } catch (const jdError& e) {
            std::cerr << "Parse error";
            if (e.line > 0) std::cerr << " at line " << e.line;
            std::cerr << ": " << e.what() << std::endl;
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "Parse error: " << e.what() << std::endl;
            return 1;
        } catch (...) {
            std::cerr << "Parse error: unknown exception during parse." << std::endl;
            return 1;
        }

        if (compile_output.empty()) {
            compile_output = filename;
            auto dot = compile_output.rfind('.');
            if (dot != std::string::npos) compile_output = compile_output.substr(0, dot);
            compile_output += ".exe";
        }

        try {
            mark("Initializing LLVM codegen...");
            LLVMCodegen codegen;

            // Check if --trace was used
            for (int j = 1; j < argc; j++) {
                if (std::string(argv[j]) == "--trace") { codegen.debug_log = true; break; }
            }

            if (emit_ir_only) {
                mark("Emitting IR...");
                codegen.emit_ir(ast);
                return 0;
            }

            mark("Generating + linking...");
            if (!codegen.compile(ast, compile_output, filename)) {
                std::cerr << "Compilation failed: " << codegen.error_msg << std::endl;
                return 1;
            }
        } catch (const jdError& e) {
            std::cerr << "Codegen error";
            if (e.line > 0) std::cerr << " at line " << e.line;
            std::cerr << ": " << e.what() << std::endl;
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "Codegen error: " << e.what() << std::endl;
            return 1;
        } catch (...) {
            std::cerr << "Codegen error: unknown exception "
                         "(likely an LLVM-C.dll load failure or VC++ "
                         "runtime mismatch — check that vcruntime140.dll, "
                         "msvcp140.dll, and LLVM-C.dll are next to "
                         "jdBasic.exe)." << std::endl;
            return 1;
        }
        // Auto-copy jdbrt.dll next to the produced .exe so the user
        // doesn't have to. Without it the .exe exits silently on
        // Windows when DLL load fails. Source: same dir as jdBasic.exe.
        try {
            std::string self = argv[0];
            size_t s_sep = self.find_last_of("/\\");
            std::string self_dir = (s_sep != std::string::npos)
                ? self.substr(0, s_sep) : ".";
            size_t o_sep = compile_output.find_last_of("/\\");
            std::string out_dir = (o_sep != std::string::npos)
                ? compile_output.substr(0, o_sep) : ".";
            std::filesystem::path src_dll = self_dir + "/jdbrt.dll";
            std::filesystem::path dst_dll = out_dir + "/jdbrt.dll";
            if (std::filesystem::exists(src_dll) &&
                (!std::filesystem::exists(dst_dll) ||
                 !std::filesystem::equivalent(src_dll, dst_dll))) {
                std::filesystem::copy_file(src_dll, dst_dll,
                    std::filesystem::copy_options::overwrite_existing);
            }
        } catch (...) { /* best-effort — do not fail the compile */ }
        std::cout << "Compiled: " << compile_output << std::endl;
        return 0;
#else
        std::cerr << "Native compilation not available (build with NATIVEC flag)." << std::endl;
        return 1;
#endif
    }

    // ── Debug mode ──────────────────────────────────────────────
    // Set base directory for module imports
    {
        size_t sep = filename.find_last_of("/\\");
        g_base_dir = (sep != std::string::npos) ? filename.substr(0, sep) : ".";
    }

    // Resolve filename to absolute path for debugger source mapping
    std::string abs_filename = filename;
    {
        char abs_buf[4096];
#ifdef _WIN32
        DWORD len = GetFullPathNameA(filename.c_str(), sizeof(abs_buf), abs_buf, nullptr);
        if (len > 0 && len < sizeof(abs_buf)) abs_filename = std::string(abs_buf);
#else
        if (realpath(filename.c_str(), abs_buf)) abs_filename = std::string(abs_buf);
#endif
    }

    if (debug_port > 0) {
        VM vm;
        setup_dynamic_code(vm);
        set_os_args(vm, argc, argv);

        // Set up debug info
        vm.debug = std::make_unique<DebugInfo>();
        vm.debug->program_path = filename;
        vm.debug->state = DebugState::PAUSED;

        DAPHandler dap(vm);
        vm.debug->dap = &dap;
        dap.program_path = filename;

        // REPL callback: evaluate input as an expression and return its value.
        // Wraps the input in `LET __REPL_RESULT__ = <expr>` then reads back the global.
        dap.on_repl_eval = [](VM& v, const std::string& code) -> std::string {
            // Trim leading/trailing whitespace
            std::string expr = code;
            while (!expr.empty() && (expr.front() == ' ' || expr.front() == '\t')) expr.erase(0, 1);
            while (!expr.empty() && (expr.back() == ' ' || expr.back() == '\t' ||
                                     expr.back() == '\r' || expr.back() == '\n')) expr.pop_back();
            if (expr.empty()) return "";
            try {
                std::string wrapped = "LET __REPL_RESULT__ = " + expr + "\n";
                Lexer lexer(wrapped);
                auto tokens = lexer.tokenize();
                Parser parser(tokens);
                setup_parser_modules(parser);
                auto ast = parser.parse();
                Compiler compiler;
                compiler.compile(ast);
                v.run_code(compiler.main_chunk(), compiler.functions());

                auto& names = v.get_global_names();
                auto& globals = v.get_globals();
                auto it = names.find("__REPL_RESULT__");
                if (it != names.end() && it->second < globals.size())
                    return globals[it->second].to_string();
                return "NONE";
            } catch (const std::exception& e) {
                return std::string("Error: ") + e.what();
            }
        };

        // Start DAP server
        dap.start(debug_port);

        // Wait for client to send "start"
        {
            std::unique_lock<std::mutex> lock(vm.debug->launch_mtx);
            vm.debug->launch_cv.wait(lock, [&] { return vm.debug->launch_ready; });
        }

        // Compile the program (compiler must stay alive — VM references its data)
        Compiler compiler;
        bool ok = false;
        try {
            std::string source = read_file(filename);
            Lexer lexer(source);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            setup_parser_modules(parser, abs_filename);
            auto ast = parser.parse();
            compiler.compile(ast, abs_filename);
            vm.load(compiler.main_chunk(), compiler.functions());
            ok = true;
        } catch (const std::exception& e) {
            dap.send_output_message(std::string("Compilation error: ") + e.what() + "\n");
        }

        // Signal compilation result to DAP thread
        {
            std::lock_guard<std::mutex> lock(vm.debug->launch_mtx);
            vm.debug->launch_success = ok;
        }
        vm.debug->launch_cv.notify_one();

        if (ok) {
            try {
                vm.run();
            } catch (const jdError& e) {
                dap.send_stopped_message("exception", e.line, filename);
                dap.send_output_message(std::string("Runtime error: ") + e.what() + "\n");
                std::cerr << "\033[91mRuntime error:\033[0m " << e.what();
                if (e.line > 0) std::cerr << " at line " << e.line;
                std::cerr << std::endl;
                vm.debug->state = DebugState::PAUSED;
                vm.debug->pause(); // Let user inspect state
            } catch (const std::exception& e) {
                dap.send_output_message(std::string("Runtime error: ") + e.what() + "\n");
                dap.send_program_ended_message();
                std::cerr << "\033[91mRuntime error:\033[0m " << e.what() << std::endl;
            }
        } else {
            std::cerr << "\033[91mCompilation failed.\033[0m See debug output for details." << std::endl;
        }

#ifdef GFX
        gfx_shutdown();
#endif
        dap.stop();
        std::cerr << std::endl;
#ifdef _WIN32
        system("pause");
#else
        std::cerr << "Press Enter to exit..." << std::endl;
        std::cin.get();
#endif
        return ok ? 0 : 1;
    }

    // ── Normal file execution ───────────────────────────────────
    try {
        std::string source = read_file(filename);
        run_source(source, timing);
    } catch (const jdError& e) {
        print_error(e.code, e.what(), e.line);
        return 1;
    } catch (const std::exception& e) {
        print_error(ErrCode::RUNTIME_ERROR, e.what());
        return 1;
    }

    return 0;
}
