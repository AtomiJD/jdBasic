// jdb_embed_api.cpp - thin C-ABI for embedding the jdBasic VM in a host
// application (Godot GDExtension is the first consumer).
//
// MVP: init / shutdown / eval / load + last-error + free. Everything runs
// on the calling thread for now. Day 2 of the spike will lift the worker /
// stop / resume / recompile primitives out of mcp_stdio.cpp into something
// reusable here.

#define JDRT_EXPORTS

#include "jdb_embed_api.h"
#include "pcode.h"
#include "vm.h"
#include "dap.h"
#include "value.h"
#include "lexer.h"
#include "parser.h"
#include <cstdio>
#include "compiler.h"
#include "ai.h"
#include "numerics.h"
#ifdef SQLITE
#include "sql.h"
#endif
#include "llm.h"
#include "sound.h"
#ifdef HTTP
#include "http.h"
#endif

// Where jdbrt.dll lives, so explicit LLM backend loads can find ggml-*.dll
// next to our runtime. Defined here (always compiled into jdbrt) rather than
// in llm.cpp, whose body is excluded in some build flavors - otherwise the
// unconditional reference below fails to link in HEADLESS builds.
std::string g_jdb_embed_dll_dir;

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

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

    // T7 debugger - host hook + snapshot caches for the query accessors.
    JdbDebugHook user_debug_hook = nullptr;
    void*        user_debug_ud   = nullptr;
    JdbLineHook  user_line_hook  = nullptr;
    void*        user_line_ud    = nullptr;
    std::vector<VM::DebugFrame>                         dbg_frames;
    std::vector<std::pair<std::string, std::string>>    dbg_locals;
    std::vector<std::pair<std::string, std::string>>    dbg_globals;
    std::string                                         dbg_eval;

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

// Bundled jdBasic modules served to IMPORT from memory. The embed has no
// filesystem module reader, so IMPORT was unavailable; this re-enables it for
// libraries we ship inside the runtime. GDX is the convenience layer over the
// GDX.* native primitives (CharacterBody helpers, timing, ...).
static const char* GDX_MODULE_SRC =
    "EXPORT MODULE GDX\n"
    // ── CharacterBody movement ──────────────────────────────────────
    "EXPORT FUNC MOVE_AND_SLIDE(n)\n"
    "    RETURN GDX.CALL(n, \"move_and_slide\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC IS_ON_FLOOR(n)\n"
    "    RETURN GDX.CALL(n, \"is_on_floor\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC IS_ON_WALL(n)\n"
    "    RETURN GDX.CALL(n, \"is_on_wall\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC IS_ON_CEILING(n)\n"
    "    RETURN GDX.CALL(n, \"is_on_ceiling\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC GET_VELOCITY(n)\n"
    "    RETURN GDX.GET(n, \"velocity\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_VELOCITY(n, v)\n"
    "    RETURN GDX.SET(n, \"velocity\", v)\n"
    "ENDFUNC\n"
    // ── Time ────────────────────────────────────────────────────────
    "EXPORT FUNC TIME_MS()\n"
    "    RETURN GDX.CALL(GDX.SINGLETON(\"Time\"), \"get_ticks_msec\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC TIME_SEC()\n"
    "    RETURN GDX.CALL(GDX.SINGLETON(\"Time\"), \"get_ticks_usec\") / 1000000.0\n"
    "ENDFUNC\n"
    // ── Scene / node navigation ─────────────────────────────────────
    "EXPORT FUNC GET_NODE(n, path)\n"
    "    RETURN GDX.CALL(n, \"get_node\", path)\n"
    "ENDFUNC\n"
    "EXPORT FUNC GET_TREE(n)\n"
    "    RETURN GDX.CALL(n, \"get_tree\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC GET_PARENT(n)\n"
    "    RETURN GDX.CALL(n, \"get_parent\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC GET_CHILDREN(n)\n"
    "    RETURN GDX.CALL(n, \"get_children\")\n"
    "ENDFUNC\n"
    // recursive=TRUE, owned=FALSE so dynamically-added children (whose
    // `owner` isn't set) are still found - friendlier than Godot's default.
    "EXPORT FUNC FIND_CHILD(n, pattern)\n"
    "    RETURN GDX.CALL(n, \"find_child\", pattern, TRUE, FALSE)\n"
    "ENDFUNC\n"
    "EXPORT FUNC QUIT(n)\n"
    "    RETURN GDX.CALL(GDX.CALL(n, \"get_tree\"), \"quit\")\n"
    "ENDFUNC\n"
    // ── Transform / common properties ───────────────────────────────
    "EXPORT FUNC POS(n)\n"
    "    RETURN GDX.GET(n, \"position\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_POS(n, v)\n"
    "    RETURN GDX.SET(n, \"position\", v)\n"
    "ENDFUNC\n"
    "EXPORT FUNC GLOBAL_POS(n)\n"
    "    RETURN GDX.GET(n, \"global_position\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_GLOBAL_POS(n, v)\n"
    "    RETURN GDX.SET(n, \"global_position\", v)\n"
    "ENDFUNC\n"
    "EXPORT FUNC ROTATE_X(n, a)\n"
    "    RETURN GDX.CALL(n, \"rotate_x\", a)\n"
    "ENDFUNC\n"
    "EXPORT FUNC ROTATE_Y(n, a)\n"
    "    RETURN GDX.CALL(n, \"rotate_y\", a)\n"
    "ENDFUNC\n"
    "EXPORT FUNC LOOK_AT(n, target, up)\n"
    "    RETURN GDX.CALL(n, \"look_at\", target, up)\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_VISIBLE(n, b)\n"
    "    RETURN GDX.SET(n, \"visible\", b)\n"
    "ENDFUNC\n"
    "EXPORT FUNC IS_VISIBLE(n)\n"
    "    RETURN GDX.GET(n, \"visible\")\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_TEXT(n, s)\n"
    "    RETURN GDX.SET(n, \"text\", s)\n"
    "ENDFUNC\n"
    "EXPORT FUNC GET_TEXT(n)\n"
    "    RETURN GDX.GET(n, \"text\")\n"
    "ENDFUNC\n"
    // ── Metadata ────────────────────────────────────────────────────
    "EXPORT FUNC SET_META(n, k, v)\n"
    "    RETURN GDX.CALL(n, \"set_meta\", k, v)\n"
    "ENDFUNC\n"
    "EXPORT FUNC GET_META(n, k)\n"
    "    RETURN GDX.CALL(n, \"get_meta\", k)\n"
    "ENDFUNC\n"
    "EXPORT FUNC HAS_META(n, k)\n"
    "    RETURN GDX.CALL(n, \"has_meta\", k)\n"
    "ENDFUNC\n"
    // ── Process control ─────────────────────────────────────────────
    "EXPORT FUNC SET_PROCESS(n, b)\n"
    "    RETURN GDX.CALL(n, \"set_process\", b)\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_PHYSICS_PROCESS(n, b)\n"
    "    RETURN GDX.CALL(n, \"set_physics_process\", b)\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_PROCESS_INPUT(n, b)\n"
    "    RETURN GDX.CALL(n, \"set_process_input\", b)\n"
    "ENDFUNC\n"
    "EXPORT FUNC SET_PROCESS_UNHANDLED_INPUT(n, b)\n"
    "    RETURN GDX.CALL(n, \"set_process_unhandled_input\", b)\n"
    "ENDFUNC\n"
    // ── Tween sugar ─────────────────────────────────────────────────
    // The node is passed as a method ARGUMENT here (not the receiver), so it
    // must be wrapped in GDX.REF to marshal back to an Object - a bare handle
    // would arrive as a plain int. Receiver args (arg 0 of CALL/GET/SET) are
    // looked up directly and need no REF.
    "EXPORT FUNC TWEEN_TO(node, prop, target, dur)\n"
    "    DIM tw = GDX.CALL(node, \"create_tween\")\n"
    "    GDX.CALL(tw, \"tween_property\", GDX.REF(node), prop, target, dur)\n"
    "    RETURN tw\n"
    "ENDFUNC\n"
    "EXPORT FUNC TWEEN_TO_EASE(node, prop, target, dur, trans, ease)\n"
    "    DIM tw = GDX.CALL(node, \"create_tween\")\n"
    "    DIM pr = GDX.CALL(tw, \"tween_property\", GDX.REF(node), prop, target, dur)\n"
    "    GDX.CALL(pr, \"set_trans\", trans)\n"
    "    GDX.CALL(pr, \"set_ease\", ease)\n"
    "    RETURN tw\n"
    "ENDFUNC\n"
    // ── Vector math (pure jdBasic on the marshalled float arrays) ────
    "EXPORT FUNC V3_LEN(v)\n"
    "    RETURN SQR(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])\n"
    "ENDFUNC\n"
    "EXPORT FUNC V3_DIST(a, b)\n"
    "    RETURN SQR((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2]))\n"
    "ENDFUNC\n"
    "EXPORT FUNC V3_DOT(a, b)\n"
    "    RETURN a[0]*b[0] + a[1]*b[1] + a[2]*b[2]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V3_CROSS(a, b)\n"
    "    RETURN [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V3_NORM(v)\n"
    "    DIM L = SQR(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])\n"
    "    IF L = 0.0 THEN RETURN v\n"
    "    RETURN [v[0]/L, v[1]/L, v[2]/L]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V3_LERP(a, b, t)\n"
    "    RETURN [a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t, a[2]+(b[2]-a[2])*t]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V3_DIR_TO(a, b)\n"
    "    DIM dx = b[0]-a[0]\n"
    "    DIM dy = b[1]-a[1]\n"
    "    DIM dz = b[2]-a[2]\n"
    "    DIM L = SQR(dx*dx + dy*dy + dz*dz)\n"
    "    IF L = 0.0 THEN RETURN [0.0, 0.0, 0.0]\n"
    "    RETURN [dx/L, dy/L, dz/L]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V2_LEN(v)\n"
    "    RETURN SQR(v[0]*v[0] + v[1]*v[1])\n"
    "ENDFUNC\n"
    "EXPORT FUNC V2_DIST(a, b)\n"
    "    RETURN SQR((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]))\n"
    "ENDFUNC\n"
    "EXPORT FUNC V2_DOT(a, b)\n"
    "    RETURN a[0]*b[0] + a[1]*b[1]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V2_NORM(v)\n"
    "    DIM L = SQR(v[0]*v[0] + v[1]*v[1])\n"
    "    IF L = 0.0 THEN RETURN v\n"
    "    RETURN [v[0]/L, v[1]/L]\n"
    "ENDFUNC\n"
    "EXPORT FUNC V2_LERP(a, b, t)\n"
    "    RETURN [a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t]\n"
    "ENDFUNC\n";

static std::pair<std::string, std::string> bundled_module_reader(const std::string& name) {
    std::string up = name, low = name;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    // Modules shipped inside the runtime take precedence.
    if (up == "GDX") return { std::string(GDX_MODULE_SRC), std::string("res://__bundled__/gdx.jdb") };

    // Disk fallback: resolve <name>.jdb relative to the process working
    // directory (the host is expected to chdir into the project dir), plus a
    // one-level modules/ subdir. Mirrors the standalone reader's cwd lookup.
    std::vector<std::string> candidates = {
        up + ".jdb", low + ".jdb",
        "modules/" + up + ".jdb", "modules/" + low + ".jdb"
    };
    for (auto& cand : candidates) {
        std::ifstream f(cand);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            return { ss.str(), cand };
        }
    }
    return { std::string(), std::string() };
}

#if defined(JDB_MCU) && defined(JDB_LOAD_TRACE)
// The port prints heap and stack at each stage. A load that stops
// without a message says nothing about where; this says which of the
// four steps it was in and what memory looked like going into it.
extern "C" void jdb_load_trace(const char* stage);
#define LOAD_TRACE(s) jdb_load_trace(s)
#else
#define LOAD_TRACE(s) ((void)0)
#endif

#ifdef JDB_MCU
// A board with memory to spare somewhere other than SRAM says so by
// defining these; the weak versions here mean every other board carries
// on unchanged. Everything allocated between them is transient - the
// tokens and the syntax tree, which the compiler reads once and which
// are gone before the program runs.
extern "C" __attribute__((weak)) void jdb_transient_begin(void) {}
extern "C" __attribute__((weak)) void jdb_transient_end(void) {}
struct TransientScope {
    TransientScope()  { jdb_transient_begin(); }
    ~TransientScope() { jdb_transient_end(); }
};
// Shuts the scope again for the stretch that builds something lasting.
struct TransientPause {
    TransientPause()  { jdb_transient_end(); }
    ~TransientPause() { jdb_transient_begin(); }
};
#define JDB_TRANSIENT_SCOPE TransientScope jdb_ts_
#define JDB_TRANSIENT_PAUSE TransientPause jdb_tp_
#else
#define JDB_TRANSIENT_SCOPE (void)0
#define JDB_TRANSIENT_PAUSE (void)0
#endif

void run_source(VM& vm, const std::string& source) {
    LOAD_TRACE("enter");
    Compiler c;
    // The token stream and the syntax tree are finished once the chunk
    // is built - the chunk owns its own constants and names - so they go
    // before the program runs rather than sitting beside it for its whole
    // life. On a board with tens of kilobytes of heap that is most of the
    // room a program was costing.
    {
        JDB_TRANSIENT_SCOPE;
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        LOAD_TRACE("lexed");
        Parser parser(tokens);
        parser.file_reader = bundled_module_reader;
        auto ast = parser.parse();
        LOAD_TRACE("parsed");
        // The chunk outlives this block, so it is built with the scope
        // shut: it belongs in SRAM with the rest of what runs.
        {
            JDB_TRANSIENT_PAUSE;
            c.compile(ast);
        }
        LOAD_TRACE("compiled");
    }
    LOAD_TRACE("freed");
    vm.run_code(c.main_chunk(), c.functions());
    LOAD_TRACE("ran");
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
    parser.file_reader = bundled_module_reader;
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
            parser.file_reader = bundled_module_reader;
            (void)parser.parse();
            return "";
        } catch (const std::exception& ex) {
            return ex.what();
        }
    };

    // Optional module-natives. Same conditional registration the standalone
    // bridge does in vm_bridge.cpp - embed hosts that built with LLM/HTTP/
    // GFX flags get those surfaces too. Functions live at global scope so
    // we use ::-qualified names to escape the anonymous namespace.
    ::register_ai_builtins(e->vm);
    ::register_numerics_builtins(e->vm);
    ::register_llm_builtins(e->vm);
#ifdef SQLITE
    ::register_sql_builtins(e->vm);
#endif
#if defined(GFX) || defined(SOUND_DSP)
    // SOUND.* sequencer (pull-mode under SOUND_DSP: no device, host renders).
    ::register_sound_builtins(e->vm);
#endif
#ifdef HTTP
    // HTTP.* client + server (links OpenSSL). Lets a Godot-embedded script
    // fetch/serve over the network.
    ::register_http_builtins(e->vm);
#endif
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

#ifdef _WIN32
// When jdbrt.dll lives in an unusual location (Godot addon, plugin folder,
// etc.) and the host process (Godot.exe) doesn't add it to the DLL search
// path, secondary loads like ggml_backend_load_all() find no backends and
// AI.LOAD_LLM fails silently. Resolve our own .dll location and pin it
// as an extra search directory the first time jdb_embed_init runs.
static void register_self_dll_dir(void) {
    static bool done = false;
    if (done) return;
    done = true;
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&register_self_dll_dir, &self) || !self) {
        return;
    }
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    for (DWORD i = n; i > 0; --i) {
        if (buf[i - 1] == L'\\' || buf[i - 1] == L'/') {
            buf[i - 1] = L'\0';
            break;
        }
    }
    // SetDllDirectoryW affects plain LoadLibrary calls process-wide, which
    // is what ggml's backend scanner uses. AddDllDirectory only helps if
    // the caller passes LOAD_LIBRARY_SEARCH_USER_DIRS, which ggml doesn't.
    SetDllDirectoryW(buf);

    // Belt + braces: prepend our directory to PATH so even DLLs loaded
    // by indirect dependency chains (CUDA -> ggml-cuda -> cublas) can
    // find their siblings.
    wchar_t* current_path = nullptr;
    size_t path_size = 0;
    _wdupenv_s(&current_path, &path_size, L"PATH");
    std::wstring new_path = std::wstring(buf) + L";";
    if (current_path) {
        new_path += current_path;
        free(current_path);
    }
    _wputenv_s(L"PATH", new_path.c_str());

    // Pre-load the LLM backend chain by full path so subsequent ggml
    // backend scans see them as already-loaded modules.
    auto preload = [&](const wchar_t* name) {
        std::wstring full = std::wstring(buf) + L"\\" + name;
        LoadLibraryW(full.c_str());
    };
    preload(L"cudart64_12.dll");
    preload(L"cublasLt64_12.dll");
    preload(L"cublas64_12.dll");
    preload(L"ggml-base.dll");
    preload(L"ggml.dll");
    preload(L"ggml-cuda.dll");
    preload(L"llama.dll");

    // Hand the directory to llm.cpp so its ensure_backend() can call
    // ggml_backend_load() explicitly when load_all comes up empty.
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        std::string utf8(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), len, nullptr, nullptr);
        ::g_jdb_embed_dll_dir = utf8;
    }
}
#endif

JDB_EMBED_API JdbEmbed* jdb_embed_init(void) {
#ifdef _WIN32
    register_self_dll_dir();
#endif
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

static std::string upper_(const std::string& s);  // defined below

JDB_EMBED_API JdbValue jdb_embed_call(JdbEmbed* eh, const char* func_name,
                                      const JdbValue* args, int argc) {
    if (!eh || !func_name) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    try {
        std::vector<Value> argv;
        argv.reserve(argc > 0 ? (size_t)argc : 0);
        for (int i = 0; i < argc; ++i) {
            const Value* v = e->lookup(args ? args[i] : 0);
            argv.push_back(v ? *v : Value::make_none());
        }
        // Function names are stored upper-cased; match case-insensitively.
        Value result = e->vm.call_function(upper_(func_name), argv);
        return e->store(std::move(result));
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return 0;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_call";
        return 0;
    }
}

// Route program output straight to stdout instead of the capture
// buffer: a live console wants PRINT as it happens, and INPUT needs
// its prompt on screen before it blocks for the answer.
JDB_EMBED_API void jdb_embed_output_stdout(JdbEmbed* eh) {
    if (!eh) return;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->vm.on_output = [](const std::string& s) {
        printf("%s", s.c_str());
        fflush(stdout);
    };
}

JDB_EMBED_API char* jdb_embed_take_output(JdbEmbed* eh) {
    if (!eh) return nullptr;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (e->output_buf.empty()) return nullptr;
    char* out = dup_cstr(e->output_buf);
    e->output_buf.clear();
    return out;
}

// Already compiled: read the chunk and run it. Nothing here touches the
// lexer, the parser or the compiler, which is the whole point - a board
// that needs fifty seconds to translate sixteen kilobytes of source
// starts the same program in the time it takes to read the file.
static bool load_pcode(JdbEmbedImpl* e, const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        e->last_error = std::string("Cannot read ") + path;
        return true;   // handled, and failed
    }
    Chunk chunk;
    std::vector<FuncProto> funcs;
    std::string err;
    if (!pcode_read(in, chunk, funcs, err)) {
        e->last_error = err;
        return true;
    }
    e->vm.run_code(chunk, funcs);
    return true;
}

JDB_EMBED_API char* jdb_embed_load(JdbEmbed* eh, const char* path) {
    if (!eh || !path) return nullptr;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->output_buf.clear();
    e->last_error.clear();
    if (pcode_is_file(path)) {
        try {
            load_pcode(e, path);
        } catch (const std::exception& ex) {
            e->last_error = ex.what();
        } catch (...) {
            e->last_error = "unknown exception running p-code";
        }
        if (!e->last_error.empty()) return nullptr;
        return dup_cstr(e->output_buf);
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        e->last_error = std::string("Cannot read ") + path;
        return nullptr;
    }
#ifdef JDB_MCU
    // Three copies of the program were alive while it compiled: the
    // stringstream's own buffer, the string str() hands back, and the
    // lexer's. On a board where the source is the smaller half of the
    // problem it is still tens of kilobytes for nothing. Read once, and
    // let the file go before the lexer starts.
    std::string src;
    {
        std::stringstream ss;
        ss << in.rdbuf();
        src = ss.str();
    }
    in.close();
#else
    std::stringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
#endif
    try {
        run_source(e->vm, src);
        return dup_cstr(e->output_buf);
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return nullptr;
    } catch (...) {
        e->last_error = "unknown exception in jdb_embed_load";
        return nullptr;
    }
}

JDB_EMBED_API int jdb_embed_write_pcode(JdbEmbed* eh, const char* src_path,
                                        const char* out_path) {
    if (!eh || !src_path || !out_path) return -1;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->last_error.clear();
    std::ifstream in(src_path);
    if (!in.is_open()) {
        e->last_error = std::string("Cannot read ") + src_path;
        return -1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
    in.close();
    try {
        Compiler c;
        {
            JDB_TRANSIENT_SCOPE;
            Lexer lexer(src);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            parser.file_reader = bundled_module_reader;
            auto ast = parser.parse();
            {
                JDB_TRANSIENT_PAUSE;
                c.compile(ast);
            }
        }
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            e->last_error = std::string("Cannot write ") + out_path;
            return -1;
        }
        std::string err;
        if (!pcode_write(out, c.main_chunk(), c.functions(), err)) {
            e->last_error = err;
            return -1;
        }
        out.close();
        return 0;
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
        return -1;
    } catch (...) {
        e->last_error = "unknown exception writing p-code";
        return -1;
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

JDB_EMBED_API char* jdb_embed_check_standalone(const char* source) {
    if (!source) return dup_cstr(std::string("source is null"));
    try {
        std::string src = source;
        if (src.empty() || src.back() != '\n') src += '\n';
        Lexer lexer(src);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        parser.file_reader = bundled_module_reader;
        (void)parser.parse();
        return nullptr;  // success
    } catch (const std::exception& ex) {
        return dup_cstr(std::string(ex.what()));
    } catch (...) {
        return dup_cstr(std::string("unknown parse error"));
    }
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

// ── Tier 4: host-supplied native functions ─────────────────────────

JDB_EMBED_API int jdb_embed_register_native(JdbEmbed* eh,
                                              const char* name,
                                              int min_args,
                                              int max_args,
                                              JdbNativeFunc fn,
                                              void* userdata) {
    if (!eh || !name || !fn) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    JdbEmbed* embed_handle = eh;
    auto wrapper = [e, embed_handle, fn, userdata]
                   (const std::vector<Value>& args) -> Value {
        // Stage each arg into the value-store; the C callback only sees
        // int64 handles. Track them for cleanup after the call returns.
        std::vector<int64_t> handles;
        handles.reserve(args.size());
        for (const auto& a : args) handles.push_back(e->store(a));

        int64_t result_handle = fn(embed_handle,
                                   (int)handles.size(),
                                   handles.data(),
                                   userdata);

        // Release temp arg handles.
        for (auto h : handles) e->value_store.erase(h);

        if (!result_handle) return Value::make_none();
        auto it = e->value_store.find(result_handle);
        if (it == e->value_store.end()) return Value::make_none();
        Value out = std::move(it->second);
        e->value_store.erase(it);
        return out;
    };
    e->vm.register_native(name, min_args, max_args, wrapper);
    // Host-supplied natives must never broadcast: when a script passes
    // an array (e.g. GDX.COLOR result), the host expects ONE call with
    // the whole array, not N calls with element scalars.
    e->vm.extra_no_vectorize.insert(name);
    return 1;
}

JDB_EMBED_API JdbValue jdb_embed_make_int(JdbEmbed* eh, int64_t v) {
    if (!eh) return 0;
    return reinterpret_cast<JdbEmbedImpl*>(eh)->store(Value::make_i64(v));
}

JDB_EMBED_API JdbValue jdb_embed_make_double(JdbEmbed* eh, double v) {
    if (!eh) return 0;
    return reinterpret_cast<JdbEmbedImpl*>(eh)->store(Value::make_f64(v));
}

JDB_EMBED_API JdbValue jdb_embed_make_string(JdbEmbed* eh, const char* v) {
    if (!eh || !v) return 0;
    return reinterpret_cast<JdbEmbedImpl*>(eh)->store(Value::make_string(std::string(v)));
}

JDB_EMBED_API JdbValue jdb_embed_make_bool(JdbEmbed* eh, int v) {
    if (!eh) return 0;
    return reinterpret_cast<JdbEmbedImpl*>(eh)->store(Value::make_bool(v != 0));
}

JDB_EMBED_API JdbValue jdb_embed_make_nil(JdbEmbed* eh) {
    if (!eh) return 0;
    return reinterpret_cast<JdbEmbedImpl*>(eh)->store(Value::make_none());
}

JDB_EMBED_API JdbValue jdb_embed_make_array(JdbEmbed* eh,
                                              const JdbValue* elems,
                                              int n) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    Value v = Value::make_array();  // refcount starts at 1
    auto* arr = (ArrayObj*)v.obj;
    arr->elements.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        auto it = e->value_store.find(elems ? elems[i] : 0);
        if (it == e->value_store.end()) {
            arr->elements.push_back(Value::make_none());
        } else {
            arr->elements.push_back(it->second);  // copy
        }
    }
    return e->store(std::move(v));
}

JDB_EMBED_API JdbValue jdb_embed_make_map(JdbEmbed* eh,
                                            const char* const* keys,
                                            const JdbValue* vals,
                                            int n) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    Value v = Value::make_object();
    auto* obj = (ObjectObj*)v.obj;
    obj->fields.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        std::string k = (keys && keys[i]) ? std::string(keys[i]) : std::string();
        auto it = e->value_store.find(vals ? vals[i] : 0);
        Value entry = (it == e->value_store.end()) ? Value::make_none() : it->second;
        obj->fields.emplace_back(std::move(k), std::move(entry));
    }
    return e->store(std::move(v));
}

// ── Debugger (T7) ──────────────────────────────────────────────────

// Trampoline: the VM's DebugInfo holds a plain C hook (void*, int, const
// char*); we route it to the public JdbDebugHook with the embed handle.
static void embed_debug_trampoline(void* ud, int line, const char* reason) {
    auto* e = reinterpret_cast<JdbEmbedImpl*>(ud);
    if (e && e->user_debug_hook) {
        e->user_debug_hook(reinterpret_cast<JdbEmbed*>(e), line,
                           reason ? reason : "", e->user_debug_ud);
    }
}

// Per-line predicate trampoline: routes the VM's plain C line callback to
// the public JdbLineHook with the embed handle.
static int embed_line_trampoline(void* ud, int line) {
    auto* e = reinterpret_cast<JdbEmbedImpl*>(ud);
    if (e && e->user_line_hook) {
        return e->user_line_hook(reinterpret_cast<JdbEmbed*>(e), line, e->user_line_ud);
    }
    return 0;
}

static DebugInfo* dbg_(JdbEmbedImpl* e) {
    if (!e) return nullptr;
    if (!e->vm.debug) e->vm.debug = std::make_unique<DebugInfo>();
    return e->vm.debug.get();
}

JDB_EMBED_API int jdb_embed_debug_enable(JdbEmbed* eh) {
    if (!eh) return 0;
    return dbg_(reinterpret_cast<JdbEmbedImpl*>(eh)) ? 1 : 0;
}

JDB_EMBED_API void jdb_embed_debug_set_hook(JdbEmbed* eh, JdbDebugHook hook, void* ud) {
    if (!eh) return;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    DebugInfo* d = dbg_(e);
    e->user_debug_hook = hook;
    e->user_debug_ud   = ud;
    d->host_ud   = e;
    d->host_hook = hook ? &embed_debug_trampoline : nullptr;
}

JDB_EMBED_API void jdb_embed_debug_set_line_hook(JdbEmbed* eh, JdbLineHook hook, void* ud) {
    if (!eh) return;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    DebugInfo* d = dbg_(e);
    e->user_line_hook = hook;
    e->user_line_ud   = ud;
    d->line_ud   = e;
    d->line_break = hook ? &embed_line_trampoline : nullptr;
}

JDB_EMBED_API void jdb_embed_debug_set_breakpoint(JdbEmbed* eh, int line) {
    if (!eh) return;
    dbg_(reinterpret_cast<JdbEmbedImpl*>(eh))->breakpoints[""][line];  // default BreakpointInfo
}

JDB_EMBED_API void jdb_embed_debug_clear_breakpoint(JdbEmbed* eh, int line) {
    if (!eh) return;
    DebugInfo* d = dbg_(reinterpret_cast<JdbEmbedImpl*>(eh));
    auto it = d->breakpoints.find("");
    if (it != d->breakpoints.end()) it->second.erase(line);
}

JDB_EMBED_API void jdb_embed_debug_clear_all(JdbEmbed* eh) {
    if (!eh) return;
    dbg_(reinterpret_cast<JdbEmbedImpl*>(eh))->breakpoints.clear();
}

JDB_EMBED_API int jdb_embed_debug_current_line(JdbEmbed* eh) {
    if (!eh) return 0;
    return reinterpret_cast<JdbEmbedImpl*>(eh)->vm.debug_current_line();
}

JDB_EMBED_API int jdb_embed_debug_stack_count(JdbEmbed* eh) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->dbg_frames = e->vm.debug_get_stack_frames();
    return (int)e->dbg_frames.size();
}

JDB_EMBED_API int jdb_embed_debug_stack_line(JdbEmbed* eh, int level) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (level < 0 || level >= (int)e->dbg_frames.size()) return 0;
    return e->dbg_frames[(size_t)level].line;
}

JDB_EMBED_API const char* jdb_embed_debug_stack_function(JdbEmbed* eh, int level) {
    if (!eh) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (level < 0 || level >= (int)e->dbg_frames.size()) return "";
    return e->dbg_frames[(size_t)level].name.c_str();
}

JDB_EMBED_API int jdb_embed_debug_locals_count(JdbEmbed* eh, int level) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->dbg_locals = e->vm.debug_get_locals_at(level);
    return (int)e->dbg_locals.size();
}

JDB_EMBED_API const char* jdb_embed_debug_local_name(JdbEmbed* eh, int i) {
    if (!eh) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (i < 0 || i >= (int)e->dbg_locals.size()) return "";
    return e->dbg_locals[(size_t)i].first.c_str();
}

JDB_EMBED_API const char* jdb_embed_debug_local_value(JdbEmbed* eh, int i) {
    if (!eh) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (i < 0 || i >= (int)e->dbg_locals.size()) return "";
    return e->dbg_locals[(size_t)i].second.c_str();
}

JDB_EMBED_API int jdb_embed_debug_globals_count(JdbEmbed* eh) {
    if (!eh) return 0;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    e->dbg_globals = e->vm.debug_get_globals();
    return (int)e->dbg_globals.size();
}

JDB_EMBED_API const char* jdb_embed_debug_global_name(JdbEmbed* eh, int i) {
    if (!eh) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (i < 0 || i >= (int)e->dbg_globals.size()) return "";
    return e->dbg_globals[(size_t)i].first.c_str();
}

JDB_EMBED_API const char* jdb_embed_debug_global_value(JdbEmbed* eh, int i) {
    if (!eh) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    if (i < 0 || i >= (int)e->dbg_globals.size()) return "";
    return e->dbg_globals[(size_t)i].second.c_str();
}

JDB_EMBED_API const char* jdb_embed_debug_eval(JdbEmbed* eh, const char* expr) {
    if (!eh || !expr) return "";
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    bool had_debug = (bool)e->vm.debug;
    bool prev = had_debug ? e->vm.debug->suppress : false;
    if (had_debug) e->vm.debug->suppress = true;
    JdbValue h = jdb_embed_eval_expr(eh, expr);
    if (had_debug) e->vm.debug->suppress = prev;
    const Value* v = e->lookup(h);
    e->dbg_eval = v ? v->to_string() : std::string("<error>");
    if (h) jdb_embed_value_release(eh, h);
    return e->dbg_eval.c_str();
}

JDB_EMBED_API void jdb_embed_debug_continue(JdbEmbed* eh) {
    if (!eh) return;
    dbg_(reinterpret_cast<JdbEmbedImpl*>(eh))->state = DebugState::RUNNING;
}

JDB_EMBED_API void jdb_embed_debug_step_over(JdbEmbed* eh) {
    if (!eh) return;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    DebugInfo* d = dbg_(e);
    d->state = DebugState::STEP_OVER;
    d->step_over_depth = e->vm.debug_call_depth();
}

JDB_EMBED_API void jdb_embed_debug_step_in(JdbEmbed* eh) {
    if (!eh) return;
    dbg_(reinterpret_cast<JdbEmbedImpl*>(eh))->state = DebugState::STEP_IN;
}

JDB_EMBED_API void jdb_embed_debug_step_out(JdbEmbed* eh) {
    if (!eh) return;
    auto* e = reinterpret_cast<JdbEmbedImpl*>(eh);
    DebugInfo* d = dbg_(e);
    d->state = DebugState::STEP_OUT;
    d->step_out_depth = e->vm.debug_call_depth();
}

}  // extern "C"
