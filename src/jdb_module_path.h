#pragma once
// Candidate paths for `IMPORT name`, shared by the interpreter, the native
// compiler, the REPL, the MCP host and the runtime DLL.
//
// Order, first match wins:
//   1. the script's own directory, then a `modules/` subdir of it
//   2. the working directory, then a `modules/` subdir of it
//   3. every directory in JDBASIC_PATH
//   4. <user home>/.jdbasic/lib
//   5. <install dir>/lib
//
// A project-local module therefore always shadows a library one, which the
// importer reports on stderr.

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace jdb_modpath {

#ifdef _WIN32
static const char PATH_SEP = ';';
#else
static const char PATH_SEP = ':';
#endif

// Directory holding the running executable, or "" when it cannot be determined.
inline std::string install_dir() {
    std::string exe;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) exe.assign(buf, len);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) exe = buf;
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) exe.assign(buf, len);
#endif
    if (exe.empty()) return "";
    size_t sep = exe.find_last_of("/\\");
    return (sep == std::string::npos) ? "" : exe.substr(0, sep);
}

inline std::string home_dir() {
#ifdef _WIN32
    // USERPROFILE before HOME: a shell like Git Bash sets HOME to a POSIX
    // path ("/c/Users/name") that the Win32 file APIs cannot open.
    const char* up = std::getenv("USERPROFILE");
    if (up && *up) return up;
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && path) return std::string(drive) + path;
#endif
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    return "";
}

// Appends "<dir>/<NAME>.jdb" and "<dir>/<name>.jdb" to out.
inline void add_dir(std::vector<std::string>& out, const std::string& dir,
                    const std::string& upper, const std::string& lower) {
    if (dir.empty()) return;
    out.push_back(dir + "/" + upper + ".jdb");
    out.push_back(dir + "/" + lower + ".jdb");
}

// The library locations, in search order: every directory in JDBASIC_PATH,
// then <user home>/.jdbasic/lib, then <install dir>/lib.
inline void add_library_dirs(std::vector<std::string>& out,
                             const std::string& upper, const std::string& lower) {
    if (const char* env = std::getenv("JDBASIC_PATH")) {
        std::string spec = env;
        size_t start = 0;
        while (start <= spec.size()) {
            size_t end = spec.find(PATH_SEP, start);
            if (end == std::string::npos) end = spec.size();
            std::string dir = spec.substr(start, end - start);
            while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
            add_dir(out, dir, upper, lower);
            if (end == spec.size()) break;
            start = end + 1;
        }
    }

    std::string home = home_dir();
    if (!home.empty()) add_dir(out, home + "/.jdbasic/lib", upper, lower);
    std::string install = install_dir();
    if (!install.empty()) add_dir(out, install + "/lib", upper, lower);
}

inline std::vector<std::string> library_candidates(const std::string& module_name) {
    std::string upper = module_name;
    std::string lower = module_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> out;
    add_library_dirs(out, upper, lower);
    return out;
}

inline std::vector<std::string> candidates(const std::string& module_name,
                                           const std::string& base_dir) {
    std::string upper = module_name;
    std::string lower = module_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::vector<std::string> out;
    if (!base_dir.empty()) {
        add_dir(out, base_dir, upper, lower);
        // Backslash spellings keep the debugger's file matching working on
        // Windows paths that were handed in with native separators.
        out.push_back(base_dir + "\\" + upper + ".jdb");
        out.push_back(base_dir + "\\" + lower + ".jdb");
        add_dir(out, base_dir + "/modules", upper, lower);
    }
    out.push_back(upper + ".jdb");
    out.push_back(lower + ".jdb");
    add_dir(out, "modules", upper, lower);

    add_library_dirs(out, upper, lower);
    return out;
}

// The path as the module reader reports it: absolute, native separators.
inline std::string abs_path(const std::string& p) {
    char buf[4096];
#ifdef _WIN32
    DWORD len = GetFullPathNameA(p.c_str(), (DWORD)sizeof(buf), buf, nullptr);
    if (len > 0 && len < sizeof(buf)) return std::string(buf, len);
#else
    if (realpath(p.c_str(), buf)) return std::string(buf);
#endif
    return p;
}

// Two spellings of one file. Windows compares case-insensitively and treats
// both separators alike.
inline bool same_path(const std::string& a, const std::string& b) {
    std::string x = abs_path(a), y = abs_path(b);
#ifdef _WIN32
    auto norm = [](std::string& s) {
        for (char& c : s) {
            if (c == '\\') c = '/';
            c = (char)::tolower((unsigned char)c);
        }
    };
    norm(x);
    norm(y);
#endif
    return x == y;
}

// The library file that `picked` hides, or "" when `picked` came from the
// library path itself or no library carries that name. Costs one open per
// library candidate, so it runs once a module has already resolved.
inline std::string shadowed_library_path(const std::string& module_name,
                                         const std::string& picked) {
    std::vector<std::string> libs = library_candidates(module_name);
    for (const auto& cand : libs)
        if (same_path(cand, picked)) return "";
    for (const auto& cand : libs) {
        std::ifstream f(cand);
        if (f.is_open()) return cand;
    }
    return "";
}

} // namespace jdb_modpath
