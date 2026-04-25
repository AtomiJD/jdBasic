#include "ffi.h"
#include "vm.h"
#include "errors.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

// ── Platform abstraction ────────────────────────────────────────
//
// FFI is supported on Windows (LoadLibrary/GetProcAddress) and on
// POSIX systems (dlopen/dlsym). Calling convention is identical on
// x86-64: integer/pointer args go through registers + stack and we
// only ever pass intptr_t-sized values, so a single set of function
// pointer typedefs covers both Win64 and SysV.

#ifdef _WIN32
  #define NOMINMAX
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  using DllHandle = HMODULE;
  using SymbolPtr = FARPROC;
  static DllHandle dll_open(const char* name) { return LoadLibraryA(name); }
  static SymbolPtr dll_sym(DllHandle h, const char* sym) { return GetProcAddress(h, sym); }
  static std::string dll_error() { return "GetLastError=" + std::to_string(GetLastError()); }
#else
  #include <dlfcn.h>
  using DllHandle = void*;
  using SymbolPtr = void*;
  static DllHandle dll_open(const char* name) { return dlopen(name, RTLD_NOW | RTLD_LOCAL); }
  static SymbolPtr dll_sym(DllHandle h, const char* sym) { return dlsym(h, sym); }
  static std::string dll_error() { const char* e = dlerror(); return e ? std::string(e) : ""; }
#endif

// ── Library name resolution ─────────────────────────────────────

static bool has_path_separator(const std::string& s) {
    return s.find('/') != std::string::npos || s.find('\\') != std::string::npos;
}

static bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
}

// Build the candidate list to try for a LIB "<name>" string.
// If the name already has an extension or a path separator we trust
// the caller. Otherwise we add the right platform suffix(es) so the
// same .jdb source can target Windows and POSIX without conditionals.
static std::vector<std::string> dll_candidate_names(const std::string& name) {
    std::vector<std::string> out;
    bool has_ext = ends_with_ci(name, ".dll") ||
                   ends_with_ci(name, ".so")  ||
                   ends_with_ci(name, ".dylib");
    if (has_path_separator(name) || has_ext) {
        out.push_back(name);
        return out;
    }
#ifdef _WIN32
    out.push_back(name + ".dll");
    out.push_back(name);
#elif defined(__APPLE__)
    out.push_back("lib" + name + ".dylib");
    out.push_back(name + ".dylib");
    out.push_back("./lib" + name + ".dylib");
    out.push_back("./" + name + ".dylib");
#else
    out.push_back("lib" + name + ".so");
    out.push_back(name + ".so");
    out.push_back("./lib" + name + ".so");
    out.push_back("./" + name + ".so");
#endif
    return out;
}

static std::unordered_map<std::string, DllHandle> g_dll_cache;

static DllHandle get_dll(const std::string& name) {
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    auto it = g_dll_cache.find(key);
    if (it != g_dll_cache.end()) return it->second;

    DllHandle h = nullptr;
    std::string last_err;
    for (const auto& candidate : dll_candidate_names(name)) {
        h = dll_open(candidate.c_str());
        if (h) break;
        last_err = dll_error();
    }
    if (!h) throw jdError(ErrCode::RUNTIME_ERROR,
        "Cannot load library: " + name + (last_err.empty() ? "" : " (" + last_err + ")"));
    g_dll_cache[key] = h;
    return h;
}

// ── Generic FFI caller (x86-64) ─────────────────────────────────

typedef intptr_t (*FP0)();
typedef intptr_t (*FP1)(intptr_t);
typedef intptr_t (*FP2)(intptr_t, intptr_t);
typedef intptr_t (*FP3)(intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP4)(intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP5)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP6)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP7)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP8)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);

static intptr_t call_ffi(SymbolPtr proc, intptr_t* a, int n) {
    switch (n) {
        case 0: return ((FP0)proc)();
        case 1: return ((FP1)proc)(a[0]);
        case 2: return ((FP2)proc)(a[0], a[1]);
        case 3: return ((FP3)proc)(a[0], a[1], a[2]);
        case 4: return ((FP4)proc)(a[0], a[1], a[2], a[3]);
        case 5: return ((FP5)proc)(a[0], a[1], a[2], a[3], a[4]);
        case 6: return ((FP6)proc)(a[0], a[1], a[2], a[3], a[4], a[5]);
        case 7: return ((FP7)proc)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
        case 8: return ((FP8)proc)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        default:
            throw jdError(ErrCode::RUNTIME_ERROR, "FFI: too many parameters (max 8)");
    }
}

// ── FFI declaration info ────────────────────────────────────────

struct FFIDecl {
    std::string name;
    std::string dll;
    std::string alias;
    std::vector<std::string> param_types; // "INTEGER", "STRING", "RETURN"
    std::string return_type;              // "INTEGER", "STRING", "ARRAY", "VOID"
    SymbolPtr proc = nullptr;             // cached
};

static std::unordered_map<std::string, FFIDecl> g_ffi_decls;

// ── RETURN buffer sizing ────────────────────────────────────────
// The RETURN parameter is an output char buffer the native function
// writes into. Its size is taken from the caller-supplied integer in
// that argument slot (in bytes). 0 / missing → default. Above the cap
// we clamp to protect against accidental huge allocations.

static constexpr int FFI_RETURN_BUF_DEFAULT = 64 * 1024;       // 64 KB
static constexpr int FFI_RETURN_BUF_MAX     = 64 * 1024 * 1024; // 64 MB

static Value ffi_call_wrapper(const FFIDecl& decl, const std::vector<Value>& args) {
    SymbolPtr proc = decl.proc;
    if (!proc) {
        DllHandle h = get_dll(decl.dll);
        proc = dll_sym(h, decl.alias.c_str());
        if (!proc) throw jdError(ErrCode::RUNTIME_ERROR,
            "FFI: function '" + decl.alias + "' not found in " + decl.dll);
        const_cast<FFIDecl&>(decl).proc = proc;
    }

    int param_count = (int)decl.param_types.size();
    intptr_t raw_args[8] = {};
    std::vector<std::string> str_storage(param_count);

    // First pass: figure out per-RETURN buffer sizes from caller args.
    std::vector<int> return_sizes;
    {
        int ai = 0;
        for (int i = 0; i < param_count; i++) {
            if (decl.param_types[i] == "RETURN") {
                int sz = FFI_RETURN_BUF_DEFAULT;
                if (ai < (int)args.size()) {
                    long long requested = (long long)args[ai].to_int();
                    if (requested > 0) {
                        if (requested > FFI_RETURN_BUF_MAX) requested = FFI_RETURN_BUF_MAX;
                        sz = (int)requested;
                    }
                }
                return_sizes.push_back(sz);
            }
            ai++;
        }
    }

    std::vector<std::vector<char>> return_buffers;
    return_buffers.reserve(return_sizes.size());
    for (int sz : return_sizes) return_buffers.emplace_back(sz, 0);

    std::vector<int> return_indices;
    int return_idx = 0;
    int arg_idx = 0;
    for (int i = 0; i < param_count && i < 8; i++) {
        const std::string& ptype = decl.param_types[i];
        if (ptype == "RETURN") {
            raw_args[i] = (intptr_t)return_buffers[return_idx].data();
            return_indices.push_back(return_idx);
            return_idx++;
            arg_idx++;
        }
        else if (ptype == "STRING") {
            if (arg_idx < (int)args.size()) {
                str_storage[i] = args[arg_idx].to_string();
                raw_args[i] = (intptr_t)str_storage[i].c_str();
            }
            arg_idx++;
        }
        else { // INTEGER or anything else → intptr_t
            if (arg_idx < (int)args.size()) {
                raw_args[i] = (intptr_t)args[arg_idx].to_int();
            }
            arg_idx++;
        }
    }

    intptr_t result = call_ffi(proc, raw_args, param_count);

    if (!return_indices.empty() || decl.return_type == "ARRAY") {
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(result));
        for (size_t ri = 0; ri < return_indices.size(); ri++) {
            // NUL-terminated up to capacity.
            const char* data = return_buffers[ri].data();
            int cap = (int)return_buffers[ri].size();
            int len = 0;
            while (len < cap && data[len] != 0) len++;
            arr.as_array()->elements.push_back(Value::make_string(std::string(data, (size_t)len)));
        }
        return arr;
    }

    if (decl.return_type == "STRING") {
        if (result == 0) return Value::make_string("");
        return Value::make_string(std::string((const char*)result));
    }
    if (decl.return_type == "VOID") {
        return Value::make_none();
    }
    return Value::make_i64(result);
}

// ── Register FFI builtins ───────────────────────────────────────

static std::string ffi_to_upper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return (char)std::toupper(c); });
    return r;
}

void register_ffi_builtins(VM& vm) {
    // __FFI_DECLARE(name, dll, alias, [param_types], [param_names], ret_type)
    vm.register_native("__FFI_DECLARE", [&vm](const std::vector<Value>& args) -> Value {
        if (args.size() < 6)
            throw jdError(ErrCode::WRONG_ARG_COUNT, "__FFI_DECLARE: internal error");

        FFIDecl decl;
        decl.name = args[0].as_string()->data;
        decl.dll = args[1].as_string()->data;
        decl.alias = args[2].as_string()->data;

        auto* ptypes = args[3].as_array();
        for (auto& e : ptypes->elements)
            decl.param_types.push_back(e.as_string()->data);

        decl.return_type = args[5].as_string()->data;

        // Store under uppercase so the wrapper resolves regardless of
        // which casing the dispatch site arrives with.
        std::string key = ffi_to_upper(decl.name);
        g_ffi_decls[key] = decl;

        auto wrapper = [key](const std::vector<Value>& call_args) -> Value {
            auto it = g_ffi_decls.find(key);
            if (it == g_ffi_decls.end())
                throw jdError(ErrCode::UNDEFINED_FUNCTION, "FFI function not declared: " + key);
            return ffi_call_wrapper(it->second, call_args);
        };

        // Register under both the original (case-preserved) name and the
        // uppercased name. The interpreter dispatches with the parser's
        // identifier value as-is; the LLVM-codegen path uppercases call
        // names before passing them through jdrt_call_*, so without the
        // uppercase alias native-compiled programs hit
        // "Undefined function: <UPPERCASE>" on the FFI call site.
        vm.register_native(decl.name, wrapper);
        if (key != decl.name) vm.register_native(key, wrapper);

        return Value::make_none();
    });
}
