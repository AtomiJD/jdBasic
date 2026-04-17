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
#include <unordered_map>
#include <mutex>

// Binary-string length registry — stored here so typed_args_to_values,
// jdrt_strlen and jdrt_call_typed_str can all share the state.
// (Moved above typed_args_to_values so the lookup path works.)
static std::unordered_map<const void*, size_t>& bin_lens() {
    static std::unordered_map<const void*, size_t> m;
    return m;
}
static std::mutex& bin_mx() { static std::mutex m; return m; }

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

// Forward decl of JdbArray (matches jdb_runtime.cpp)
struct JdbArrayFwd { double* data; int64_t length; int32_t flags; };

// Convert a JdbArray* to a VM Value (recursive for nested).
// Elements marked with flags bit 0 are ptr-encoded (string or nested array).
static Value jdbarray_to_value(JdbArrayFwd* arr) {
    if (!arr) return Value::make_array();
    Value r = Value::make_array();
    auto* out = r.as_array();
    out->elements.reserve(arr->length);
    bool has_ptr = (arr->flags & 1) != 0;
    bool has_string = (arr->flags & 2) != 0;
    // bit 1 set → all ptr elements are strings.
    // bit 0 without bit 1 → all ptr elements are nested JdbArrays.
    // (Mixed arrays would need per-element tagging; jdBasic doesn't allow that
    // at the language level for uniform arrays, so we rely on the bit meaning.)
    for (int64_t i = 0; i < arr->length; i++) {
        double d = arr->data[i];
        if (has_ptr) {
            union { double d; int64_t i; } u; u.d = d;
            void* p = (void*)(intptr_t)u.i;
            if (!p) {
                out->elements.push_back(Value::make_none());
                continue;
            }
            if (has_string) {
                out->elements.push_back(Value::make_string((const char*)p));
            } else {
                // Nested array: recurse
                out->elements.push_back(jdbarray_to_value((JdbArrayFwd*)p));
            }
        } else {
            out->elements.push_back(Value::make_f64(d));
        }
    }
    return r;
}

// Helper: convert typed args to Value vector
static std::vector<Value> typed_args_to_values(JdRTImpl* rt, const int64_t* args, const int32_t* tags, int nargs) {
    std::vector<Value> vargs;
    vargs.reserve(nargs);
    for (int i = 0; i < nargs; i++) {
        switch (tags[i]) {
            case 0: { // i64 — preserved exactly by the native caller
                vargs.push_back(Value::make_i64(args[i]));
                break;
            }
            case 1: { // f64
                double d;
                memcpy(&d, &args[i], sizeof(double));
                vargs.push_back(Value::make_f64(d));
                break;
            }
            case 2: { // string (char* as intptr) — may be binary with embedded nulls
                const char* s = (const char*)(intptr_t)args[i];
                if (!s) { vargs.push_back(Value::make_string("")); break; }
                // Use registered binary length if present, else strlen.
                int64_t blen;
                {
                    std::lock_guard<std::mutex> lk(bin_mx());
                    auto it = bin_lens().find((const void*)s);
                    blen = (it != bin_lens().end()) ? (int64_t)it->second : -1;
                }
                if (blen >= 0)
                    vargs.push_back(Value::make_string(std::string(s, (size_t)blen)));
                else
                    vargs.push_back(Value::make_string(s));
                break;
            }
            case 3: { // array (JdbArray*) — convert to VM Value array
                JdbArrayFwd* arr = (JdbArrayFwd*)(intptr_t)args[i];
                vargs.push_back(jdbarray_to_value(arr));
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

// Binary-string length registry — lets LEN$ recover the true byte length
// for strings with embedded nulls (from PACK$, binary I/O).
// (bin_lens() and bin_mx() are defined near the top of this file.)

static void register_binary_string(const char* s, size_t n) {
    if (!s || n == 0) return;
    // Only bother if the string actually has embedded nulls
    if (strlen(s) == n) return;
    std::lock_guard<std::mutex> lk(bin_mx());
    bin_lens()[(const void*)s] = n;
}

// Exported: native runtime's jdb_len_str calls this first.
// Returns -1 if the string isn't in our binary registry (use strlen).
JDRT_API int64_t jdrt_strlen(const char* s) {
    if (!s) return 0;
    std::lock_guard<std::mutex> lk(bin_mx());
    auto it = bin_lens().find((const void*)s);
    if (it != bin_lens().end()) return (int64_t)it->second;
    return -1;
}

JDRT_API char* jdrt_call_typed_str(JdRT handle, const char* name,
                                    const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        // For STRING values, preserve embedded nulls (binary data from PACK$).
        if (result.type == ValueType::STRING) {
            const std::string& s = result.as_string()->data;
            char* buf = (char*)malloc(s.size() + 1);
            memcpy(buf, s.data(), s.size());
            buf[s.size()] = '\0';
            register_binary_string(buf, s.size());
            return buf;
        }
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

// ── Frame-based handle cleanup ──────────────────────────────────
// Compiled programs allocate many VM handles per frame iteration
// (nested map/array access, bridge call results). A frame-begin
// snapshot + frame-end sweep keeps the store bounded.

JDRT_API int64_t jdrt_frame_begin(JdRT handle) {
    auto* rt = (JdRTImpl*)handle;
    return rt->next_handle;  // watermark: all handles < this survive
}

JDRT_API void jdrt_frame_end(JdRT handle, int64_t watermark) {
    auto* rt = (JdRTImpl*)handle;
    // Erase all entries with key >= watermark (allocated during this frame).
    for (auto it = rt->value_store.begin(); it != rt->value_store.end(); ) {
        if (it->first >= watermark)
            it = rt->value_store.erase(it);
        else
            ++it;
    }
}

// ── Field access on opaque VM Value handles (Maps / JSON objects) ──
// All return a sentinel/empty value if the handle is unknown or the field
// is missing — never crash the compiled binary on a typo.

static const Value* obj_field(JdRTImpl* rt, int64_t h, const char* key) {
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end() || it->second.type != ValueType::OBJECT)
        return nullptr;
    auto* m = it->second.as_object();
    Value* found = m->get(std::string(key ? key : ""));
    return found;
}

JDRT_API double jdrt_obj_get_f64(JdRT handle, int64_t h, const char* key) {
    auto* rt = (JdRTImpl*)handle;
    const Value* v = obj_field(rt, h, key);
    return v ? v->to_double() : 0.0;
}

JDRT_API const char* jdrt_obj_get_str(JdRT handle, int64_t h, const char* key) {
    auto* rt = (JdRTImpl*)handle;
    const Value* v = obj_field(rt, h, key);
    if (!v) return _strdup("");
    if (v->type == ValueType::STRING) return _strdup(v->as_string()->data.c_str());
    return _strdup(v->to_string().c_str());
}

JDRT_API int64_t jdrt_obj_get_obj(JdRT handle, int64_t h, const char* key) {
    auto* rt = (JdRTImpl*)handle;
    const Value* v = obj_field(rt, h, key);
    if (!v) return 0;
    // Store *any* non-missing field so the caller always receives a valid
    // handle. Scalars (STRING, numbers) materialise later via coerce paths;
    // Objects/Arrays can be further indexed. Returning 0 for scalars broke
    // 'title = g{"title"}' where the field's a string.
    return rt->store_value(*v);
}

// jdrt_obj_get_arr is defined below where JdbArray / value_to_jdbarray
// are in scope (right after value_to_jdbarray's definition).

JDRT_API int64_t jdrt_obj_exists(JdRT handle, int64_t h, const char* key) {
    auto* rt = (JdRTImpl*)handle;
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end() || it->second.type != ValueType::OBJECT)
        return 0;
    auto* m = it->second.as_object();
    return m->get(std::string(key ? key : "")) ? 1 : 0;
}

// JdbArray struct matching jdb_runtime.cpp (keep in sync!)
struct JdbArray {
    double* data;
    int64_t length;
    int32_t flags;
};

// Convert VM Value array to JdbArray (recursive for nested)
static JdbArray* value_to_jdbarray(const Value& v) {
    auto* r = (JdbArray*)malloc(sizeof(JdbArray));
    r->data = nullptr;
    r->length = 0;
    r->flags = 0;
    if (v.type != ValueType::ARRAY) return r;
    auto* arr = v.as_array();
    r->length = (int64_t)arr->elements.size();
    r->data = (double*)calloc(r->length > 0 ? r->length : 1, sizeof(double));
    // flags bit 0: elements are ptrs (strings OR nested arrays — both need decoding)
    // flags bit 1: elements are strings (so decoder knows to treat ptr as char*)
    // If an array mixes strings and sub-arrays, both bits set; caller inspects
    // each element via helpers.
    bool has_ptr = false, has_string = false;
    for (int64_t i = 0; i < r->length; i++) {
        const auto& e = arr->elements[i];
        if (e.type == ValueType::ARRAY) {
            has_ptr = true;
            JdbArray* inner = value_to_jdbarray(e);
            union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)inner;
            r->data[i] = u.d;
        } else if (e.type == ValueType::STRING) {
            has_ptr = true; has_string = true;
            const std::string& s = e.as_string()->data;
            char* copy = _strdup(s.c_str());
            union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)copy;
            r->data[i] = u.d;
        } else {
            r->data[i] = e.to_double();
        }
    }
    if (has_ptr) r->flags |= 1;
    if (has_string) r->flags |= 2;
    return r;
}

// Convert a stored VM Value handle to a scalar. For numeric Values this
// is to_double / to_string of the Value; for Maps/Arrays the fallback of
// to_double() is 0 and to_string() is a formatted dump.
JDRT_API double jdrt_val_to_f64(JdRT handle, int64_t h) {
    auto* rt = (JdRTImpl*)handle;
    auto it = rt->value_store.find(h);
    return (it != rt->value_store.end()) ? it->second.to_double() : 0.0;
}

JDRT_API const char* jdrt_val_to_str(JdRT handle, int64_t h) {
    auto* rt = (JdRTImpl*)handle;
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end()) return _strdup("");
    if (it->second.type == ValueType::STRING)
        return _strdup(it->second.as_string()->data.c_str());
    return _strdup(it->second.to_string().c_str());
}

// Integer-indexed access on a stored Array Value — returns a fresh
// handle for the element so subsequent ops see the element's real type.
JDRT_API int64_t jdrt_val_arr_get(JdRT handle, int64_t h, int64_t idx) {
    auto* rt = (JdRTImpl*)handle;
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end() || it->second.type != ValueType::ARRAY) return 0;
    auto* arr = it->second.as_array();
    if (idx < 0 || (size_t)idx >= arr->elements.size()) return 0;
    return rt->store_value(arr->elements[(size_t)idx]);
}

JDRT_API int64_t jdrt_val_length(JdRT handle, int64_t h) {
    auto* rt = (JdRTImpl*)handle;
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end()) return 0;
    if (it->second.type == ValueType::ARRAY) return (int64_t)it->second.as_array()->elements.size();
    if (it->second.type == ValueType::STRING) return (int64_t)it->second.as_string()->data.size();
    if (it->second.type == ValueType::OBJECT) return (int64_t)it->second.as_object()->fields.size();
    return 0;
}

// ── Tagged field access: returns JdTaggedVal with runtime type tag ──

JDRT_API int32_t jdrt_obj_get_tagged(JdRT handle, int64_t h, const char* key, int64_t* out_val) {
    *out_val = 0;
    auto* rt = (JdRTImpl*)handle;
    const Value* v = obj_field(rt, h, key);
    if (!v) return 0;
    union { double d; int64_t i; } u;
    switch (v->type) {
        case ValueType::STRING:
            *out_val = (int64_t)(intptr_t)_strdup(v->as_string()->data.c_str());
            return 2;
        case ValueType::ARRAY:
            *out_val = (int64_t)(intptr_t)value_to_jdbarray(*v);
            return 3;
        case ValueType::OBJECT:
            *out_val = rt->store_value(*v);
            return 6;
        default:
            u.d = v->to_double();
            *out_val = u.i;
            return 1;
    }
}

// Field-access companion to obj_get_*: pull an array-typed field out of
// a stored Value handle and convert it to the native JdbArray shape.
JDRT_API void* jdrt_obj_get_arr(JdRT handle, int64_t h, const char* key) {
    auto* rt = (JdRTImpl*)handle;
    const Value* v = obj_field(rt, h, key);
    if (!v) {
        auto* r = (JdbArray*)malloc(sizeof(JdbArray));
        r->data = nullptr; r->length = 0; r->flags = 0;
        return r;
    }
    return value_to_jdbarray(*v);
}

// Returns a JdbArray* for functions that return arrays (SPLIT, KEYS, etc.)
JDRT_API void* jdrt_call_typed_arr(JdRT handle, const char* name,
                                    const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = (JdRTImpl*)handle;
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        return value_to_jdbarray(result);
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        auto* r = (JdbArray*)malloc(sizeof(JdbArray));
        r->data = nullptr; r->length = 0; r->flags = 0;
        return r;
    }
}

JDRT_API const char* jdrt_last_error(JdRT handle) {
    auto* rt = (JdRTImpl*)handle;
    return rt->last_error.empty() ? nullptr : rt->last_error.c_str();
}

} // extern "C"
