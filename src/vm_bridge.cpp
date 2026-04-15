// vm_bridge.cpp — C-linkage bridge to the jdBasic VM for native executables.
// Compiled into jdbrt.dll. Wraps VM::call_function() with a simple C API.

#include "vm.h"
#include "vm_bridge.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>

// Forward declarations for module registrations
extern void register_sound_builtins(VM& vm);
extern void register_ffi_builtins(VM& vm);
extern void register_ai_builtins(VM& vm);
extern void register_llm_builtins(VM& vm);
#ifdef COM
extern void register_com_builtins(VM& vm);
#endif
#ifdef HTTP
extern void register_http_builtins(VM& vm);
#endif
#ifdef USE_SERIAL
extern void register_serial_builtins(VM& vm);
#endif
#ifdef GFX
extern void register_graphics_builtins(VM& vm);
#endif
#ifdef IMGUI
extern void register_gui_builtins(VM& vm);
#endif

// DLL-local base directory for module imports
static std::string g_base_dir = ".";

static void setup_parser_modules(Parser& parser) {
    // Minimal module reader for EVAL/EXECUTE
    parser.file_reader = [](const std::string& module_name) -> std::pair<std::string, std::string> {
        std::string lower = module_name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::vector<std::string> candidates = {
            g_base_dir + "/" + module_name + ".jdb",
            g_base_dir + "/" + lower + ".jdb",
            module_name + ".jdb",
            lower + ".jdb"
        };
        for (auto& cand : candidates) {
            std::ifstream f(cand);
            if (f.is_open()) {
                std::stringstream ss; ss << f.rdbuf();
                return {ss.str(), cand};
            }
        }
        return {"", ""};
    };
}

static void run_on_vm(VM& vm, const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    setup_parser_modules(parser);
    auto ast = parser.parse();
    Compiler compiler;
    compiler.compile(ast);
    vm.run_code(compiler.main_chunk(), compiler.functions());
}

// Internal: set up all builtins on a VM (mirrors main.cpp setup_dynamic_code)
static void setup_all_builtins(VM& vm) {
    vm.on_execute = [](VM& v, const std::string& code) {
        run_on_vm(v, code + "\n");
    };
    vm.on_eval = [](VM& v, const std::string& expr) -> Value {
        std::string code = "LET __EVAL_RESULT__ = " + expr + "\n";
        run_on_vm(v, code);
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
}

// Per-RT error storage
struct JdRTImpl {
    VM vm;
    std::string last_error;
    // Store VM Value objects for opaque handles (MAP, JSON objects)
    std::unordered_map<int64_t, Value> value_store;
    int64_t next_handle = 1;

    int64_t store_value(Value v) {
        int64_t h = next_handle++;
        value_store[h] = std::move(v);
        return h;
    }
};

// Helper: convert double args to Value vector
static std::vector<Value> args_to_values(const double* args, int nargs) {
    std::vector<Value> vargs;
    vargs.reserve(nargs);
    for (int i = 0; i < nargs; i++)
        vargs.push_back(Value::make_f64(args[i]));
    return vargs;
}

extern "C" {

JDRT_API JdRT jdrt_init(void) {
    auto* rt = new JdRTImpl();
    setup_all_builtins(rt->vm);
    return (JdRT)rt;
}

JDRT_API void jdrt_shutdown(JdRT handle) {
    delete (JdRTImpl*)handle;
}

JDRT_API double jdrt_call_f64(JdRT handle, const char* name,
                               const double* args, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = args_to_values(args, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        return result.to_double();
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return 0.0;
    }
}

JDRT_API char* jdrt_call_str(JdRT handle, const char* name,
                              const double* args, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = args_to_values(args, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        return _strdup(result.to_string().c_str());
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return _strdup("");
    }
}

JDRT_API void* jdrt_call_arr(JdRT handle, const char* name,
                              const double* args, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = args_to_values(args, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        // Return the Value's array object as opaque pointer
        // (caller must not free — managed by VM GC)
        if (result.type == ValueType::ARRAY && result.as_array())
            return (void*)result.as_array();
        return nullptr;
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return nullptr;
    }
}

JDRT_API void jdrt_call_void(JdRT handle, const char* name,
                              const double* args, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = args_to_values(args, nargs);
        rt->vm.call_function(name, vargs);
        rt->last_error.clear();
    } catch (const std::exception& e) {
        rt->last_error = e.what();
    }
}

JDRT_API void jdrt_free(void* ptr) {
    free(ptr);
}

// Helper: convert typed args to Value vector
static std::vector<Value> typed_args_to_values(JdRTImpl* rt, const int64_t* args, const int32_t* tags, int nargs) {
    std::vector<Value> vargs;
    vargs.reserve(nargs);
    for (int i = 0; i < nargs; i++) {
        switch (tags[i]) {
            case 0: { // i64 (stored as double bits)
                double d;
                memcpy(&d, &args[i], sizeof(double));
                vargs.push_back(Value::make_f64(d));
                break;
            }
            case 1: { // f64
                double d;
                memcpy(&d, &args[i], sizeof(double));
                vargs.push_back(Value::make_f64(d));
                break;
            }
            case 2: { // string (char* as intptr)
                const char* s = (const char*)(intptr_t)args[i];
                vargs.push_back(Value::make_string(s ? s : ""));
                break;
            }
            case 3: { // array (JdbArray* — not directly compatible, wrap as Value array)
                // For now, pass as f64 (opaque pointer)
                double d;
                memcpy(&d, &args[i], sizeof(double));
                vargs.push_back(Value::make_f64(d));
                break;
            }
            case 4: { // VM object handle — look up in value_store
                auto it = rt->value_store.find(args[i]);
                if (it != rt->value_store.end())
                    vargs.push_back(it->second);
                else
                    vargs.push_back(Value::make_none());
                break;
            }
            default: {
                double d;
                memcpy(&d, &args[i], sizeof(double));
                vargs.push_back(Value::make_f64(d));
                break;
            }
        }
    }
    return vargs;
}

JDRT_API double jdrt_call_typed_f64(JdRT handle, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        return result.to_double();
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return 0.0;
    }
}

JDRT_API char* jdrt_call_typed_str(JdRT handle, const char* name,
                                    const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        return _strdup(result.to_string().c_str());
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return _strdup("");
    }
}

JDRT_API void jdrt_call_typed_void(JdRT handle, const char* name,
                                    const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        rt->vm.call_function(name, vargs);
        rt->last_error.clear();
    } catch (const std::exception& e) {
        rt->last_error = e.what();
    }
}

JDRT_API int64_t jdrt_call_typed_obj(JdRT handle, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        // Store the result and return a handle
        return rt->store_value(std::move(result));
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return 0;
    }
}

JDRT_API void jdrt_release_value(JdRT handle, int64_t val_handle) {
    auto* rt = (JdRTImpl*)handle;
    rt->value_store.erase(val_handle);
}

JDRT_API const char* jdrt_last_error(JdRT handle) {
    auto* rt = (JdRTImpl*)handle;
    return rt->last_error.empty() ? nullptr : rt->last_error.c_str();
}

} // extern "C"
