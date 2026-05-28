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

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

struct JdbEmbedImpl {
    VM vm;
    std::string output_buf;
    std::string last_error;
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

}  // extern "C"
