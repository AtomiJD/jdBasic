// vm_bridge.cpp — C-linkage bridge to the jdBasic VM for native executables.
// Compiled into jdbrt.dll. Wraps VM::call_function() with a simple C API.

#include "vm.h"
#include "vm_bridge.h"
#include "jdb_tags.h"
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

// Map of char* buffers that came out of PACK$/binary I/O and may contain
// embedded nulls. jdrt_strlen consults this registry before falling back
// to strlen() so LEN$ reports the true byte length.
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

// One VM instance per jdrt_init() caller, plus the side tables used to
// translate between VM Values and the integer handles the native-compiled
// code can store in i64 variables.
struct JdRTImpl {
    VM vm;
    std::string last_error;
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
        // Borrow the ArrayObj* — owned by the VM, caller must not free.
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

// Must stay layout-compatible with JdbArray in jdb_runtime.cpp; duplicated
// so the bridge does not pull the whole runtime header in.
struct JdbArrayFwd { double* data; int64_t length; int32_t flags; };

// flags bit 0: element doubles are pointers punned as f64.
// flags bit 1: those pointers are char* strings (else JdbArray* nested).
// jdBasic's uniform-array rule means one of these encodings applies to
// every element — no per-element tag is needed.
static Value jdbarray_to_value(JdbArrayFwd* arr) {
    if (!arr) return Value::make_array();
    Value r = Value::make_array();
    auto* out = r.as_array();
    out->elements.reserve(arr->length);
    bool has_ptr = (arr->flags & 1) != 0;
    bool has_string = (arr->flags & 2) != 0;
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

// Decode the {args, tags} wire format into VM Values. Only the tags that
// appear on the wire are handled here — NATIVE_MAP never crosses the
// bridge (codegen downgrades it to I64) and RUNTIME is unpacked by the
// caller into one of the concrete tags below.
static std::vector<Value> typed_args_to_values(JdRTImpl* rt, const int64_t* args, const int32_t* tags, int nargs) {
    std::vector<Value> vargs;
    vargs.reserve(nargs);
    for (int i = 0; i < nargs; i++) {
        switch (static_cast<JdTag>(tags[i])) {
            case JdTag::I64: {
                vargs.push_back(Value::make_i64(args[i]));
                break;
            }
            case JdTag::F64: {
                double d;
                memcpy(&d, &args[i], sizeof(double));
                vargs.push_back(Value::make_f64(d));
                break;
            }
            case JdTag::STR: {
                const char* s = (const char*)(intptr_t)args[i];
                if (!s) { vargs.push_back(Value::make_string("")); break; }
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
            case JdTag::ARR: {
                JdbArrayFwd* arr = (JdbArrayFwd*)(intptr_t)args[i];
                vargs.push_back(jdbarray_to_value(arr));
                break;
            }
            case JdTag::NATIVE_MAP:
                // Wire isn't supposed to carry NATIVE_MAP — codegen
                // downgrades it to I64 on the way out. A runtime-tagged
                // value produced by jdrt_tagged_get off a native JdbMap*
                // can still leak one through, so fall through to VM_HANDLE
                // lookup: miss→NONE keeps the call surviving instead of
                // punning a pointer through make_f64.
            case JdTag::VM_HANDLE: {
                auto it = rt->value_store.find(args[i]);
                if (it != rt->value_store.end())
                    vargs.push_back(it->second);
                else
                    vargs.push_back(Value::make_none());
                break;
            }
            case JdTag::FUNCREF:
            case JdTag::RUNTIME:
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

static void register_binary_string(const char* s, size_t n) {
    if (!s || n == 0) return;
    // Plain C strings don't need an entry — strlen already gets it right.
    if (strlen(s) == n) return;
    std::lock_guard<std::mutex> lk(bin_mx());
    bin_lens()[(const void*)s] = n;
}

// Called by the native runtime's jdb_len_str before it falls back to
// strlen. Returns -1 for unregistered buffers.
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
        if (result.type == ValueType::STRING) {
            // memcpy preserves embedded nulls (PACK$, binary I/O) that
            // strcpy would truncate; register the length so LEN$ can
            // recover it later.
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

// Snapshot next_handle at loop start; anything allocated between begin
// and end is a frame-local temporary and gets swept at end so
// value_store doesn't grow unbounded across frames.
JDRT_API int64_t jdrt_frame_begin(JdRT handle) {
    auto* rt = (JdRTImpl*)handle;
    return rt->next_handle;
}

JDRT_API void jdrt_frame_end(JdRT handle, int64_t watermark) {
    auto* rt = (JdRTImpl*)handle;
    for (auto it = rt->value_store.begin(); it != rt->value_store.end(); ) {
        if (it->first >= watermark)
            it = rt->value_store.erase(it);
        else
            ++it;
    }
}

// Every obj_get_* returns a safe default on unknown-handle or missing-key
// so a typo in compiled code never crashes the exe.
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
    // Store every non-missing field — including scalars — so the caller
    // always gets a live handle. Scalars materialise via the val_to_*
    // coerce paths; returning 0 for a string-typed scalar would break
    // patterns like `title = g{"title"}`.
    return rt->store_value(*v);
}

JDRT_API int64_t jdrt_obj_exists(JdRT handle, int64_t h, const char* key) {
    auto* rt = (JdRTImpl*)handle;
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end() || it->second.type != ValueType::OBJECT)
        return 0;
    auto* m = it->second.as_object();
    return m->get(std::string(key ? key : "")) ? 1 : 0;
}

// Must stay layout-compatible with JdbArray in jdb_runtime.cpp.
struct JdbArray {
    double* data;
    int64_t length;
    int32_t flags;
};

// Inverse of jdbarray_to_value: walk a VM array, copy its numeric cells
// verbatim and pun inner strings/arrays as pointers into the data[]
// slots. flags records which punning the decoder should do.
static JdbArray* value_to_jdbarray(const Value& v) {
    auto* r = (JdbArray*)malloc(sizeof(JdbArray));
    r->data = nullptr;
    r->length = 0;
    r->flags = 0;
    if (v.type != ValueType::ARRAY) return r;
    auto* arr = v.as_array();
    r->length = (int64_t)arr->elements.size();
    r->data = (double*)calloc(r->length > 0 ? r->length : 1, sizeof(double));
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

// Materialise a handle as a concrete scalar. Numeric values round-trip
// exactly; Maps/Arrays fall back to to_double()==0 and to_string()'s
// formatted dump — callers that care must check the type first.
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

// Returns a fresh handle for the element so the caller sees the real
// element type on subsequent ops (needed for arrays of mixed types).
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

// Pull `key` out of the VM Value stored at handle `h` and return it as a
// (tag, bits) pair. Used by the tag-7 dispatch path in compiled code.
JDRT_API int32_t jdrt_obj_get_tagged(JdRT handle, int64_t h, const char* key, int64_t* out_val) {
    *out_val = 0;
    auto* rt = (JdRTImpl*)handle;
    const Value* v = obj_field(rt, h, key);
    if (!v) return 0;
    union { double d; int64_t i; } u;
    switch (v->type) {
        case ValueType::STRING:
            *out_val = (int64_t)(intptr_t)_strdup(v->as_string()->data.c_str());
            return jd_tag(JdTag::STR);
        case ValueType::ARRAY: {
            // Pure-numeric arrays convert cheaply to a flat JdbArray; mixed
            // arrays need a VM handle so per-element typed access works.
            auto* arr = v->as_array();
            bool pure_numeric = true;
            for (auto& e : arr->elements) {
                if (e.type == ValueType::STRING || e.type == ValueType::OBJECT ||
                    e.type == ValueType::ARRAY) {
                    pure_numeric = false; break;
                }
            }
            if (pure_numeric) {
                *out_val = (int64_t)(intptr_t)value_to_jdbarray(*v);
                return jd_tag(JdTag::ARR);
            }
            *out_val = rt->store_value(*v);
            return jd_tag(JdTag::VM_HANDLE);
        }
        case ValueType::OBJECT:
            *out_val = rt->store_value(*v);
            return jd_tag(JdTag::VM_HANDLE);
        default:
            u.d = v->to_double();
            *out_val = u.i;
            return jd_tag(JdTag::F64);
    }
}

// Must stay layout-compatible with JdbMap in jdb_runtime.cpp.
struct JdbMap {
    int64_t count, capacity;
    char** keys;
    double* values;
    int32_t* tags;
};

// Entry point for tag-7 INDEX dispatch on a key. val_bits is either a VM
// value_store key or a native JdbMap* depending on val_tag; this function
// dispatches to the right getter and returns the field's runtime tag.
// Duplicates jdb_map_get_tagged's read-path rather than cross-linking
// (bridge and native runtime are separate DLLs).
JDRT_API int32_t jdrt_tagged_get(JdRT handle, int64_t val_bits, int32_t val_tag,
                                  const char* key, int64_t* out_val) {
    *out_val = 0;
    if (val_tag == jd_tag(JdTag::VM_HANDLE)) {
        return jdrt_obj_get_tagged(handle, val_bits, key, out_val);
    }
    auto* m = (JdbMap*)(intptr_t)val_bits;
    if (!m) return 0;
    int64_t idx = -1;
    for (int64_t i = 0; i < m->count; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0) { idx = i; break; }
    }
    if (idx < 0) return 0;
    union { double d; int64_t i; } u;
    u.d = m->values[idx];
    int32_t t = m->tags[idx];
    if (t == jd_tag(JdTag::STR)) {
        const char* s = (const char*)(intptr_t)u.i;
        *out_val = (int64_t)(intptr_t)_strdup(s ? s : "");
        return jd_tag(JdTag::STR);
    }
    *out_val = u.i;
    // Preserve pointer-ish tags; anything else is treated as f64-in-bits.
    return (t == jd_tag(JdTag::ARR) || t == jd_tag(JdTag::NATIVE_MAP))
               ? t : jd_tag(JdTag::F64);
}

// Entry point for tag-7 INDEX dispatch on an integer index. For VM arrays
// the result is itself a VM handle (so the caller can keep drilling with
// typed access); for native arrays every element is f64.
JDRT_API int32_t jdrt_tagged_arr_get(JdRT handle, int64_t val_bits, int32_t val_tag,
                                      int64_t idx, int64_t* out_val) {
    *out_val = 0;
    if (val_tag == jd_tag(JdTag::VM_HANDLE)) {
        *out_val = jdrt_val_arr_get(handle, val_bits, idx);
        return (*out_val != 0) ? jd_tag(JdTag::VM_HANDLE) : 0;
    }
    auto* arr = (JdbArray*)(intptr_t)val_bits;
    if (!arr || idx < 0 || idx >= arr->length) return 0;
    union { double d; int64_t i; } u;
    u.d = arr->data[idx];
    *out_val = u.i;
    return jd_tag(JdTag::F64);
}

// Deferred from the obj_get_* block because value_to_jdbarray and the
// JdbArray layout aren't in scope up there.
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
