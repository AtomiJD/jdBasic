#pragma once
// Child processes for OS.SPAWN / OS.WAIT / OS.KILL / OS.ALIVE, and the path
// and pid of the running program for OS.SELF$ / OS.PID. Included by
// vm_builtins_system.cpp only, after vm_internal.h has pulled in the
// platform headers.

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace os_spawn {

struct Options {
    std::string cwd;
    std::vector<std::pair<std::string, std::string>> env;  // added or replaced
    std::string output;   // stdout and stderr appended to this file when set
    bool hidden = false;  // no console window (Windows)
};

struct Child {
#if defined(_WIN32)
    HANDLE process = nullptr;
#endif
    bool finished = false;
    int exit_code = 0;
};

inline std::mutex& table_mutex() {
    static std::mutex m;
    return m;
}

inline std::map<int64_t, Child>& table() {
    static std::map<int64_t, Child> t;
    return t;
}

inline int64_t self_pid() {
#if defined(_WIN32)
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

inline std::string self_path() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
        if (n == 0) return "";
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, buf.data(), (int)buf.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf.data(), (int)buf.size(), &out[0], len, nullptr, nullptr);
    return out;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(&buf[0], &size) != 0) return "";
    char resolved[PATH_MAX];
    if (realpath(buf.c_str(), resolved)) return resolved;
    return std::string(buf.c_str());
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    return buf;
#endif
}

#if defined(_WIN32)
// MSVCRT command line quoting, the rule CommandLineToArgvW and the C runtime
// read arguments back with.
inline std::string argv_quote(const std::string& s) {
    if (!s.empty() && s.find_first_of(" \t\n\v\"") == std::string::npos) return s;
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
}

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

inline bool same_name(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

// The current environment with opts.env applied, as a CreateProcessW block.
inline std::vector<wchar_t> env_block(const Options& opts) {
    std::vector<std::wstring> entries;
    if (wchar_t* cur = GetEnvironmentStringsW()) {
        for (wchar_t* p = cur; *p; p += wcslen(p) + 1) entries.emplace_back(p);
        FreeEnvironmentStringsW(cur);
    }
    for (const auto& kv : opts.env) {
        std::wstring key = widen(kv.first);
        std::wstring entry = key + L"=" + widen(kv.second);
        bool replaced = false;
        for (auto& e : entries) {
            size_t eq = e.find(L'=', 1);
            if (eq != std::wstring::npos && same_name(e.substr(0, eq), key)) {
                e = entry;
                replaced = true;
            }
        }
        if (!replaced) entries.push_back(entry);
    }
    std::vector<wchar_t> block;
    for (const auto& e : entries) {
        block.insert(block.end(), e.begin(), e.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
#else
// The program file execve runs: a name without a slash is looked up in PATH.
inline std::string find_program(const std::string& prog) {
    if (prog.find('/') != std::string::npos) return prog;
    const char* path = getenv("PATH");
    std::string dirs = path ? path : "/usr/bin:/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        size_t colon = dirs.find(':', start);
        std::string dir = dirs.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (dir.empty()) dir = ".";
        std::string cand = dir + "/" + prog;
        if (access(cand.c_str(), X_OK) == 0) return cand;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return prog;
}
#endif

// Starts the program and returns its pid. argv[0] is the program; with
// use_shell the single string in argv[0] is a command line for the shell.
inline int64_t start(const std::vector<std::string>& argv, bool use_shell, const Options& opts) {
#if defined(_WIN32)
    std::string line;
    if (use_shell) {
        line = "cmd /d /s /c \"" + argv[0] + "\"";
    } else {
        for (size_t i = 0; i < argv.size(); ++i) {
            if (i) line += " ";
            line += argv_quote(argv[i]);
        }
    }
    std::wstring wline = widen(line);
    std::vector<wchar_t> cmd(wline.begin(), wline.end());
    cmd.push_back(L'\0');
    std::vector<wchar_t> env = env_block(opts);
    std::wstring wcwd = widen(opts.cwd);

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    HANDLE out = nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
    std::vector<char> attr_buf;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    if (opts.hidden) flags |= CREATE_NO_WINDOW;
    if (!opts.output.empty()) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        // Write access positioned at the end rather than an append-only
        // handle, which MSYS programs such as Git's bash cannot write to
        out = CreateFileW(widen(opts.output).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE)
            throw std::runtime_error("OS.SPAWN: cannot open the output file " + opts.output);
        LARGE_INTEGER zero = {};
        SetFilePointerEx(out, zero, nullptr, FILE_END);
        // Only the output handle is inherited, never the sockets and files
        // the program holds.
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attr_buf.resize(size);
        attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)attr_buf.data();
        InitializeProcThreadAttributeList(attrs, 1, 0, &size);
        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &out, sizeof(out), nullptr, nullptr);
        si.lpAttributeList = attrs;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdOutput = out;
        si.StartupInfo.hStdError = out;
        si.StartupInfo.hStdInput = nullptr;
        flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, out ? TRUE : FALSE, flags, env.data(),
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si.StartupInfo, &pi);
    DWORD err = GetLastError();
    if (attrs) DeleteProcThreadAttributeList(attrs);
    if (out) CloseHandle(out);
    if (!ok && err == ERROR_DIRECTORY)
        throw std::runtime_error("OS.SPAWN: cannot change to the directory " + opts.cwd);
    if (!ok)
        throw std::runtime_error("OS.SPAWN: cannot start " + argv[0] + " (Windows error " + std::to_string(err) + ")");
    CloseHandle(pi.hThread);
    std::lock_guard<std::mutex> lock(table_mutex());
    Child c;
    c.process = pi.hProcess;
    table()[(int64_t)pi.dwProcessId] = c;
    return (int64_t)pi.dwProcessId;
#else
    std::vector<std::string> args = use_shell ? std::vector<std::string>{"/bin/sh", "-c", argv[0]} : argv;
    std::string prog = use_shell ? "/bin/sh" : find_program(argv[0]);
    std::vector<char*> cargv;
    for (auto& a : args) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    std::vector<std::string> env_strs;
    for (char** e = environ; e && *e; ++e) {
        std::string entry = *e;
        size_t eq = entry.find('=');
        bool replaced = false;
        for (const auto& kv : opts.env)
            if (eq != std::string::npos && entry.compare(0, eq, kv.first) == 0 && eq == kv.first.size()) replaced = true;
        if (!replaced) env_strs.push_back(entry);
    }
    for (const auto& kv : opts.env) env_strs.push_back(kv.first + "=" + kv.second);
    std::vector<char*> cenv;
    for (auto& e : env_strs) cenv.push_back(const_cast<char*>(e.c_str()));
    cenv.push_back(nullptr);

    int out = -1;
    if (!opts.output.empty()) {
        out = open(opts.output.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (out < 0) throw std::runtime_error("OS.SPAWN: cannot open the output file " + opts.output);
    }
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0 || max_fd > 65536) max_fd = 65536;
    int fail_pipe[2];
    if (pipe(fail_pipe) != 0) throw std::runtime_error("OS.SPAWN: cannot create a pipe");
    fcntl(fail_pipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid == 0) {
        // Only async-signal-safe calls from here to execve.
        if (out >= 0) {
            dup2(out, 1);
            dup2(out, 2);
        }
        if (!opts.cwd.empty() && chdir(opts.cwd.c_str()) != 0) {
            int msg[2] = {1, errno};
            (void)!write(fail_pipe[1], msg, sizeof(msg));
            _exit(127);
        }
        for (long fd = 3; fd < max_fd; ++fd)
            if (fd != fail_pipe[1]) close((int)fd);
        execve(prog.c_str(), cargv.data(), cenv.data());
        int msg[2] = {2, errno};
        (void)!write(fail_pipe[1], msg, sizeof(msg));
        _exit(127);
    }
    close(fail_pipe[1]);
    if (out >= 0) close(out);
    if (pid < 0) {
        close(fail_pipe[0]);
        throw std::runtime_error("OS.SPAWN: fork failed");
    }
    int msg[2] = {0, 0};
    ssize_t got = read(fail_pipe[0], msg, sizeof(msg));
    close(fail_pipe[0]);
    if (got == (ssize_t)sizeof(msg)) {
        int st = 0;
        waitpid(pid, &st, 0);
        if (msg[0] == 1)
            throw std::runtime_error("OS.SPAWN: cannot change to the directory " + opts.cwd + " (" + std::string(strerror(msg[1])) + ")");
        throw std::runtime_error("OS.SPAWN: cannot start " + argv[0] + " (" + std::string(strerror(msg[1])) + ")");
    }
    std::lock_guard<std::mutex> lock(table_mutex());
    table()[(int64_t)pid] = Child();
    return (int64_t)pid;
#endif
}

// TRUE once the child has finished; records its exit code. Caller holds the lock.
inline bool poll_locked(int64_t pid, Child& c, int64_t timeout_ms) {
    if (c.finished) return true;
#if defined(_WIN32)
    DWORD w = WaitForSingleObject(c.process, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    if (w != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    GetExitCodeProcess(c.process, &code);
    CloseHandle(c.process);
    c.process = nullptr;
    c.exit_code = (int)code;
#else
    int st = 0;
    pid_t r;
    if (timeout_ms < 0) {
        do { r = waitpid((pid_t)pid, &st, 0); } while (r < 0 && errno == EINTR);
    } else {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            r = waitpid((pid_t)pid, &st, WNOHANG);
            if (r != 0 || std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (r == 0) return false;
    }
    if (r < 0) c.exit_code = -1;
    else if (WIFEXITED(st)) c.exit_code = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) c.exit_code = 128 + WTERMSIG(st);
#endif
    c.finished = true;
    return true;
}

}  // namespace os_spawn
