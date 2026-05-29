// jdb_embed_api.cpp - thin C-ABI for embedding the jdBasic VM in a host
// application (Godot GDExtension is the first consumer).
//
// MVP: init / shutdown / eval / load + last-error + free. Everything runs
// on the calling thread for now. Day 2 of the spike will lift the worker /
// stop / resume / recompile primitives out of mcp_stdio.cpp into something
// reusable here.

#define JDRT_EXPORTS

#include "jdb_embed_api.h"
#include "vm.h"
#include "value.h"
#include "lexer.h"
#include "parser.h"
#include "compiler.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {

struct JdbEmbedImpl {
    VM vm;
    std::string output_buf;
    std::string last_error;

    // E3 - per-VM value store backing the typed-handle accessors.
    // Handles are int64 keys (1-based; 0 is sentinel "no value").
    // Strings returned from jdb_embed_value_string are owned by these
    // Value copies and stay valid until release.
    std::unordered_map<int64_t, Value> value_store;
    int64_t next_handle = 1;

    int64_t store(Value v) {
        int64_t h = next_handle++;
        value_store.emplace(h, std::move(v));
        return h;
    }
    const Value* lookup(int64_t h) const {
        if (!h) return nullptr;
        auto it = value_store.find(h);
        return (it == value_store.end()) ? nullptr : &it->second;
    }
};

void run_source(VM& vm, const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    Compiler c;
    c.compile(ast);
    vm.run_code(c.main_chunk(), c.functions());
}

// Recompile pattern: lex / parse / compile, then merge_funcs into the
// running VM. The main chunk (top-level DIM statements etc.) is dropped
// on purpose - the running script is mid-flight, retro-actively re-running
// its boot code would clobber state. This is exactly the path the MCP
// jdb_recompile tool uses, mirrored here for embedders.
std::string recompile_source(VM& vm, const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto ast = parser.parse();
    Compiler c;
    c.compile(ast);
    auto& fns = c.functions();
    auto [added, updated] = vm.merge_funcs(fns);
    return "added=" + std::to_string(added) + " updated=" + std::to_string(updated);
}

void setup(JdbEmbedImpl* e) {
    e->vm.on_output = [e](const std::string& s) {
        e->output_buf += s;
    };
    e->vm.on_execute = [](VM& v, const std::string& code) {
        std::string src = code;
        if (src.empty() || src.back() != '\n') src += '\n';
        run_source(v, src);
    };
    e->vm.on_check = [](VM&, const std::string& code) -> std::string {
        try {
            Lexer lexer(code + "\n");
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            (void)parser.parse();
            return "";
        } catch (const std::exception& ex) {
            return ex.what();
        }
    };
}

char* dup_cstr(const std::string& s) {
    char* p = (char*)std::malloc(s.size() + 1);
    if (!p) return nullptr;
    std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    return p;
}

}  // namespace

extern "C" {

JDB_EMBED_API JdbEmbed* jdb_embed_init(void) {
    auto* e = new (std::nothrow) JdbEmbedImpl();
    if (!e) return nullptr;
    setup(e);
    return reinterpret_cast<JdbEmbed*>(e);
}

JDB_EMBED_API void jdb_embed_shutdown(JdbEmbed* e) {
    delete reinterpret_cast<JdbEmbedImpl*>(e);
}

JDB_EMBED_API char* jdb_embed_eval(JdbEmbed* eh, const char* code) {
    if (!eh || !code) return nullptr;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    try {
        std::string src = code;
        if (src.empty() || src.back() != '\n') src += '\n';
        run_source(e->vm, src);
        return dup_cstr(e->output_buf);
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return nullptr;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_eval";
        return nullptr;
    }
}

JDB_EMBED_API char* jdb_embed_load(JdbEmbed* eh, const char* path) {
    if (!eh || !path) return nullptr;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    std::ifstream in(path);
    if (!in.is_open()) {
        e->last_error = std::string("Cannot read ") + path;
        return nullptr;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        run_source(e->vm, ss.str());
        return dup_cstr(e->output_buf);
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return nullptr;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_load";
        return nullptr;
    }
}

JDB_EMBED_API char* jdb_embed_recompile_source(JdbEmbed* eh, const char* source) {
    if (!eh || !source) return nullptr;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    try {
        std::string summary = recompile_source(e->vm, source);
        return dup_cstr(summary);
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return nullptr;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_recompile_source";
        return nullptr;
    }
}

JDB_EMBED_API char* jdb_embed_recompile(JdbEmbed* eh, const char* path) {
    if (!eh || !path) return nullptr;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    std::ifstream in(path);
    if (!in.is_open()) {
        e->last_error = std::string("Cannot read ") + path;
        return nullptr;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        std::string summary = recompile_source(e->vm, ss.str());
        return dup_cstr(summary);
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return nullptr;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_recompile";
        return nullptr;
    }
}

JDB_EMBED_API const char* jdb_embed_last_error(JdbEmbed* eh) {
    if (!eh) return "null embed handle";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    return e->last_error.empty() ? nullptr : e->last_error.c_str();
}

JDB_EMBED_API void jdb_embed_free(char* s) {
    std::free(s);
}

// ── E3: typed value handles ────────────────────────────────────────
//
// Helpers below are static C++ functions inside the surrounding extern
// "C" block - an explicit anonymous namespace would be ill-formed here.

// jdBasic identifiers are case-insensitive; globals are stored upper-cased.
static std::string upper_(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c){ return (char)std::toupper(c); });
    return r;
}

static int tag_for_(const Value& v) {
    switch (v.type) {
        case ValueType::NONE:    return JDB_T_NONE;
        case ValueType::BOOLEAN: return JDB_T_BOOL;
        case ValueType::BYTE:
        case ValueType::INT16:
        case ValueType::INT32:
        case ValueType::INT64:   return JDB_T_INT;
        case ValueType::FLOAT16:
        case ValueType::FLOAT32:
        case ValueType::FLOAT64: return JDB_T_DOUBLE;
        case ValueType::STRING:  return JDB_T_STRING;
        case ValueType::OBJECT:  return JDB_T_OBJECT;
        case ValueType::ARRAY:   return JDB_T_ARRAY;
        case ValueType::TENSOR:  return JDB_T_ARRAY;  // tensors marshall as arrays for E3
        default:                 return JDB_T_NONE;
    }
}

JDB_EMBED_API JdbValue jdb_embed_eval_expr(JdbEmbed* eh, const char* expr) {
    if (!eh || !expr) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    // Wrap as a temp-global assignment so we can fish the value back
    // out without depending on PRINT formatting.
    std::string code = "__JDB_EMBED_RET = (";
    code += expr;
    code += ")\n";
    try {
        run_source(e->vm, code);
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return 0;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_eval_expr";
        return 0;
    }
    const auto& names = e->vm.get_global_names();
    auto it = names.find("__JDB_EMBED_RET");
    if (it == names.end()) {
        e->last_error = "expr evaluated but result global not captured";
        return 0;
    }
    const auto& globals = e->vm.get_globals();
    if (it->second >= globals.size()) return 0;
    return e->store(globals[it->second]);
}

JDB_EMBED_API JdbValue jdb_embed_get_global(JdbEmbed* eh, const char* name) {
    if (!eh || !name) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    std::string up = upper_(name);
    const auto& names = e->vm.get_global_names();
    auto it = names.find(up);
    if (it == names.end()) return 0;
    const auto& globals = e->vm.get_globals();
    if (it->second >= globals.size()) return 0;
    return e->store(globals[it->second]);
}

JDB_EMBED_API int jdb_embed_value_tag(JdbEmbed* eh, JdbValue h) {
    if (!eh) return JDB_T_NONE;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    return v ? tag_for_(*v) : JDB_T_NONE;
}

JDB_EMBED_API int64_t jdb_embed_value_int(JdbEmbed* eh, JdbValue h) {
    if (!eh) return 0;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    return v ? (int64_t)v->to_double() : 0;
}

JDB_EMBED_API double jdb_embed_value_double(JdbEmbed* eh, JdbValue h) {
    if (!eh) return 0.0;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    return v ? v->to_double() : 0.0;
}

JDB_EMBED_API const char* jdb_embed_value_string(JdbEmbed* eh, JdbValue h) {
    if (!eh) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    auto it = e->value_store.find(h);
    if (it == e->value_store.end()) return "";
    if (it->second.type == ValueType::STRING) {
        return it->second.as_string()->data.c_str();
    }
    // Non-strings get formatted on demand and cached on the Value's
    // existing OBJECT-style fallback path. For E3 we materialise via
    // to_string and stash inside a side-buffer keyed by handle.
    static thread_local std::unordered_map<int64_t, std::string> s_fmt_cache;
    s_fmt_cache[h] = it->second.to_string();
    return s_fmt_cache[h].c_str();
}

JDB_EMBED_API int jdb_embed_value_bool(JdbEmbed* eh, JdbValue h) {
    if (!eh) return 0;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    if (!v) return 0;
    if (v->type == ValueType::BOOLEAN) return v->to_double() != 0.0;
    return v->to_double() != 0.0;
}

JDB_EMBED_API int jdb_embed_array_len(JdbEmbed* eh, JdbValue h) {
    if (!eh) return 0;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    if (!v || v->type != ValueType::ARRAY) return 0;
    return (int)v->as_array()->elements.size();
}

JDB_EMBED_API JdbValue jdb_embed_array_get(JdbEmbed* eh, JdbValue h, int idx) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    const Value* v = e->lookup(h);
    if (!v || v->type != ValueType::ARRAY) return 0;
    auto& elems = v->as_array()->elements;
    if (idx < 0 || (size_t)idx >= elems.size()) return 0;
    return e->store(elems[(size_t)idx]);
}

JDB_EMBED_API int jdb_embed_array_is_numeric(JdbEmbed* eh, JdbValue h) {
    if (!eh) return 0;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    if (!v || v->type != ValueType::ARRAY) return 0;
    for (const auto& e : v->as_array()->elements) {
        if (e.type == ValueType::STRING || e.type == ValueType::OBJECT
            || e.type == ValueType::ARRAY || e.type == ValueType::NONE) {
            return 0;
        }
    }
    return 1;
}

JDB_EMBED_API int jdb_embed_map_size(JdbEmbed* eh, JdbValue h) {
    if (!eh) return 0;
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    if (!v || v->type != ValueType::OBJECT) return 0;
    return (int)v->as_object()->fields.size();
}

JDB_EMBED_API const char* jdb_embed_map_key_at(JdbEmbed* eh, JdbValue h, int idx) {
    if (!eh) return "";
    const Value* v = reinterpret_cast<JdbEmbedImpl*>(eh)->lookup(h);
    if (!v || v->type != ValueType::OBJECT) return "";
    const auto& fields = v->as_object()->fields;
    if (idx < 0 || (size_t)idx >= fields.size()) return "";
    return fields[(size_t)idx].first.c_str();
}

JDB_EMBED_API JdbValue jdb_embed_map_value_at(JdbEmbed* eh, JdbValue h, int idx) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    const Value* v = e->lookup(h);
    if (!v || v->type != ValueType::OBJECT) return 0;
    const auto& fields = v->as_object()->fields;
    if (idx < 0 || (size_t)idx >= fields.size()) return 0;
    return e->store(fields[(size_t)idx].second);
}

JDB_EMBED_API JdbValue jdb_embed_map_get(JdbEmbed* eh, JdbValue h, const char* key) {
    if (!eh || !key) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    const Value* v = e->lookup(h);
    if (!v || v->type != ValueType::OBJECT) return 0;
    // ObjectObj stores fields as a vector<pair>; linear scan is the only
    // option without exposing the internal map.
    const auto& fields = v->as_object()->fields;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].first == key) return e->store(fields[i].second);
    }
    return 0;
}

JDB_EMBED_API int jdb_embed_set_global_double(JdbEmbed* eh, const char* name, double val) {
    if (!eh || !name) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->vm.set_global(upper_(name), Value::make_f64(val));
    return 1;
}

JDB_EMBED_API int jdb_embed_set_global_int(JdbEmbed* eh, const char* name, int64_t val) {
    if (!eh || !name) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->vm.set_global(upper_(name), Value::make_i64(val));
    return 1;
}

JDB_EMBED_API int jdb_embed_set_global_string(JdbEmbed* eh, const char* name, const char* val) {
    if (!eh || !name || !val) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->vm.set_global(upper_(name), Value::make_string(std::string(val)));
    return 1;
}

JDB_EMBED_API int jdb_embed_set_global_bool(JdbEmbed* eh, const char* name, int val) {
    if (!eh || !name) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->vm.set_global(upper_(name), Value::make_bool(val != 0));
    return 1;
}

JDB_EMBED_API void jdb_embed_value_release(JdbEmbed* eh, JdbValue h) {
    if (!eh || !h) return;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->value_store.erase(h);
}

}  // extern "C"
