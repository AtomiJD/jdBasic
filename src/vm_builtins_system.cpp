// VM builtins: environment, OS.*, EXECUTE/EVAL, reactive bindings and internal hooks.

#include "vm_internal.h"

void VM::register_system_builtins() {
    // ── System / Environment ─────────────────────────────────

    register_native("GETENV$", [](const std::vector<Value>& args) -> Value {
        const char* val = std::getenv(args[0].as_string()->data.c_str());
        return Value::make_string(val ? val : "");
    });

    register_native("SETENV", 1, 2, [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        std::string val = (args.size() >= 2) ? args[1].as_string()->data : "";
#if defined(_WIN32)
        // SetEnvironmentVariableA with NULL deletes; _putenv_s with "" on
        // Windows updates the CRT view so subsequent getenv sees it.
        _putenv_s(name.c_str(), val.c_str());
        SetEnvironmentVariableA(name.c_str(), args.size() >= 2 ? val.c_str() : NULL);
#else
        if (args.size() >= 2) setenv(name.c_str(), val.c_str(), 1);
        else unsetenv(name.c_str());
#endif
        return Value::make_none();
    });

    register_native("MKTEMP$", 0, 1, [](const std::vector<Value>& args) -> Value {
        // Returns a unique path in the system temp dir; the file is NOT
        // created. Caller is free to open it however they want.
        std::string prefix = (args.size() >= 1) ? args[0].as_string()->data : "jdb";
#if defined(_WIN32)
        char tmp[MAX_PATH];
        DWORD len = GetTempPathA(MAX_PATH, tmp);
        if (len == 0) tmp[0] = '\0';
        char name[MAX_PATH];
        // GetTempFileNameA creates a 0-byte file; delete it so callers can
        // open the path with whatever flags they need (append, binary, …).
        if (GetTempFileNameA(tmp, prefix.c_str(), 0, name)) {
            DeleteFileA(name);
            return Value::make_string(name);
        }
        return Value::make_string("");
#else
        const char* tmp = getenv("TMPDIR");
        if (!tmp) tmp = "/tmp";
        std::string tmpl = std::string(tmp) + "/" + prefix + "XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end()); buf.push_back('\0');
        int fd = mkstemp(buf.data());
        if (fd < 0) return Value::make_string("");
        close(fd); unlink(buf.data());
        return Value::make_string(buf.data());
#endif
    });

    register_native("SETLOCALE", [](const std::vector<Value>& args) -> Value {
        std::string loc_name = args[0].as_string()->data;
        // Windows accepts bare names ("de_DE"); glibc on Linux/macOS
        // requires an encoding suffix and the user-facing name often
        // doesn't carry one. Fall through .UTF-8 / .utf8 if the bare
        // name failed and no encoding was already specified.
        const char* set = std::setlocale(LC_ALL, loc_name.c_str());
        std::string applied = loc_name;
        if (!set && loc_name.find('.') == std::string::npos) {
            applied = loc_name + ".UTF-8";
            set = std::setlocale(LC_ALL, applied.c_str());
            if (!set) {
                applied = loc_name + ".utf8";
                set = std::setlocale(LC_ALL, applied.c_str());
            }
        }
        try {
            g_jd_locale = std::locale(applied.c_str());
            std::cout.imbue(g_jd_locale);
        } catch (const std::runtime_error&) {
            // If the C++ locale fails, keep the previous one
        }
        return Value::make_none();
    });

    register_native("OPTION", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) return Value::make_none();
        std::string opt = args[0].as_string()->data;
        std::transform(opt.begin(), opt.end(), opt.begin(), ::toupper);
        if (opt == "NOCOLOR" || opt == "PLAINTEXT") g_color_errors = false;
        else if (opt == "COLOR") g_color_errors = true;
        else if (opt == "NOAUTOIDENT" || opt == "NOAUTOINDENT") {
            extern bool g_editor_autoindent;
            g_editor_autoindent = false;
        }
        else if (opt == "AUTOIDENT" || opt == "AUTOINDENT") {
            extern bool g_editor_autoindent;
            g_editor_autoindent = true;
        }
        return Value::make_none();
    });

    // ── OS Functions ──────────────────────────────────────────

    auto getos_fn = [](const std::vector<Value>& args) -> Value {
        (void)args;
#if defined(__EMSCRIPTEN__)
        return Value::make_string("WEB");
#elif defined(_WIN32)
        return Value::make_string("WINDOWS");
#elif defined(__APPLE__)
        return Value::make_string("MACOS");
#elif defined(FRUITJAM)
        return Value::make_string("FRUITJAM");
#elif defined(PICOCALC)
        return Value::make_string("PICOCALC");
#elif defined(ESP32)
        return Value::make_string("ESP32");
#elif defined(JDB_MCU)
        return Value::make_string("PICO");
#else
        return Value::make_string("LINUX");
#endif
    };
    register_native("OS.GETOS", getos_fn);
    register_native("OS.GETOS$", getos_fn);

    register_native("OS.ARGS", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        // Populated by main() via set_global
        return Value::make_array(); // fallback; main sets the real value
    });

#ifndef JDB_LEAN
    register_native("OS.EXEC", [](const std::vector<Value>& args) -> Value {
        std::string cmd = args[0].as_string()->data;
        // A program path or an argument holding spaces is quoted, unless the
        // caller quoted it already; a command line such as "git status" is
        // not a file and stays as it is.
        auto quoted = [](const std::string& s) {
            if (s.find(' ') == std::string::npos || (!s.empty() && s.front() == '"')) return s;
            return "\"" + s + "\"";
        };
        if (cmd.find(' ') != std::string::npos && std::ifstream(cmd).good())
            cmd = quoted(cmd);
        if (args.size() >= 2 && args[1].type == ValueType::ARRAY) {
            for (auto& a : args[1].as_array()->elements)
                cmd += " " + (a.type == ValueType::STRING ? quoted(a.to_string()) : a.to_string());
        }
        // Execute and capture output
        std::string output;
        int exit_code = -1;
#if defined(_WIN32)
        // The outer quotes are what cmd /c strips, so quotes inside survive.
        cmd = "cmd /c \"" + cmd + " 2>&1\"";
#else
        cmd = cmd + " 2>&1";
#endif
#if defined(_WIN32)
        FILE* pipe = _popen(cmd.c_str(), "r");
#else
        FILE* pipe = popen(cmd.c_str(), "r");
#endif
        if (pipe) {
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe)) output += buf;
#if defined(_WIN32)
            exit_code = _pclose(pipe);
#else
            exit_code = pclose(pipe);
#endif
        }
        Value result = Value::make_object();
        result.as_object()->set("OUTPUT", Value::make_string(output));
        result.as_object()->set("EXIT_CODE", Value::make_i64(exit_code));
        return result;
    });
#endif

#ifndef JDB_LEAN
    register_native("OS.HOSTNAME$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        char buf[256] = {};
#if defined(_WIN32)
        DWORD sz = sizeof(buf);
        GetComputerNameA(buf, &sz);
#else
        gethostname(buf, sizeof(buf));
#endif
        return Value::make_string(buf);
    });
#endif

#ifndef JDB_LEAN
    register_native("OS.IP$", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::string ip = "127.0.0.1";
#if defined(_WIN32)
        ULONG buf_size = 15000;
        PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(buf_size);
        if (pAddresses) {
            DWORD ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &buf_size);
            if (ret == ERROR_BUFFER_OVERFLOW) {
                free(pAddresses);
                pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(buf_size);
                if (pAddresses) ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &buf_size);
            }
            if (ret == NO_ERROR) {
                for (auto pAddr = pAddresses; pAddr; pAddr = pAddr->Next) {
                    // Skip loopback and down interfaces
                    if (pAddr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                    if (pAddr->OperStatus != IfOperStatusUp) continue;
                    for (auto pUni = pAddr->FirstUnicastAddress; pUni; pUni = pUni->Next) {
                        auto sa = (struct sockaddr_in*)pUni->Address.lpSockaddr;
                        char buf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                        std::string candidate(buf);
                        if (candidate != "127.0.0.1" && candidate.substr(0,4) != "169.") {
                            ip = candidate;
                            goto found;
                        }
                    }
                }
                found:;
            }
            free(pAddresses);
        }
#endif
        return Value::make_string(ip);
    });
#endif

    register_native("OS.FEATURE", 1, 1, [](const std::vector<Value>& args) -> Value {
        // Reports whether the running binary advertises a given build feature.
        // Used by tests/programs that need to skip work the current backend
        // doesn't support - e.g. `IF NOT OS.FEATURE("NATIVEC") THEN ...`.
        if (args.empty() || args[0].type != ValueType::STRING)
            return Value::make_bool(false);
        std::string name = args[0].as_string()->data;
        for (auto& c : name) c = (char)std::toupper((unsigned char)c);

        // Interpreter never reports NATIVEC. INTERPRETER is its inverse.
        if (name == "NATIVEC")     return Value::make_bool(false);
        if (name == "INTERPRETER") return Value::make_bool(true);

        // Optional build flags - true iff this jdBasic.exe was built with them.
        bool on = false;
#ifdef COM
        if (name == "COM") on = true;
#endif
#ifdef HTTP
        if (name == "HTTP") on = true;
#endif
#ifdef NET
        if (name == "NET") on = true;
#endif
#ifdef USE_SERIAL
        if (name == "SERIAL") on = true;
#endif
#ifdef GFX
        if (name == "GFX") on = true;
#endif
#ifdef IMGUI
        if (name == "IMGUI") on = true;
#endif
#ifdef LLM
        if (name == "LLM") on = true;
#endif
#ifdef ONNX
        if (name == "ONNX") on = true;
#endif
#ifdef SQLITE
        if (name == "SQLITE") on = true;
#endif
#ifdef PYTHON
        if (name == "PYTHON") on = true;
#endif
#ifdef LLVM_CODEGEN
        if (name == "LLVMC") on = true;  // compiler available (--compile)
#endif
        return Value::make_bool(on);
    });

#ifndef JDB_LEAN
    register_native("OS.SCREENSHOT", 1, 3, [](const std::vector<Value>& args) -> Value {
        // OS.SCREENSHOT(path$ [, mode$] [, caption$]) -> int rc (0 = ok).
        //   mode$    : "screen" (default) | "window" (frame) | "client" (content)
        //   caption$ : optional FindWindow title; empty => foreground window
        // The image format (.png / .jpg / .bmp / .tif / .gif) is taken from the
        // path's extension. Windows-only; returns -100 on other platforms.
        if (args.empty() || args[0].type != ValueType::STRING)
            return Value::make_i64(-1);
        std::string path = args[0].as_string()->data;
        std::string mode = (args.size() >= 2 && args[1].type == ValueType::STRING)
                           ? args[1].as_string()->data : std::string();
        std::string cap  = (args.size() >= 3 && args[2].type == ValueType::STRING)
                           ? args[2].as_string()->data : std::string();
        int rc = jdb_screencap(path.c_str(),
                               mode.empty() ? nullptr : mode.c_str(),
                               cap.empty()  ? nullptr : cap.c_str());
        return Value::make_i64(rc);
    });
#endif

#ifndef JDB_LEAN
    register_native("OS.LOAD", 0, -1, [](const std::vector<Value>& args) -> Value {
        (void)args;
        double load = 0.0;
#if defined(_WIN32)
        // Use a quick WMI/perf counter approach via wmic
        FILE* pipe = _popen("wmic cpu get LoadPercentage /value 2>nul", "r");
        if (pipe) {
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe)) {
                std::string line(buf);
                if (line.find("LoadPercentage=") != std::string::npos) {
                    try { load = std::stod(line.substr(line.find('=') + 1)); } catch (...) {}
                }
            }
            _pclose(pipe);
        }
#endif
        return Value::make_f64(load);
    });
#endif

    // ── Reactive system ───────────────────────────────────────

    register_native("REACT_BIND", [this](const std::vector<Value>& args) -> Value {
        std::string var = args[0].as_string()->data;
        std::string formula = args[1].as_string()->data;
        std::string func = args[2].as_string()->data;
        std::vector<std::string> deps;
        if (args[3].type == ValueType::ARRAY)
            for (auto& d : args[3].as_array()->elements)
                deps.push_back(d.as_string()->data);
        bind_reactive(var, formula, func, deps);
        return Value::make_none();
    });

    register_native("UNREACT", [this](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        if (name == "ALL" || name == "*") {
            reactive_bindings.clear();
        } else {
            reactive_bindings.erase(name);
        }
        return Value::make_none();
    });

    register_native("TRON", 0, 0, [this](const std::vector<Value>& args) -> Value {
        (void)args;
        trace_enabled = true;
        emit("Trace ON\n");
        return Value::make_none();
    });

    register_native("TROFF", 0, 0, [this](const std::vector<Value>& args) -> Value {
        (void)args;
        trace_enabled = false;
        emit("Trace OFF\n");
        return Value::make_none();
    });

    register_native("DUMP", 0, 1, [this](const std::vector<Value>& args) -> Value {
        std::string what = args.empty() ? std::string("GLOBAL") : args[0].to_string();
        std::transform(what.begin(), what.end(), what.begin(), ::toupper);

        if (what == "REACT") {
            if (reactive_bindings.empty()) {
                emit("No reactive bindings.\n");
            } else {
                emit("--- Reactive Graph (" + std::to_string(reactive_bindings.size()) + " bindings) ---\n");
                for (auto& [name, b] : reactive_bindings) {
                    std::string line = "  " + name + " -> " + b.formula + "  [deps: ";
                    for (size_t i = 0; i < b.dependencies.size(); i++) {
                        if (i > 0) line += ", ";
                        line += b.dependencies[i];
                    }
                    line += "]\n";
                    emit(line);
                }
            }
        } else if (what == "STACK") {
            emit("--- Call Stack (" + std::to_string(frames.size()) + " frames) ---\n");
            for (size_t i = 0; i < frames.size(); i++) {
                int line = 0;
                if (frames[i].chunk && frames[i].ip > 0)
                    line = frames[i].chunk->line_at(frames[i].ip - 1);
                emit("  #" + std::to_string(i) + " line " + std::to_string(line) + "\n");
            }
        } else if (what == "FUNCS" || what == "FUNCTIONS") {
            emit("--- Functions (" + std::to_string(owned_funcs.size()) + ") ---\n");
            for (auto& f : owned_funcs)
                emit("  " + f.name + " (" + std::to_string(f.arity) + " params)\n");
        } else { // GLOBAL or VARS (default)
            emit("--- Global Variables (" + std::to_string(global_names.size()) + ") ---\n");
            for (auto& [name, slot] : global_names) {
                if (slot < globals.size() && name.substr(0, 2) != "__")
                    emit("  " + name + " = " + globals[slot].to_string() + "\n");
            }
        }
        return Value::make_none();
    });

    // ── Dynamic code ─────────────────────────────────────────

    register_native("EXECUTE", [this](const std::vector<Value>& args) -> Value {
        std::string code = args[0].as_string()->data;
        if (on_execute) on_execute(*this, code);
        return Value::make_none();
    });

    register_native("EVAL", [this](const std::vector<Value>& args) -> Value {
        std::string expr_str = args[0].as_string()->data;
        if (on_eval) return on_eval(*this, expr_str);
        return Value::make_none();
    });

    // JDB.CHECK$(code) - Lex + Parse only. Returns "" if the code is
    // syntactically valid, or the error message otherwise. No execution
    // happens, no globals or functions get registered. Useful for the
    // MCP jdb_check tool: validate a snippet before EXECUTE'ing it on
    // the persistent VM.
#ifndef JDB_LEAN
    register_native("JDB.CHECK$", 1, 1, [this](const std::vector<Value>& args) -> Value {
        std::string code = args[0].as_string()->data;
        if (on_check) return Value::make_string(on_check(*this, code));
        return Value::make_string("JDB.CHECK$ unavailable: host did not register on_check");
    });
#endif

    // JDB.GLOBAL_GET(name$) - read a single global by (case-insensitive)
    // name. Returns NONE if the name is unknown. Companion to set_global.
    // Used by the MCP jdb_save_state tool to capture a value-copy of each
    // user variable before an experimental snippet runs.
    register_native("JDB.GLOBAL_GET", 1, 1, [this](const std::vector<Value>& args) -> Value {
        std::string nm = args[0].as_string()->data;
        for (auto& c : nm) c = (char)std::toupper((unsigned char)c);
        auto it = global_names.find(nm);
        if (it == global_names.end() || it->second >= globals.size())
            return Value::make_none();
        return globals[it->second];
    });

    // JDB.GLOBAL_SET(name$, value) - write a single global by name.
    // Allocates the slot if the name is new. Companion to GLOBAL_GET;
    // jdb_restore_state writes saved values back into place via this.
    register_native("JDB.GLOBAL_SET", 2, 2, [this](const std::vector<Value>& args) -> Value {
        std::string nm = args[0].as_string()->data;
        for (auto& c : nm) c = (char)std::toupper((unsigned char)c);
        set_global(nm, args[1]);
        return Value::make_none();
    });

    // ── Pipe apply (internal: calls funcref with value) ─────
    register_native("__PIPE_APPLY", 2, 2, [this](const std::vector<Value>& args) -> Value {
        return call_funcref(args[0], {args[1]});
    });

    // ── Event system ─────────────────────────────────────────

    register_native("__EVENT_ON", [this](const std::vector<Value>& args) -> Value {
        std::string event_name = args[0].as_string()->data;
        std::string handler = args[1].as_string()->data;
        event_on(event_name, handler);
        return Value::make_none();
    });

    register_native("__EVENT_RAISE", [this](const std::vector<Value>& args) -> Value {
        std::string event_name = args[0].as_string()->data;
        std::vector<Value> data;
        for (size_t i = 1; i < args.size(); i++) data.push_back(args[i]);
        event_raise(event_name, data);
        return Value::make_none();
    });
}
