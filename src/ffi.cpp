#include "ffi.h"
#include "vm.h"
#include "errors.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

// ── DLL cache ───────────────────────────────────────────────────

static std::unordered_map<std::string, HMODULE> g_dll_cache;

static HMODULE get_dll(const std::string& name) {
    // Case-insensitive cache key
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    auto it = g_dll_cache.find(key);
    if (it != g_dll_cache.end()) return it->second;
    HMODULE h = LoadLibraryA(name.c_str());
    if (!h) throw jdError(ErrCode::RUNTIME_ERROR, "Cannot load DLL: " + name);
    g_dll_cache[key] = h;
    return h;
}

// ── Generic FFI caller (x64) ────────────────────────────────────
// On x64 Windows there is only one calling convention.
// We dispatch by argument count, casting the function pointer.

typedef intptr_t (*FP0)();
typedef intptr_t (*FP1)(intptr_t);
typedef intptr_t (*FP2)(intptr_t, intptr_t);
typedef intptr_t (*FP3)(intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP4)(intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP5)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP6)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP7)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
typedef intptr_t (*FP8)(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);

static intptr_t call_ffi(FARPROC proc, intptr_t* a, int n) {
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
    FARPROC proc = nullptr;              // cached
};

static std::unordered_map<std::string, FFIDecl> g_ffi_decls;

// ── Build the native wrapper for a declared function ────────────

static Value ffi_call_wrapper(const FFIDecl& decl, const std::vector<Value>& args) {
    // Resolve proc if not cached
    FARPROC proc = decl.proc;
    if (!proc) {
        HMODULE h = get_dll(decl.dll);
        proc = GetProcAddress(h, decl.alias.c_str());
        if (!proc) throw jdError(ErrCode::RUNTIME_ERROR,
            "FFI: function '" + decl.alias + "' not found in " + decl.dll);
        // Cache it (const_cast to update the cached copy)
        const_cast<FFIDecl&>(decl).proc = proc;
    }

    int param_count = (int)decl.param_types.size();
    intptr_t raw_args[8] = {};

    // Temporary storage for string buffers and RETURN buffers
    std::vector<std::string> str_storage(param_count);
    // Pre-count RETURN params so we can pre-allocate (avoid realloc invalidation)
    int return_count = 0;
    for (auto& pt : decl.param_types) if (pt == "RETURN") return_count++;
    std::vector<std::vector<char>> return_buffers(return_count, std::vector<char>(4096, 0));
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

    // Call the function
    intptr_t result = call_ffi(proc, raw_args, param_count);

    // Build return value
    if (!return_indices.empty() || decl.return_type == "ARRAY") {
        // Pack [return_value, return_param1, return_param2, ...]
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(result));
        for (size_t ri = 0; ri < return_indices.size(); ri++) {
            // RETURN params are char buffers → convert to string
            std::string s(return_buffers[ri].data());
            arr.as_array()->elements.push_back(Value::make_string(s));
        }
        return arr;
    }

    if (decl.return_type == "STRING") {
        // Result is a char pointer
        if (result == 0) return Value::make_string("");
        return Value::make_string(std::string((const char*)result));
    }

    if (decl.return_type == "VOID") {
        return Value::make_none();
    }

    // INTEGER (default)
    return Value::make_i64(result);
}

#endif // _WIN32

// ── Register FFI builtins ───────────────────────────────────────

void register_ffi_builtins(VM& vm) {
#ifdef _WIN32
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

        // Store the declaration
        g_ffi_decls[decl.name] = decl;

        // Register a native function with this name
        std::string fn_name = decl.name;
        vm.register_native(fn_name, [fn_name](const std::vector<Value>& call_args) -> Value {
            auto it = g_ffi_decls.find(fn_name);
            if (it == g_ffi_decls.end())
                throw jdError(ErrCode::UNDEFINED_FUNCTION, "FFI function not declared: " + fn_name);
            return ffi_call_wrapper(it->second, call_args);
        });

        return Value::make_none();
    });
#else
    vm.register_native("__FFI_DECLARE", [](const std::vector<Value>& args) -> Value {
        (void)args;
        throw jdError(ErrCode::RUNTIME_ERROR, "DECLARE: FFI only available on Windows");
    });
#endif
}
