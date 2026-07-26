// vm_bridge.cpp - C-linkage bridge to the jdBasic VM for native executables.
// Compiled into jdbrt.dll. Wraps VM::call_function() with a simple C API.

#include "vm.h"
#include "vm_bridge.h"
#include "jdb_tags.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "async_task.h"
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>

// MSVC's _strdup is POSIX strdup with an underscore prefix. Map them so
// the call sites stay portable.
#if !defined(_WIN32)
#define _strdup strdup
#endif

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
extern void register_numerics_builtins(VM& vm);
extern void register_llm_builtins(VM& vm);
#ifdef SQLITE
extern void register_sql_builtins(VM& vm);
#endif
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
#ifdef OPENGL
extern void register_opengl_builtins(VM& vm);
#endif
#ifdef IMGUI
extern void register_gui_builtins(VM& vm);
#endif
#ifdef TUI
extern void register_tui_natives(VM& vm);
#endif

// DLL-local base directory for module imports. Non-static so graphics.cpp
// can extern-link it for asset-path resolution.
std::string g_base_dir = ".";

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
        // Exactly one level down into a sibling "modules/" subdir.
        // Mirror main.cpp's resolver - no walk-up. The script's own dir
        // (or a direct `modules/` subdir) is the only place we look.
        candidates.push_back(g_base_dir + "/modules/" + module_name + ".jdb");
        candidates.push_back(g_base_dir + "/modules/" + lower + ".jdb");
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
#ifdef OPENGL
    register_opengl_builtins(vm);
#endif
#ifdef IMGUI
    register_gui_builtins(vm);
#endif
#ifdef TUI
    register_tui_natives(vm);
#endif
    register_sound_builtins(vm);
    register_ffi_builtins(vm);
    register_ai_builtins(vm);
    register_numerics_builtins(vm);
    register_llm_builtins(vm);
#ifdef SQLITE
    register_sql_builtins(vm);
#endif
}

// One VM instance per jdrt_init() caller, plus the side tables used to
// translate between VM Values and the integer handles the native-compiled
// code can store in i64 variables.
struct JdRTImpl {
    VM vm;
    std::string last_error;
    std::unordered_map<int64_t, Value> value_store;
    int64_t next_handle = 1;            // positive - frame-temp, swept
    int64_t next_persistent = -1;       // negative - persistent, never swept
    JdrtEventDispatch user_event_dispatch = nullptr;

    int64_t store_value(Value v) {
        int64_t h = next_handle++;
        value_store[h] = std::move(v);
        return h;
    }

    // Promote a frame-temp value to a persistent key so it survives
    // jdrt_frame_end sweeps. Used when a VM handle is stored into a
    // long-lived container (NATIVE_MAP, ARRAY) - without this the
    // sweep at the end of the enclosing DO-loop iteration would erase
    // the value, and the next iteration's read would fault.
    int64_t store_persistent(Value v) {
        int64_t h = next_persistent--;
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

// A compiled program calls jdrt_init() once and keeps the handle in a module
// global, so every ASYNC task reaches the bridge through it. JdRTImpl holds a
// VM, a value_store and a non-atomic next_handle, none of which is safe to
// share across threads, so each thread gets its own instance.
//
// The bridge VM carries builtins only (no user functions, no globals), which
// makes a per-thread instance equivalent, and no handle crosses a thread:
// whichever thread stores a Value is the one that reads it back.
static JdRTImpl* g_primary_rt = nullptr;
static std::thread::id g_primary_thread;

// Deliberately leaked: a thread_local with a non-trivial destructor is torn
// down inside DLL unload, where the VM's own statics may already be gone.
// A worker's bridge state lives as long as its thread and the process ends
// shortly after, so the leak is bounded by the number of ASYNC tasks.
static JdRTImpl* thread_rt() {
    static thread_local JdRTImpl* tls = nullptr;
    if (!tls) {
        tls = new JdRTImpl();
        setup_all_builtins(tls->vm);
        // Events raised on a worker still reach the program's handlers.
        if (g_primary_rt && g_primary_rt->user_event_dispatch)
            tls->vm.user_event_dispatch = g_primary_rt->vm.user_event_dispatch;
    }
    return tls;
}

// Entry points take the handle the compiled code holds; on the thread that
// created it that IS the right state, on any other thread it is shared
// mutable state and gets replaced by the caller's own.
static inline JdRTImpl* resolve_rt(JdRT handle) {
    auto* rt = (JdRTImpl*)handle;
    if (rt == g_primary_rt && std::this_thread::get_id() != g_primary_thread)
        return thread_rt();
    return rt;
}

JDRT_API JdRT jdrt_init(void) {
    auto* rt = new JdRTImpl();
    setup_all_builtins(rt->vm);
    if (!g_primary_rt) {
        g_primary_rt = rt;
        g_primary_thread = std::this_thread::get_id();
    }
    return (JdRT)rt;
}

JDRT_API void jdrt_shutdown(JdRT handle) {
    delete (JdRTImpl*)handle;
}

JDRT_API double jdrt_call_f64(JdRT handle, const char* name,
                               const double* args, int nargs) {
    auto* rt = resolve_rt(handle);
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
    auto* rt = resolve_rt(handle);
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
    auto* rt = resolve_rt(handle);
    try {
        auto vargs = args_to_values(args, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        // Borrow the ArrayObj* - owned by the VM, caller must not free.
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
    auto* rt = resolve_rt(handle);
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
// bit 3 (= 8) marks per-element tag arrays added 2026-05-04 for ARRAY_LITERAL
// containing map-lookup elements; elem_tags is allocated only when bit 3 set.
struct JdbArrayFwd {
    double* data;
    int64_t length;
    int32_t flags;
    int8_t* elem_tags;
};

// flags bit 0: element doubles are pointers punned as f64.
// flags bit 1: those pointers are char* strings (else JdbArray* nested).
// flags bit 2: elements are bool (TRUE/FALSE rendering vs 1/0).
// flags bit 3: per-element JdTag carried in elem_tags (mixed-type literals).
// Heuristic: distinguish a real f64 number from an f64-punned pointer.
// Userspace pointers on Linux/Windows x86_64 sit below 2^47. Any finite
// f64 with non-zero magnitude has its exponent bits set high enough that
// the raw bit pattern is at least 2^52, well above the pointer range.
// Used by jdbarray_to_value to walk mixed-type arrays where the JdbArray
// flags say "has pointers" but individual cells may carry either kind.
static inline bool bits_look_like_ptr(double d) {
    union { double d; uint64_t u; } u; u.d = d;
    return u.u != 0 && u.u < (1ULL << 47);
}

struct JdbMapFwd;
static Value jdbmap_to_value(JdbMapFwd* m);

static Value jdbarray_to_value(JdbArrayFwd* arr) {
    if (!arr) return Value::make_array();
    Value r = Value::make_array();
    auto* out = r.as_array();
    out->elements.reserve(arr->length);
    bool has_tagged = (arr->flags & 8) != 0 && arr->elem_tags != nullptr;
    bool has_ptr = (arr->flags & 1) != 0;
    bool has_string = (arr->flags & 2) != 0;
    bool has_bool = (arr->flags & 4) != 0;
    for (int64_t i = 0; i < arr->length; i++) {
        double d = arr->data[i];
        if (has_tagged) {
            // Per-cell tags from the tagged ARRAY_LITERAL path. This is
            // what makes vo's GUI.COMBO pick up the actual string items
            // when its third arg is a map-stored array of mixed type.
            int8_t t = arr->elem_tags[i];
            union { double d; int64_t i; } u; u.d = d;
            if (t == jd_tag(JdTag::STR)) {
                const char* p = (const char*)(intptr_t)u.i;
                out->elements.push_back(p ? Value::make_string(p) : Value::make_none());
            } else if (t == jd_tag(JdTag::ARR)) {
                JdbArrayFwd* inner = (JdbArrayFwd*)(intptr_t)u.i;
                out->elements.push_back(inner ? jdbarray_to_value(inner) : Value::make_none());
            } else if (t == jd_tag(JdTag::BOOL)) {
                out->elements.push_back(Value::make_bool(d != 0.0));
            } else if (t == jd_tag(JdTag::NATIVE_MAP)) {
                JdbMapFwd* m = (JdbMapFwd*)(intptr_t)u.i;
                out->elements.push_back(m ? jdbmap_to_value(m) : Value::make_none());
            } else {
                // F64 / I64 - numeric.
                out->elements.push_back(Value::make_f64(d));
            }
            continue;
        }
        if (has_ptr && bits_look_like_ptr(d)) {
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
        } else if (has_bool) {
            // Comparison-result array (flags bit2): cells are 0/1 booleans.
            out->elements.push_back(Value::make_bool(d != 0.0));
        } else {
            // Either uniform f64 array, or a numeric cell inside a mixed-
            // type literal like [1, "x", 3] - the bit pattern reveals it
            // as a real number despite the array-level has_ptr flag.
            out->elements.push_back(Value::make_f64(d));
        }
    }
    return r;
}

// Decode the {args, tags} wire format into VM Values. Only the tags that
// appear on the wire are handled here - NATIVE_MAP never crosses the
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
                // Wire isn't supposed to carry NATIVE_MAP - codegen
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
            case JdTag::BOOL: {
                // Wire carries the bool as a 0/1 integer. Without this case it
                // fell through to default and memcpy'd the i64 bits into a
                // double (1 -> the denormal 5e-324), not the boolean.
                vargs.push_back(Value::make_bool(args[i] != 0));
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
    auto* rt = resolve_rt(handle);
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->vm.event_poll();
        rt->last_error.clear();
        return result.to_double();
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        return 0.0;
    }
}

static void register_binary_string(const char* s, size_t n) {
    if (!s || n == 0) return;
    // Plain C strings don't need an entry - strlen already gets it right.
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

// Exposed so the jdb_runtime helpers (MID$, LEFT$, RIGHT$, REPLACE$, ...)
// can record their own result lengths when they produce buffers that
// contain embedded NULs. Without this, slicing a binary buffer (BINREADER$
// content, PACK$ output) loses the length the moment it passes through
// MID$ - downstream BYTEAT/MID$/INSTR calls would then trip over the
// strlen fallback at the first 0x00 byte.
JDRT_API void jdrt_register_binary(const char* s, int64_t n) {
    if (!s || n <= 0) return;
    if (strlen(s) == (size_t)n) return;
    std::lock_guard<std::mutex> lk(bin_mx());
    bin_lens()[(const void*)s] = (size_t)n;
}

JDRT_API char* jdrt_call_typed_str(JdRT handle, const char* name,
                                    const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = resolve_rt(handle);
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->vm.event_poll();
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
    auto* rt = resolve_rt(handle);
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        rt->vm.call_function(name, vargs);
        rt->vm.event_poll();
        rt->last_error.clear();
    } catch (const std::exception& e) {
        rt->last_error = e.what();
    }
}

JDRT_API int64_t jdrt_call_typed_obj(JdRT handle, const char* name,
                                     const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = resolve_rt(handle);
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
    auto* rt = resolve_rt(handle);
    rt->value_store.erase(val_handle);
}

// Snapshot next_handle at loop start; anything allocated between begin
// and end is a frame-local temporary and gets swept at end so
// value_store doesn't grow unbounded across frames.
JDRT_API int64_t jdrt_frame_begin(JdRT handle) {
    auto* rt = resolve_rt(handle);
    return rt->next_handle;
}

JDRT_API void jdrt_frame_end(JdRT handle, int64_t watermark) {
    auto* rt = resolve_rt(handle);
    for (auto it = rt->value_store.begin(); it != rt->value_store.end(); ) {
        // Negative keys are persistent (promoted via jdrt_promote_handle)
        // and must survive the per-iteration sweep - they're held by
        // long-lived containers like vstate{...}.
        if (it->first >= watermark && it->first > 0)
            it = rt->value_store.erase(it);
        else
            ++it;
    }
}

// Re-store a frame-temp handle's Value at a persistent (negative) key
// and return the new key. The original temp key is left in place - the
// next jdrt_frame_end will sweep it. Idempotent on already-persistent
// handles (returns the input key unchanged).
JDRT_API int64_t jdrt_promote_handle(JdRT handle, int64_t h) {
    auto* rt = resolve_rt(handle);
    if (h <= 0) return h;
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end()) return h;
    return rt->store_persistent(it->second);
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
    auto* rt = resolve_rt(handle);
    const Value* v = obj_field(rt, h, key);
    return v ? v->to_double() : 0.0;
}

JDRT_API const char* jdrt_obj_get_str(JdRT handle, int64_t h, const char* key) {
    auto* rt = resolve_rt(handle);
    const Value* v = obj_field(rt, h, key);
    if (!v) return _strdup("");
    if (v->type == ValueType::STRING) return _strdup(v->as_string()->data.c_str());
    return _strdup(v->to_string().c_str());
}

JDRT_API int64_t jdrt_obj_get_obj(JdRT handle, int64_t h, const char* key) {
    auto* rt = resolve_rt(handle);
    const Value* v = obj_field(rt, h, key);
    if (!v) return 0;
    // Store every non-missing field - including scalars - so the caller
    // always gets a live handle. Scalars materialise via the val_to_*
    // coerce paths; returning 0 for a string-typed scalar would break
    // patterns like `title = g{"title"}`.
    return rt->store_value(*v);
}

JDRT_API int64_t jdrt_obj_exists(JdRT handle, int64_t h, const char* key) {
    auto* rt = resolve_rt(handle);
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end() || it->second.type != ValueType::OBJECT)
        return 0;
    auto* m = it->second.as_object();
    return m->get(std::string(key ? key : "")) ? 1 : 0;
}

// Must stay layout-compatible with JdbArray in jdb_runtime.cpp.
// elem_tags added 2026-05-04 - non-null when flags & 8.
struct JdbArray {
    double* data;
    int64_t length;
    int32_t flags;
    int8_t* elem_tags;
};

// Inverse of jdbarray_to_value: walk a VM array, copy its numeric cells
// verbatim and pun inner strings/arrays as pointers into the data[]
// slots. flags records which punning the decoder should do.
static JdbArray* value_to_jdbarray(const Value& v) {
    auto* r = (JdbArray*)malloc(sizeof(JdbArray));
    r->data = nullptr;
    r->length = 0;
    r->flags = 0;
    r->elem_tags = nullptr;
    if (v.type != ValueType::ARRAY) return r;
    auto* arr = v.as_array();
    r->length = (int64_t)arr->elements.size();
    r->data = (double*)calloc(r->length > 0 ? r->length : 1, sizeof(double));
    bool has_ptr = false, has_string = false, has_other = false;
    std::vector<int8_t> cell_tags((size_t)(r->length > 0 ? r->length : 1), 1);
    for (int64_t i = 0; i < r->length; i++) {
        const auto& e = arr->elements[i];
        if (e.type == ValueType::ARRAY) {
            has_ptr = true; has_other = true;
            cell_tags[(size_t)i] = 3;  // JD_TAG_ARR
            JdbArray* inner = value_to_jdbarray(e);
            union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)inner;
            r->data[i] = u.d;
        } else if (e.type == ValueType::STRING) {
            has_ptr = true; has_string = true;
            cell_tags[(size_t)i] = 2;  // JD_TAG_STR
            const std::string& s = e.as_string()->data;
            char* copy = _strdup(s.c_str());
            union { int64_t i; double d; } u; u.i = (int64_t)(intptr_t)copy;
            r->data[i] = u.d;
        } else {
            has_other = true;
            r->data[i] = e.to_double();
        }
    }
    if (has_ptr) r->flags |= 1;
    if (has_string) r->flags |= 2;
    // String cells mixed with anything else make flags-only decoding
    // ambiguous (a numeric cell in a string-flagged row would be
    // dereferenced as char*) - per-element tags pin the layout for those.
    // Uniform arrays keep the plain flags encoding.
    if (has_string && has_other) {
        r->elem_tags = (int8_t*)malloc((size_t)(r->length > 0 ? r->length : 1));
        memcpy(r->elem_tags, cell_tags.data(), (size_t)(r->length > 0 ? r->length : 1));
        r->flags |= 8;
    }
    return r;
}

// Layout-compatible mirror of JdbMap from jdb_runtime.cpp. Used to
// box a native JdbMap* into a Value::OBJECT before crossing the bridge,
// so generic native CALLs that accept arbitrary Values (e.g. CHAN.SEND)
// can store the map under VM_HANDLE without losing the contents.
struct JdbMapFwd {
    int64_t  count;
    int64_t  capacity;
    char**   keys;
    double*  values;
    int32_t* tags;
};

// Walk a native JdbMap*, build a fresh Value::OBJECT mirroring its
// entries. Recurses into nested arrays and maps so array-of-map and
// map-of-map values marshal faithfully. Caller retains ownership of
// the JdbMap. Tag values follow JdTag enum.
static Value jdbmap_to_value(JdbMapFwd* m) {
    Value v = Value::make_object();
    if (!m) return v;
    auto* obj = v.as_object();
    for (int64_t i = 0; i < m->count; i++) {
        std::string k = m->keys[i] ? m->keys[i] : "";
        int32_t t = m->tags ? m->tags[i] : 0; // F64 default
        double  d = m->values[i];
        Value cell;
        switch (static_cast<JdTag>(t)) {
            case JdTag::I64: {
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                cell = Value::make_i64(bits);
                break;
            }
            case JdTag::STR: {
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                const char* s = (const char*)(intptr_t)bits;
                cell = Value::make_string(s ? s : "");
                break;
            }
            case JdTag::ARR: {
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                cell = jdbarray_to_value((JdbArrayFwd*)(intptr_t)bits);
                break;
            }
            case JdTag::NATIVE_MAP: {
                int64_t bits;
                memcpy(&bits, &d, sizeof(bits));
                cell = jdbmap_to_value((JdbMapFwd*)(intptr_t)bits);
                break;
            }
            case JdTag::F64:
            default:
                cell = Value::make_f64(d);
                break;
        }
        obj->set(k, std::move(cell));
    }
    return v;
}

// Box a native JdbMap* into a Value::OBJECT, store it in value_store,
// return the new handle. Caller retains ownership of the JdbMap.
JDRT_API int64_t jdrt_map_to_handle(JdRT handle, void* m_ptr) {
    auto* rt = resolve_rt(handle);
    Value v = jdbmap_to_value(m_ptr ? (JdbMapFwd*)m_ptr : nullptr);
    return rt->store_value(std::move(v));
}

// ── ASYNC FUNC dispatch from native code ───────────────────────
//
// Native compile emits each user FUNC as a real LLVM Function - they
// never reach the runtime VM's func_map, so the OpCode::CALL ASYNC
// path inside vm.cpp can't help. Instead, codegen synthesises a
// uniform funcref wrapper for the target FUNC (f64 args, f64 return,
// see build_funcref_wrapper) and calls jdrt_async_spawn with the
// wrapper pointer. This routine spawns the thread, invokes the
// wrapper with the packed args, repacks the f64 result into a Value
// based on the user's declared return tag, and returns a task id
// the caller can AWAIT through the existing g_async_tasks registry.
// async_task.h declares g_async_tasks / g_async_mutex / g_async_next_id
// (defined in vm.cpp).

JDRT_API int64_t jdrt_async_spawn(JdRT handle, void* fn_ptr,
                                   const double* args, int nargs,
                                   int return_tag) {
    int task_id = g_async_next_id++;
    auto task = std::make_shared<AsyncTask>();
    std::vector<double> args_copy(args, args + nargs);
    int rtag = return_tag;

    task->thread = std::thread([fn_ptr, args_copy, rtag, task]() {
        double r = 0.0;
        try {
            const auto& A = args_copy;
            switch ((int)A.size()) {
                case 0: r = ((double(*)())fn_ptr)(); break;
                case 1: r = ((double(*)(double))fn_ptr)(A[0]); break;
                case 2: r = ((double(*)(double,double))fn_ptr)(A[0],A[1]); break;
                case 3: r = ((double(*)(double,double,double))fn_ptr)(A[0],A[1],A[2]); break;
                case 4: r = ((double(*)(double,double,double,double))fn_ptr)(A[0],A[1],A[2],A[3]); break;
                case 5: r = ((double(*)(double,double,double,double,double))fn_ptr)(A[0],A[1],A[2],A[3],A[4]); break;
                case 6: r = ((double(*)(double,double,double,double,double,double))fn_ptr)(A[0],A[1],A[2],A[3],A[4],A[5]); break;
                case 7: r = ((double(*)(double,double,double,double,double,double,double))fn_ptr)(A[0],A[1],A[2],A[3],A[4],A[5],A[6]); break;
                case 8: r = ((double(*)(double,double,double,double,double,double,double,double))fn_ptr)(A[0],A[1],A[2],A[3],A[4],A[5],A[6],A[7]); break;
                default:
                    throw std::runtime_error(
                        "ASYNC FUNC native dispatch supports up to 8 args");
            }
            Value result;
            int64_t bits;
            memcpy(&bits, &r, sizeof(bits));
            switch (rtag) {
                case JD_TAG_I64:
                case JD_TAG_BOOL:
                    result = Value::make_i64(bits);
                    break;
                case JD_TAG_STR: {
                    const char* s = (const char*)(intptr_t)bits;
                    result = Value::make_string(s ? s : "");
                    break;
                }
                case JD_TAG_ARR: {
                    JdbArrayFwd* arr = (JdbArrayFwd*)(intptr_t)bits;
                    result = jdbarray_to_value(arr);
                    break;
                }
                case -1: // SUB return - discard
                    result = Value::make_none();
                    break;
                case JD_TAG_F64:
                default:
                    result = Value::make_f64(r);
                    break;
            }
            task->result = std::move(result);
        } catch (const std::exception& e) {
            task->result = Value::make_string("ERROR: " + std::string(e.what()));
        }
        task->done = true;
    });
    task->thread.detach();

    { std::lock_guard<std::mutex> lock(g_async_mutex);
      g_async_tasks[task_id] = task; }
    (void)handle;
    return task_id;
}

// Materialise a handle as a concrete scalar. Numeric values round-trip
// exactly; Maps/Arrays fall back to to_double()==0 and to_string()'s
// formatted dump - callers that care must check the type first.
JDRT_API double jdrt_val_to_f64(JdRT handle, int64_t h) {
    auto* rt = resolve_rt(handle);
    auto it = rt->value_store.find(h);
    return (it != rt->value_store.end()) ? it->second.to_double() : 0.0;
}

JDRT_API const char* jdrt_val_to_str(JdRT handle, int64_t h) {
    auto* rt = resolve_rt(handle);
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end()) return _strdup("");
    if (it->second.type == ValueType::STRING)
        return _strdup(it->second.as_string()->data.c_str());
    return _strdup(it->second.to_string().c_str());
}

// Returns a fresh handle for the element so the caller sees the real
// element type on subsequent ops (needed for arrays of mixed types).
JDRT_API int64_t jdrt_val_arr_get(JdRT handle, int64_t h, int64_t idx) {
    auto* rt = resolve_rt(handle);
    auto it = rt->value_store.find(h);
    if (it == rt->value_store.end() || it->second.type != ValueType::ARRAY) return 0;
    auto* arr = it->second.as_array();
    if (idx < 0 || (size_t)idx >= arr->elements.size()) return 0;
    return rt->store_value(arr->elements[(size_t)idx]);
}

JDRT_API int64_t jdrt_val_length(JdRT handle, int64_t h) {
    auto* rt = resolve_rt(handle);
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
    auto* rt = resolve_rt(handle);
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
        case ValueType::INT64:
            // Preserve the integer tag so `TYPEOF(row{"id"}) = "INT64"`
            // matches in native - vo's load_form() guards `IF TYPEOF(
            // row{"vertreter_id"}) = "INT64" THEN ...` and silently
            // falls through to the default -1 if we collapse INT to FLOAT.
            *out_val = v->to_int();
            return jd_tag(JdTag::I64);
        case ValueType::BOOLEAN:
            *out_val = v->to_int() ? 1 : 0;
            return jd_tag(JdTag::BOOL);
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
    // Per-element tags (flags bit 3) win - they're set by the tagged
    // ARRAY_LITERAL path for `[m{"name"}, m{"age"}, ...]` so each cell
    // carries its real JdTag. Falls back to the all-elements-string flag
    // (bit 1) and finally to F64 for pure-numeric arrays.
    if ((arr->flags & 8) && arr->elem_tags) {
        return (int32_t)arr->elem_tags[idx];
    }
    if (arr->flags & 1) return jd_tag(JdTag::STR);
    return jd_tag(JdTag::F64);
}

// Deferred from the obj_get_* block because value_to_jdbarray and the
// JdbArray layout aren't in scope up there.
JDRT_API void* jdrt_obj_get_arr(JdRT handle, int64_t h, const char* key) {
    auto* rt = resolve_rt(handle);
    const Value* v = obj_field(rt, h, key);
    if (!v) {
        auto* r = (JdbArray*)malloc(sizeof(JdbArray));
        r->data = nullptr; r->length = 0; r->flags = 0; r->elem_tags = nullptr;
        return r;
    }
    return value_to_jdbarray(*v);
}

// Returns a JdbArray* for functions that return arrays (SPLIT, KEYS, etc.)
JDRT_API void* jdrt_call_typed_arr(JdRT handle, const char* name,
                                    const int64_t* args, const int32_t* tags, int nargs) {
    auto* rt = resolve_rt(handle);
    try {
        auto vargs = typed_args_to_values(rt, args, tags, nargs);
        Value result = rt->vm.call_function(name, vargs);
        rt->last_error.clear();
        return value_to_jdbarray(result);
    } catch (const std::exception& e) {
        rt->last_error = e.what();
        auto* r = (JdbArray*)malloc(sizeof(JdbArray));
        r->data = nullptr; r->length = 0; r->flags = 0; r->elem_tags = nullptr;
        return r;
    }
}

JDRT_API const char* jdrt_last_error(JdRT handle) {
    auto* rt = resolve_rt(handle);
    return rt->last_error.empty() ? nullptr : rt->last_error.c_str();
}

JDRT_API void jdrt_set_event_dispatcher(JdRT handle, JdrtEventDispatch fn) {
    auto* rt = (JdRTImpl*)handle;
    rt->user_event_dispatch = fn;
    // Wire it into the VM so that event_raise in vm.cpp picks the
    // user dispatcher whenever a handler is registered.
    rt->vm.user_event_dispatch =
        [fn, rt](const std::string& name, const std::vector<Value>& data) {
            if (!fn) return;

            // Flatten: scalar Values pass through as one slot; OBJECT
            // values get expanded into one slot per field, in the order
            // event_poll() built them. The .exe-side trampoline
            // (jdrt_dispatch_event) reads slots positionally per event
            // schema (KEYDOWN: scancode, keycode, key, repeat).
            std::vector<int64_t> args;
            std::vector<int32_t> tags;
            args.reserve(8);
            tags.reserve(8);

            auto push_value = [&](const Value& v) {
                switch (v.type) {
                    case ValueType::INT64:
                    case ValueType::INT32:
                    case ValueType::INT16:
                    case ValueType::BYTE:
                    case ValueType::BOOLEAN:
                        args.push_back(v.to_int());
                        tags.push_back(JD_TAG_I64);
                        break;
                    case ValueType::FLOAT64:
                    case ValueType::FLOAT32:
                    case ValueType::FLOAT16: {
                        double d = v.to_double();
                        int64_t bits;
                        memcpy(&bits, &d, sizeof(double));
                        args.push_back(bits);
                        tags.push_back(JD_TAG_F64);
                        break;
                    }
                    case ValueType::STRING:
                        args.push_back((int64_t)(intptr_t)
                            (v.as_string() ? v.as_string()->data.c_str() : ""));
                        tags.push_back(JD_TAG_STR);
                        break;
                    default:
                        args.push_back(v.to_int());
                        tags.push_back(JD_TAG_I64);
                        break;
                }
            };

            for (const Value& v : data) {
                if (v.type == ValueType::OBJECT && v.as_object()) {
                    for (auto& field : v.as_object()->fields) {
                        push_value(field.second);
                    }
                } else {
                    push_value(v);
                }
            }
            fn(name.c_str(), args.data(), tags.data(), (int)args.size());
        };
}

JDRT_API void jdrt_clear_last_error(JdRT handle) {
    auto* rt = resolve_rt(handle);
    rt->last_error.clear();
}

} // extern "C"
