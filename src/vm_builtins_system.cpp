// VM builtins: environment, OS.* (child processes in os_spawn.h), EXECUTE/EVAL, reactive bindings and internal hooks.

#include "vm_internal.h"
#include "os_spawn.h"
#include <fstream>
#include <mutex>
#include <thread>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

#if defined(_WIN32)
// The output of a console program as UTF-8: text that is valid UTF-8 stays
// as it is, anything else is read in the OEM codepage that Windows console
// tools such as schtasks write.
static std::string console_text_to_utf8(const std::string& s) {
    size_t i = 0;
    bool valid = true;
    while (i < s.size() && valid) {
        unsigned char c = s[i];
        size_t need = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 9;
        if (need == 9 || (need > 0 && i + need >= s.size())) { valid = false; break; }
        for (size_t k = 1; k <= need; k++)
            if (((unsigned char)s[i + k] >> 6) != 0x2) { valid = false; break; }
        i += need + 1;
    }
    if (valid) return s;
    // The system locale's OEM codepage; CP_OEMCP is UTF-8 under our manifest.
    DWORD oem = 0;
    if (GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTCODEPAGE | LOCALE_RETURN_NUMBER,
                       reinterpret_cast<LPWSTR>(&oem), sizeof(oem) / sizeof(WCHAR)) == 0 || oem == 0)
        oem = 850;
    int wlen = MultiByteToWideChar(oem, 0, s.data(), (int)s.size(), nullptr, 0);
    if (wlen <= 0) return s;
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(oem, 0, s.data(), (int)s.size(), &w[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::string out(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, &out[0], ulen, nullptr, nullptr);
    return out;
}
#endif

#ifndef JDB_LEAN
// Idle and total CPU time of the whole machine, in the platform's own units
static bool cpu_times(uint64_t& idle, uint64_t& total) {
#if defined(_WIN32)
    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi, &fk, &fu)) return false;
    auto u64 = [](const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    idle = u64(fi);
    total = u64(fk) + u64(fu);   // kernel time includes the idle time
    return true;
#elif defined(__APPLE__)
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&info, &n) != KERN_SUCCESS)
        return false;
    idle = info.cpu_ticks[CPU_STATE_IDLE];
    total = 0;
    for (int i = 0; i < CPU_STATE_MAX; i++) total += info.cpu_ticks[i];
    return true;
#elif defined(__linux__)
    std::ifstream f("/proc/stat");
    std::string cpu;
    uint64_t v[8] = {0};
    if (!(f >> cpu) || cpu != "cpu") return false;
    for (int i = 0; i < 8; i++) f >> v[i];   // user nice system idle iowait irq softirq steal
    idle = v[3] + v[4];
    total = 0;
    for (int i = 0; i < 8; i++) total += v[i];
    return true;
#else
    (void)idle; (void)total;
    return false;
#endif
}

// Physical memory: all of it, and what programs could still have without
// the system swapping
static bool memory_bytes(uint64_t& total, uint64_t& avail) {
#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return false;
    total = ms.ullTotalPhys;
    avail = ms.ullAvailPhys;
    return true;
#elif defined(__APPLE__)
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) != 0) return false;
    vm_statistics64_data_t vs;
    mach_msg_type_number_t n = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vs, &n) != KERN_SUCCESS)
        return false;
    total = mem;
    avail = ((uint64_t)vs.free_count + vs.inactive_count + vs.purgeable_count) * (uint64_t)vm_kernel_page_size;
    return true;
#elif defined(__linux__)
    std::ifstream f("/proc/meminfo");
    std::string key, unit;
    uint64_t kb = 0;
    bool got_total = false, got_avail = false;
    while (f >> key >> kb >> unit) {
        if (key == "MemTotal:") { total = kb * 1024; got_total = true; }
        else if (key == "MemAvailable:") { avail = kb * 1024; got_avail = true; }
        if (got_total && got_avail) return true;
    }
    return false;
#else
    (void)total; (void)avail;
    return false;
#endif
}
#endif

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
        // Numbers keep reading with a point: VAL, CSV, JSON and source text
        // parse through strtod, which follows LC_NUMERIC. The locale's own
        // punctuation reaches the output through g_jd_punct below.
        std::setlocale(LC_NUMERIC, "C");
        try {
            g_jd_locale = std::locale(applied.c_str());
            g_jd_punct = jdb_num::punct_of(g_jd_locale);
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
        std::string output;
        int exit_code = -1;
        // With an argument array the program runs without a shell, so no
        // argument can be read as an operator (&, |, >, ;, $ ...).
        if (args.size() >= 2 && args[1].type == ValueType::ARRAY) {
#if defined(_WIN32)
            // MSVCRT command line quoting: backslashes before a quote are
            // doubled and the quote escaped, the whole argument quoted.
            auto argv_quote = [](const std::string& s, bool always) {
                if (!always && !s.empty() && s.find_first_of(" \t\n\v\"") == std::string::npos) return s;
                std::string out = "\"";
                size_t slashes = 0;
                for (char c : s) {
                    if (c == '\\') { slashes++; continue; }
                    if (c == '"') out.append(slashes * 2 + 1, '\\');
                    else out.append(slashes, '\\');
                    slashes = 0;
                    out.push_back(c);
                }
                out.append(slashes * 2, '\\');
                out.push_back('"');
                return out;
            };
            std::string line = cmd;
            std::string all_quoted = cmd;
            bool batch_safe = true;
            for (auto& a : args[1].as_array()->elements) {
                std::string s = a.to_string();
                line += " " + argv_quote(s, false);
                all_quoted += " " + argv_quote(s, true);
                if (s.find_first_of("%\"\r\n") != std::string::npos) batch_safe = false;
            }
            std::string low = cmd;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            while (!low.empty() && low.back() == '"') low.pop_back();
            bool is_batch = low.size() > 4 && (low.compare(low.size() - 4, 4, ".bat") == 0 || low.compare(low.size() - 4, 4, ".cmd") == 0);
            SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
            HANDLE rd = nullptr, wr = nullptr;
            if (!CreatePipe(&rd, &wr, &sa, 0)) throw std::runtime_error("OS.EXEC: cannot create a pipe");
            SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = wr;
            si.hStdError = wr;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            // Only this child's own pipe (and an inheritable stdin) is handed
            // down: a child another thread starts meanwhile must not inherit
            // the write end too, or this read waits for that child as well.
            HANDLE inherit[2];
            DWORD ninherit = 0;
            inherit[ninherit++] = wr;
            DWORD in_flags = 0;
            if (si.hStdInput && si.hStdInput != INVALID_HANDLE_VALUE &&
                GetHandleInformation(si.hStdInput, &in_flags) && (in_flags & HANDLE_FLAG_INHERIT))
                inherit[ninherit++] = si.hStdInput;
            SIZE_T attr_size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
            std::vector<char> attr_buf(attr_size);
            auto attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)attr_buf.data();
            bool have_list = attr_size > 0 &&
                InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
                UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          inherit, ninherit * sizeof(HANDLE), nullptr, nullptr);
            STARTUPINFOEXA six = {};
            six.StartupInfo = si;
            six.StartupInfo.cb = sizeof(six);
            six.lpAttributeList = have_list ? attrs : nullptr;
            LPSTARTUPINFOA start_info = have_list ? &six.StartupInfo : &si;
            DWORD create_flags = CREATE_NO_WINDOW | (have_list ? EXTENDED_STARTUPINFO_PRESENT : 0);
            PROCESS_INFORMATION pi = {};
            BOOL ok = FALSE;
            if (!is_batch) {
                std::vector<char> buf_line(line.begin(), line.end());
                buf_line.push_back('\0');
                ok = CreateProcessA(nullptr, buf_line.data(), nullptr, nullptr, TRUE, create_flags, nullptr, nullptr, start_info, &pi);
            }
            if (!ok && (is_batch || GetLastError() == ERROR_FILE_NOT_FOUND)) {
                // A batch file (.cmd, .bat, or a name such as npm that is no
                // .exe) runs under cmd, which reads % and " even inside
                // quotes; every argument is quoted and those characters are
                // refused.
                if (!batch_safe) {
                    if (have_list) DeleteProcThreadAttributeList(attrs);
                    CloseHandle(wr);
                    CloseHandle(rd);
                    throw std::runtime_error("OS.EXEC: an argument with % or a double quote cannot be passed to the batch file " + cmd + " safely");
                }
                std::string via_cmd = "cmd /d /s /c \"" + all_quoted + "\"";
                std::vector<char> buf_cmd(via_cmd.begin(), via_cmd.end());
                buf_cmd.push_back('\0');
                ok = CreateProcessA(nullptr, buf_cmd.data(), nullptr, nullptr, TRUE, create_flags, nullptr, nullptr, start_info, &pi);
            }
            if (have_list) DeleteProcThreadAttributeList(attrs);
            CloseHandle(wr);
            if (ok) {
                char buf[4096];
                DWORD got = 0;
                while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) output.append(buf, got);
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD code = 0;
                GetExitCodeProcess(pi.hProcess, &code);
                exit_code = (int)code;
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                output = "cannot start " + cmd;
            }
            CloseHandle(rd);
            Value result = Value::make_object();
            result.as_object()->set("OUTPUT", Value::make_string(console_text_to_utf8(output)));
            result.as_object()->set("EXIT_CODE", Value::make_i64(exit_code));
            return result;
#else
            auto sh_quote = [](const std::string& s) {
                std::string out = "'";
                for (char c : s) {
                    if (c == '\'') out += "'\\''";
                    else out.push_back(c);
                }
                return out + "'";
            };
            for (auto& a : args[1].as_array()->elements) cmd += " " + sh_quote(a.to_string());
#endif
        }
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
            // fread, not fgets: binary output keeps its NUL bytes.
            char buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), pipe)) > 0) output.append(buf, got);
#if defined(_WIN32)
            exit_code = _pclose(pipe);
#else
            // pclose answers the wait status: the exit code is inside it, and
            // a signal becomes 128 + its number as in a shell.
            int status = pclose(pipe);
            if (status == -1) exit_code = -1;
            else if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);
            else exit_code = status;
#endif
        }
        Value result = Value::make_object();
#if defined(_WIN32)
        output = console_text_to_utf8(output);
#endif
        result.as_object()->set("OUTPUT", Value::make_string(output));
        result.as_object()->set("EXIT_CODE", Value::make_i64(exit_code));
        return result;
    });
#endif

#if !defined(JDB_LEAN) && !defined(__EMSCRIPTEN__)
    // OS.SPAWN(command$, [args_array$], [options]) -> pid. Starts and returns
    // at once. With an argument array (even an empty one) the program runs
    // without a shell; without one, command$ is a line for cmd / sh.
    register_native("OS.SPAWN", 1, 3, [](const std::vector<Value>& args) -> Value {
        if (args[0].type != ValueType::STRING || args[0].as_string()->data.empty())
            throw std::runtime_error("OS.SPAWN: expects a program or command line");
        std::vector<std::string> argv{args[0].as_string()->data};
        bool use_shell = true;
        if (args.size() >= 2 && args[1].type == ValueType::ARRAY) {
            use_shell = false;
            for (auto& a : args[1].as_array()->elements) argv.push_back(a.to_string());
        } else if (args.size() >= 2 && args[1].type != ValueType::NONE) {
            throw std::runtime_error("OS.SPAWN: the arguments must be an array of strings");
        }
        os_spawn::Options opts;
        if (args.size() >= 3 && args[2].type == ValueType::OBJECT) {
            for (auto& f : args[2].as_object()->fields) {
                std::string key = f.first;
                for (char& c : key) c = (char)std::toupper((unsigned char)c);
                if (key == "CWD") opts.cwd = f.second.to_string();
                else if (key == "OUTPUT") opts.output = f.second.to_string();
                else if (key == "HIDDEN") opts.hidden = f.second.to_bool();
                else if (key == "ENV") {
                    if (f.second.type != ValueType::OBJECT)
                        throw std::runtime_error("OS.SPAWN: ENV must be a map of names to values");
                    for (auto& e : f.second.as_object()->fields)
                        opts.env.emplace_back(e.first, e.second.to_string());
                } else {
                    throw std::runtime_error("OS.SPAWN: unknown option " + f.first + " (CWD, ENV, OUTPUT, HIDDEN)");
                }
            }
        } else if (args.size() >= 3 && args[2].type != ValueType::NONE) {
            throw std::runtime_error("OS.SPAWN: the options must be a map");
        }
        return Value::make_i64(os_spawn::start(argv, use_shell, opts));
    });

    // OS.WAIT(pid, [timeout_ms]) -> exit code, or NONE when the child still
    // runs after timeout_ms. Only for processes OS.SPAWN started.
    register_native("OS.WAIT", 1, 2, [](const std::vector<Value>& args) -> Value {
        int64_t pid = args[0].to_int();
        int64_t timeout_ms = args.size() >= 2 ? args[1].to_int() : -1;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(os_spawn::table_mutex());
                auto it = os_spawn::table().find(pid);
                if (it == os_spawn::table().end())
                    throw std::runtime_error("OS.WAIT: process " + std::to_string(pid) + " was not started by OS.SPAWN");
                if (os_spawn::poll_locked(pid, it->second, 0)) return Value::make_i64(it->second.exit_code);
            }
            if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= deadline) return Value::make_none();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // OS.ALIVE(pid) -> BOOLEAN, for any process.
    register_native("OS.ALIVE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t pid = args[0].to_int();
        {
            std::lock_guard<std::mutex> lock(os_spawn::table_mutex());
            auto it = os_spawn::table().find(pid);
            if (it != os_spawn::table().end())
                return Value::make_bool(!os_spawn::poll_locked(pid, it->second, 0));
        }
        if (pid <= 0) return Value::make_bool(false);
#if defined(_WIN32)
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
        if (!h) return Value::make_bool(GetLastError() == ERROR_ACCESS_DENIED);
        bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
        CloseHandle(h);
        return Value::make_bool(alive);
#else
        return Value::make_bool(kill((pid_t)pid, 0) == 0 || errno == EPERM);
#endif
    });

    // OS.KILL(pid) -> BOOLEAN. Ends the process at once (TerminateProcess,
    // SIGKILL); FALSE when it was gone or may not be ended.
    register_native("OS.KILL", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t pid = args[0].to_int();
        if (pid <= 0 || pid == os_spawn::self_pid()) return Value::make_bool(false);
#if defined(_WIN32)
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (!h) return Value::make_bool(false);
        bool ok = TerminateProcess(h, 137) != 0;
        CloseHandle(h);
        return Value::make_bool(ok);
#else
        return Value::make_bool(kill((pid_t)pid, SIGKILL) == 0);
#endif
    });

    register_native("OS.PID", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(os_spawn::self_pid());
    });

    register_native("OS.SELF$", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_string(os_spawn::self_path());
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
#ifdef FORMS
        if (name == "FORMS") on = true;
#endif
#ifdef OPENGL
        if (name == "OPENGL") on = true;
#endif
#ifdef MINIAUDIO
        if (name == "MINIAUDIO") on = true;
#endif
#if defined(GFX) || defined(SOUND_DSP)
        if (name == "SOUND") on = true;  // the SOUND.* synth and sequencer
#endif
#ifdef FX
        if (name == "FX") on = true;
#endif
#ifdef MIDI
        if (name == "MIDI") on = true;
#endif
#ifdef MCPSERVER
        if (name == "MCPSERVER") on = true;  // --mcp speaks over stdio
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
    // The load between two readings of the system's CPU time counters: since
    // the previous call, or over a tenth of a second for the first one.
    register_native("OS.LOAD", 0, 0, [](const std::vector<Value>&) -> Value {
        static std::mutex mu;
        static uint64_t prev_idle = 0, prev_total = 0;
        static bool have_prev = false;
        std::lock_guard<std::mutex> lock(mu);
        uint64_t idle = 0, total = 0;
        if (!cpu_times(idle, total)) return Value::make_f64(0.0);
        if (!have_prev) {
            prev_idle = idle;
            prev_total = total;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!cpu_times(idle, total)) return Value::make_f64(0.0);
        }
        uint64_t d_total = total - prev_total;
        uint64_t d_idle = idle - prev_idle;
        prev_idle = idle;
        prev_total = total;
        have_prev = true;
        if (d_total == 0) return Value::make_f64(0.0);
        double load = 100.0 * (double)(d_total - std::min(d_idle, d_total)) / (double)d_total;
        return Value::make_f64(load);
    });

    register_native("SYS.MEMORY", 0, 0, [](const std::vector<Value>&) -> Value {
        uint64_t total = 0, avail = 0;
        if (!memory_bytes(total, avail))
            throw std::runtime_error("SYS.MEMORY: this platform cannot report its memory");
        Value m = Value::make_object();
        m.as_object()->set("TOTAL", Value::make_i64((int64_t)total));
        m.as_object()->set("FREE", Value::make_i64((int64_t)avail));
        return m;
    });
#endif

    // ── Reactive system ───────────────────────────────────────

    register_native("REACT_BIND", 4, 4, [this](const std::vector<Value>& args) -> Value {
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
                if (!f.name.empty()) emit("  " + f.name + " (" + std::to_string(f.arity) + " params)\n");
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

    register_native("__EVENT_ON", 2, 2, [this](const std::vector<Value>& args) -> Value {
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
