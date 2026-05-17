// jdBasic MCP server — stdio transport (newline-delimited JSON).
//
// Per the MCP spec, each request/response is one JSON object on its
// own line, terminated by '\n'. (NOT LSP-style Content-Length framing —
// that's a different protocol.) JSON is parsed/serialized through the
// VM's own JSON.PARSE$ / JSON.STRINGIFY$ natives (no extra dependency).
// The persistent VM is the same one that would run a script — every
// jdb_eval shares state across calls, exactly like the HTTP variant in
// mcp/server.jdb.
//
// Cross-platform (POSIX + Windows): build.sh / build.bat emit -DMCPSERVER
// when MCPSERVER=1, and main.cpp guards --mcp dispatch behind that macro.
// Platform-specific bits (raw stdin read, popen, binary-mode stdio) are
// wrapped in #ifdef _WIN32 below.

#ifdef MCPSERVER

#include "mcp_stdio.h"
#include "vm.h"
#include "value.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <ctime>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <atomic>
#include <future>

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #define MCP_POPEN  _popen
  #define MCP_PCLOSE _pclose
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #ifdef __APPLE__
    #include <mach-o/dyld.h>
  #endif
  #define MCP_POPEN  popen
  #define MCP_PCLOSE pclose
#endif

// run_on_vm lives in main.cpp. Declared extern here so mcp_stdio.cpp can
// drive code execution on the persistent VM.
extern void run_on_vm(VM& vm, const std::string& source);
extern std::string recompile_on_vm(VM& vm, const std::string& source);

// Workspace persistence — defined in main.cpp. SAVEWS / LOADWS are REPL
// commands by their original UX, but the underlying logic is generic and
// the MCP server exposes them as jdb_savews / jdb_loadws tools. The fourth
// `extra_filter` param lets the MCP wrapper drop boot-set vars (PI, E,
// other VM-builtin globals) from the saved workspace.
extern void save_workspace(VM& vm, const std::string& program_buffer,
                           const std::string& name,
                           std::function<bool(const std::string&)> extra_filter = nullptr);
extern void load_workspace(VM& vm, std::string& program_buffer,
                           const std::string& name);

// Session source buffer: every successful jdb_eval call appends its source
// here so jdb_savews can persist user FUNC/SUB definitions alongside the
// var snapshot. Without this, only the variable values would survive a
// save/load round-trip — load_workspace re-parses the program text to
// rebuild function bindings.
static std::string g_session_buffer;

// Last path passed to jdb_load — jdb_recompile defaults to this so the
// caller doesn't have to pass it again every iteration.
static std::string g_last_loaded_path;

namespace {

// ── Framing ─────────────────────────────────────────────────────
// MCP stdio: one JSON-RPC object per line, terminated by '\n'.
// Empty lines (whitespace-only) are skipped — Claude Code occasionally
// sends a heartbeat newline. EOF on stdin → return false to exit cleanly.
//
// stdin is read raw via ::read, NOT std::cin, to avoid any translation
// and to keep behaviour deterministic when the VM swaps on_output during
// a tool call.

bool read_frame(std::string& body) {
    body.clear();
    char c;
    while (true) {
#ifdef _WIN32
        int r = _read(_fileno(stdin), &c, 1);
#else
        ssize_t r = ::read(STDIN_FILENO, &c, 1);
#endif
        if (r == 0) {                       // EOF
            return !body.empty();           // last line w/o trailing \n is still valid
        }
        if (r < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            return false;
        }
        if (c == '\n') {
            // Strip optional trailing \r (CRLF tolerant).
            if (!body.empty() && body.back() == '\r') body.pop_back();
            // Skip blank lines — keep reading for the next real frame.
            bool blank = true;
            for (char ch : body) { if (ch != ' ' && ch != '\t') { blank = false; break; } }
            if (blank) { body.clear(); continue; }
            return true;
        }
        body.push_back(c);
    }
}

// stdout is shared between the main dispatch thread and the reader thread
// (when it fast-paths a jdb_stop response). Without serialisation the two
// frames could interleave bytes mid-flush. Held only for the duration of
// the cout write, so it never blocks the VM tick.
static std::mutex g_stdout_mutex;

void write_frame(const std::string& body) {
    // One JSON object, then '\n'. Flush so the client sees the reply
    // before its handshake timeout fires. NDJSON forbids embedded
    // newlines in the body — JSON.STRINGIFY$ already escapes them.
    std::lock_guard<std::mutex> g(g_stdout_mutex);
    std::cout << body << '\n';
    std::cout.flush();
}

// ── Logging (server-side only) ───────────────────────────────────
// Anything stdout-bound MUST be a JSON-RPC frame. Server-side logging
// goes to stderr only. Off by default; flip on with JDBASIC_MCP_LOG=1.

bool log_enabled() {
    static int cached = -1;
    if (cached < 0) {
        const char* e = std::getenv("JDBASIC_MCP_LOG");
        cached = (e && *e && std::strcmp(e, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

void log_line(const std::string& s) {
    if (!log_enabled()) return;
    std::cerr << "[mcp] " << s << "\n";
    std::cerr.flush();
}

// ── Value helpers ────────────────────────────────────────────────
// Tiny ergonomics over ObjectObj.fields — every JSON-RPC envelope and
// every tool result is one of these. Centralising "get/set field" keeps
// the dispatch code readable.

Value make_string_v(const std::string& s) {
    return Value::make_string(s);
}

void obj_set(Value& v, const std::string& key, Value val) {
    if (v.type != ValueType::OBJECT) return;
    v.as_object()->set(key, std::move(val));
}

Value* obj_get(const Value& v, const std::string& key) {
    if (v.type != ValueType::OBJECT) return nullptr;
    return v.as_object()->get(key);
}

std::string obj_get_str(const Value& v, const std::string& key, const std::string& def = "") {
    Value* f = obj_get(v, key);
    if (!f || f->type != ValueType::STRING) return def;
    return f->as_string()->data;
}

bool obj_has(const Value& v, const std::string& key) {
    return obj_get(v, key) != nullptr;
}

Value parse_json(VM& vm, const std::string& s) {
    return vm.call_function("JSON.PARSE$", { Value::make_string(s) });
}

std::string stringify_json(VM& vm, const Value& v) {
    Value out = vm.call_function("JSON.STRINGIFY$", { v });
    if (out.type == ValueType::STRING) return out.as_string()->data;
    return "null";
}

// ── Tool result helpers ─────────────────────────────────────────
// MCP tools/call responses always look like
//   { "content": [ {"type":"text","text": "..."} ], "isError": bool }

Value make_text_result(const std::string& text, bool is_error) {
    Value content = Value::make_array();
    Value block = Value::make_object();
    obj_set(block, "type", make_string_v("text"));
    obj_set(block, "text", make_string_v(text));
    content.as_array()->elements.push_back(std::move(block));

    Value r = Value::make_object();
    obj_set(r, "content", std::move(content));
    if (is_error) obj_set(r, "isError", Value::make_bool(true));
    return r;
}

// ── PRINT capture ───────────────────────────────────────────────
// Swap vm.on_output with a buffer-appender; restore on scope exit.
// The host's setup_dynamic_code does not rely on on_output, so this is
// safe across the whole tool-call lifetime.

struct OutputCapture {
    VM& vm;
    std::function<void(const std::string&)> prev;
    std::string buf;
    explicit OutputCapture(VM& v) : vm(v), prev(v.on_output) {
        vm.on_output = [this](const std::string& s) { buf += s; };
    }
    ~OutputCapture() { vm.on_output = prev; }
};

// ── VM worker thread + job queue ────────────────────────────────
//
// All VM-mutating work runs on a single persistent worker thread:
//
//  * keeps SDL thread-affinity intact across multiple jdb_load /
//    jdb_resume calls (a fresh thread per call would break the
//    "the thread that did SDL_Init owns the window" rule),
//  * decouples long-running scripts from the MCP request/response
//    cycle — jdb_load/jdb_resume post a job and return immediately,
//    so the MCP client never waits for the game loop to end.
//
// jdb_eval also posts a job, but waits on a promise — evals are
// short and the caller wants the result. While the worker is
// running a long script, eval is rejected ("VM busy") rather than
// queued, because waiting would re-introduce the very hang the
// async model is meant to avoid. Atomi must call jdb_stop first.
//
// jdb_vars / jdb_funcs / jdb_savews / jdb_loadws read or mutate
// VM state and stay on the main thread — they take the worker
// mutex briefly so they're serialised against any in-flight job.

struct VmJob {
    std::function<void(VM&)> task;
};

struct VmWorker {
    std::thread t;
    std::mutex m;
    std::condition_variable cv;
    std::deque<VmJob> queue;
    std::atomic<bool> busy{false};      // true while a job is executing
    std::atomic<bool> shutdown{false};
};

static VmWorker g_worker;

// True while the worker is actively running a job OR a job is queued
// waiting to be picked up. Caller MUST hold g_worker.m to make this
// check race-free against the worker thread.
inline bool worker_busy_or_queued_locked() {
    return g_worker.busy.load() || !g_worker.queue.empty();
}

void worker_loop(VM& vm) {
    while (true) {
        VmJob job;
        {
            std::unique_lock<std::mutex> lk(g_worker.m);
            g_worker.cv.wait(lk, []{
                return !g_worker.queue.empty() || g_worker.shutdown.load();
            });
            if (g_worker.queue.empty() && g_worker.shutdown.load()) break;
            job = std::move(g_worker.queue.front());
            g_worker.queue.pop_front();
            g_worker.busy.store(true);
        }
        try {
            job.task(vm);
        } catch (const std::exception& e) {
            log_line(std::string("worker job exception: ") + e.what());
        } catch (...) {
            log_line("worker job: unknown exception");
        }
        g_worker.busy.store(false);
    }
}

// Post a job and notify the worker. Caller must NOT hold g_worker.m.
void post_job(std::function<void(VM&)> task) {
    {
        std::lock_guard<std::mutex> lk(g_worker.m);
        VmJob j;
        j.task = std::move(task);
        g_worker.queue.push_back(std::move(j));
    }
    g_worker.cv.notify_one();
}

// ── Tools ───────────────────────────────────────────────────────

Value tool_echo(VM&, const Value& args) {
    return make_text_result("Echo: " + obj_get_str(args, "message"), false);
}

Value tool_jdb_eval(VM& vm, const Value& args) {
    std::string code = obj_get_str(args, "code");
    {
        std::lock_guard<std::mutex> lk(g_worker.m);
        if (worker_busy_or_queued_locked()) {
            return make_text_result(
                "VM busy — call jdb_stop first (or wait for the script to STOP "
                "on its own).", true);
        }
    }
    auto promise = std::make_shared<std::promise<Value>>();
    auto fut = promise->get_future();
    post_job([code, promise](VM& v) {
        OutputCapture cap(v);
        try {
            run_on_vm(v, code + "\n");
            // Only append on success — failed snippets shouldn't poison the
            // workspace's PROGRAM section. load_workspace re-parses this text
            // to rebuild user FUNC/SUB bindings, so syntax-broken fragments
            // would prevent any later restore from succeeding.
            g_session_buffer += code;
            if (code.empty() || code.back() != '\n') g_session_buffer += '\n';
            promise->set_value(make_text_result(cap.buf, false));
        } catch (const std::exception& e) {
            std::string err = "Error: ";
            err += e.what();
            std::string body = cap.buf.empty() ? err : (cap.buf + err);
            promise->set_value(make_text_result(body, true));
        }
    });
    return fut.get();
}

Value tool_jdb_check(VM& vm, const Value& args) {
    std::string code = obj_get_str(args, "code");
    if (!vm.on_check) {
        return make_text_result("on_check not registered", true);
    }
    std::string err = vm.on_check(vm, code);
    if (err.empty()) return make_text_result("ok", false);
    return make_text_result(err, true);
}

Value tool_jdb_stop(VM& vm, const Value& /*args*/) {
    // Fallback path. Normally the reader thread fast-paths jdb_stop before
    // it ever reaches the main dispatch (so the response goes out while the
    // VM is still running). If we get here, the request arrived between
    // tool calls — the VM is idle and stop_requested will be cleared by
    // the next run_code prep, so this is mostly a no-op-with-receipt.
    vm.stop_requested.store(true);
    return make_text_result("[stop requested]", false);
}

Value tool_jdb_recompile(VM& vm, const Value& args) {
    {
        std::lock_guard<std::mutex> lk(g_worker.m);
        if (worker_busy_or_queued_locked()) {
            return make_text_result(
                "VM busy — call jdb_stop first, then recompile while STOPped.",
                true);
        }
    }
    std::string path = obj_get_str(args, "path");
    if (path.empty()) {
        if (g_last_loaded_path.empty()) {
            return make_text_result(
                "No path given and no previous jdb_load — pass path=<file>.",
                true);
        }
        path = g_last_loaded_path;
    }
    std::ifstream in(path);
    if (!in.is_open()) return make_text_result("Cannot read " + path, true);
    std::stringstream ss; ss << in.rdbuf();
    std::string source = ss.str();

    auto promise = std::make_shared<std::promise<Value>>();
    auto fut = promise->get_future();
    post_job([source, path, promise](VM& v) {
        try {
            std::string summary = recompile_on_vm(v, source);
            promise->set_value(make_text_result(
                "[recompiled " + path + " — " + summary + "]", false));
        } catch (const std::exception& e) {
            promise->set_value(make_text_result(
                "Recompile error in " + path + ": " + e.what(), true));
        }
    });
    return fut.get();
}

Value tool_jdb_status(VM& vm, const Value&) {
    std::string s;
    if (g_worker.busy.load()) {
        s = "running (worker executing a job)";
    } else if (vm.is_paused()) {
        s = "stopped — call jdb_resume to continue, or jdb_eval to inspect/mutate";
    } else {
        s = "idle — ready for jdb_load / jdb_eval";
    }
    return make_text_result(s, false);
}

Value tool_jdb_resume(VM& vm, const Value& /*args*/) {
    {
        std::lock_guard<std::mutex> lk(g_worker.m);
        if (worker_busy_or_queued_locked()) {
            return make_text_result("VM is already running.", true);
        }
        if (!vm.is_paused()) {
            return make_text_result(
                "Nothing to resume — VM is not in a STOPped state.", false);
        }
    }
    post_job([](VM& v) {
        // Discard PRINT during the async resume — the originating tool
        // call is already gone. Live-tweak workflows inspect via
        // jdb_eval after the next STOP, not via load/resume stdout.
        auto prev = v.on_output;
        v.on_output = [](const std::string&) {};
        try {
            v.resume();
        } catch (const std::exception& e) {
            log_line(std::string("resume error: ") + e.what());
        } catch (...) {
            log_line("resume error (unknown exception)");
        }
        v.on_output = prev;
    });
    return make_text_result(
        "[resumed in worker — script runs until next STOP or running=0]",
        false);
}

Value tool_jdb_load(VM& vm, const Value& args) {
    {
        std::lock_guard<std::mutex> lk(g_worker.m);
        if (worker_busy_or_queued_locked()) {
            return make_text_result(
                "VM busy — call jdb_stop first.", true);
        }
        if (vm.is_paused()) {
            return make_text_result(
                "VM is paused (STOPped). Call jdb_resume to continue the "
                "current script, or interact with it via jdb_eval. To run "
                "a different script, exit the current one first (e.g. "
                "`running = 0` then jdb_resume).", true);
        }
    }
    std::string path = obj_get_str(args, "path");
    std::ifstream in(path);
    if (!in.is_open()) {
        return make_text_result("Cannot read " + path, true);
    }
    std::stringstream ss; ss << in.rdbuf();
    std::string source = ss.str();
    std::string banner = "[load posted: " + path + ", "
        + std::to_string(source.size()) + " bytes — script runs until first "
        "STOP or running=0]";
    g_last_loaded_path = path;  // jdb_recompile defaults to this
    post_job([source, path](VM& v) {
        // Discard PRINT during async run — the load tool already returned.
        // For live-tweak workflows, use jdb_eval after STOP to read state.
        auto prev = v.on_output;
        v.on_output = [](const std::string&) {};
        try {
            run_on_vm(v, source);
        } catch (const std::exception& e) {
            log_line("run error in " + path + ": " + e.what());
        } catch (...) {
            log_line("run error in " + path + " (unknown exception)");
        }
        v.on_output = prev;
    });
    return make_text_result(banner, false);
}

// Cache of variable names that exist on the VM at boot. jdb_vars hides
// these so the user only sees vars created by his own snippets.
struct BootSet {
    std::unordered_set<std::string> vars;
    std::unordered_set<std::string> funcs;
};

BootSet& boot_set() {
    static BootSet b; return b;
}

void capture_boot_set(VM& vm) {
    auto& b = boot_set();
    for (auto& [name, slot] : vm.get_global_names()) b.vars.insert(name);
    for (auto& f : vm.get_funcs()) b.funcs.insert(f.name);
}

bool is_user_var(const std::string& name) {
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') return false;
    if (name.find('.') != std::string::npos) return false;  // dotted = engine noise
    return boot_set().vars.count(name) == 0;
}

bool is_user_func(const std::string& name) {
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') return false;
    return boot_set().funcs.count(name) == 0;
}

Value tool_jdb_vars(VM& vm, const Value&) {
    std::lock_guard<std::mutex> lk(g_worker.m);
    if (worker_busy_or_queued_locked()) {
        return make_text_result("VM busy — call jdb_stop first.", true);
    }
    auto& names = vm.get_global_names();
    auto& globals = vm.get_globals();
    std::vector<std::string> kept;
    for (auto& [name, slot] : names) {
        if (slot >= globals.size()) continue;
        if (!is_user_var(name)) continue;
        kept.push_back("  " + name + " = " + globals[slot].to_string());
    }
    if (kept.empty()) return make_text_result("(no user variables)", false);
    std::string out;
    for (size_t i = 0; i < kept.size(); i++) {
        if (i) out += "\n";
        out += kept[i];
    }
    return make_text_result(out, false);
}

Value tool_jdb_funcs(VM& vm, const Value&) {
    std::lock_guard<std::mutex> lk(g_worker.m);
    if (worker_busy_or_queued_locked()) {
        return make_text_result("VM busy — call jdb_stop first.", true);
    }
    const auto& funcs = vm.get_funcs();
    std::vector<std::string> kept;
    for (const auto& f : funcs) {
        if (!is_user_func(f.name)) continue;
        std::string kind = f.is_sub ? "SUB" : (f.is_async ? "ASYNC FUNC" : "FUNC");
        std::string sig = "(";
        for (size_t i = 0; i < f.param_names.size(); i++) {
            if (i) sig += ", ";
            sig += f.param_names[i];
        }
        sig += ")";
        kept.push_back("  " + f.name + sig + "  " + kind);
    }
    if (kept.empty()) return make_text_result("(no user functions)", false);
    std::string out;
    for (size_t i = 0; i < kept.size(); i++) {
        if (i) out += "\n";
        out += kept[i];
    }
    return make_text_result(out, false);
}

// Locate the directory containing the running jdBasic executable. Used to
// resolve doc/languages.md relative to the binary so a redistributed bundle
// works without the user having to set "cwd" in the MCP client config.
// Returns "" if the platform lookup fails — caller falls back to CWD.

std::string exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string path((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, &path[0], len, nullptr, nullptr);
    auto pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
#elif defined(__APPLE__)
    char buf[4096]; uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    std::string path(buf);
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n <= 0) return "";
    std::string path(buf, (size_t)n);
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
#endif
}

// Open doc/languages.md, trying EXE-relative first (bundle case), then CWD
// (in-tree dev case). On failure, both attempted paths are returned via
// out-params so the error message can name them.

bool open_doc_languages(std::ifstream& in,
                        std::string& tried_exe,
                        std::string& tried_cwd) {
    std::string ed = exe_dir();
    if (!ed.empty()) {
#ifdef _WIN32
        tried_exe = ed + "\\doc\\languages.md";
#else
        tried_exe = ed + "/doc/languages.md";
#endif
        in.open(tried_exe);
        if (in.is_open()) return true;
    }
    tried_cwd = "doc/languages.md";
    in.open(tried_cwd);
    return in.is_open();
}

// jdb_doc — fuzzy search doc/languages.md, mirroring mcp/server.jdb's
// IS_DOC_ANCHOR + body-collection logic. Anchor = bullet starting with
// "* **" or any markdown heading.

bool is_doc_anchor(const std::string& s) {
    if (s.size() >= 4 && s.compare(0, 4, "* **") == 0) return true;
    if (!s.empty() && s[0] == '#') return true;
    return false;
}

std::string upper(const std::string& s) {
    std::string out(s);
    for (auto& c : out) c = (char)std::toupper((unsigned char)c);
    return out;
}

Value tool_jdb_doc(VM&, const Value& args) {
    std::string query = obj_get_str(args, "query");
    if (query.empty()) return make_text_result("query is empty", true);

    std::ifstream in;
    std::string tried_exe, tried_cwd;
    if (!open_doc_languages(in, tried_exe, tried_cwd)) {
        std::string msg = "Cannot read doc/languages.md";
        if (!tried_exe.empty()) msg += " — tried '" + tried_exe + "'";
        if (!tried_cwd.empty()) msg += (tried_exe.empty() ? " — tried '" : " and '") + tried_cwd + "'";
        return make_text_result(msg, true);
    }
    std::stringstream ss; ss << in.rdbuf();
    std::string doc = ss.str();

    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : doc) {
            if (c == '\n') { lines.push_back(std::move(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(std::move(cur));
    }

    std::string q_up = upper(query);
    const size_t MAX_HITS = 8;
    const size_t MAX_BODY = 30;
    std::vector<std::string> hits;

    size_t i = 0;
    while (i < lines.size() && hits.size() < MAX_HITS) {
        const std::string& line = lines[i];
        if (!is_doc_anchor(line)) { i++; continue; }
        if (upper(line).find(q_up) == std::string::npos) { i++; continue; }
        std::string entry = line;
        size_t body = 1;
        size_t j = i + 1;
        while (j < lines.size() && body < MAX_BODY) {
            if (is_doc_anchor(lines[j])) break;
            entry += "\n";
            entry += lines[j];
            body++; j++;
        }
        hits.push_back(std::move(entry));
        i = j;
    }

    if (hits.empty()) return make_text_result("(no matches for: " + query + ")", true);
    std::string out;
    for (size_t k = 0; k < hits.size(); k++) {
        if (k) out += "\n\n---\n\n";
        out += hits[k];
    }
    return make_text_result(out, false);
}

// jdb_savews / jdb_loadws — Smalltalk-style workspace persistence over MCP.
// SAVEWS pickles every user-bound global (variables and FUNC/SUB definitions
// the agent has accumulated this session) into "<name>.jdws" in the server's
// CWD. LOADWS resets the VM and restores from that file. The original REPL
// commands kept a source buffer too; the MCP server has no notion of a
// linear input log so we pass an empty buffer — only state is persisted,
// not history. Source-text projects are better handled with jdb_load.

Value tool_jdb_savews(VM& vm, const Value& args) {
    std::string name = obj_get_str(args, "name");
    if (name.empty()) return make_text_result("name is empty", true);
    std::lock_guard<std::mutex> lk(g_worker.m);
    if (worker_busy_or_queued_locked()) {
        return make_text_result("VM busy — call jdb_stop first.", true);
    }
    OutputCapture cap(vm);
    try {
        // Pass the session buffer so FUNC/SUB definitions get persisted in
        // the source_code array of the .jsws file. load_workspace re-parses
        // this text to rebuild user function bindings. The MCP server also
        // hands save_workspace its boot-set filter so VM-internal globals
        // (created during VM bootstrap, e.g. ZEROS scratch slots) are
        // skipped — only vars the user actually set this session land in
        // the file.
        save_workspace(vm, g_session_buffer, name,
                       [](const std::string& n) { return is_user_var(n); });
        std::string body = cap.buf.empty()
            ? ("workspace saved: " + name + ".jsws") : cap.buf;
        return make_text_result(body, false);
    } catch (const std::exception& e) {
        return make_text_result(std::string("Error: ") + e.what(), true);
    }
}

Value tool_jdb_loadws(VM& vm, const Value& args) {
    std::string name = obj_get_str(args, "name");
    if (name.empty()) return make_text_result("name is empty", true);
    std::lock_guard<std::mutex> lk(g_worker.m);
    if (worker_busy_or_queued_locked()) {
        return make_text_result("VM busy — call jdb_stop first.", true);
    }
    OutputCapture cap(vm);
    try {
        // load_workspace clears + repopulates the buffer — adopt it as the
        // new session buffer so subsequent jdb_savews preserves the loaded
        // FUNC/SUB definitions plus anything the user adds afterwards.
        g_session_buffer.clear();
        load_workspace(vm, g_session_buffer, name);
        std::string body = cap.buf.empty()
            ? ("workspace loaded: " + name + ".jsws") : cap.buf;
        return make_text_result(body, false);
    } catch (const std::exception& e) {
        return make_text_result(std::string("Error: ") + e.what(), true);
    }
}

// jdb_run_native — popen() the command, capture combined stdout+stderr,
// return banner + body. No timeout (caller's responsibility) — same
// limitation as the .jdb version.

Value tool_jdb_run_native(VM&, const Value& args) {
    std::string cmd = obj_get_str(args, "command");
    if (cmd.empty()) return make_text_result("command is empty", true);

    auto t0 = std::clock();
    std::string redirected = cmd + " 2>&1";
    FILE* p = MCP_POPEN(redirected.c_str(), "r");
    if (!p) return make_text_result("popen failed: " + cmd, true);
    std::string output;
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof(buf), p)) {
        output.append(buf, n);
    }
    int rc = MCP_PCLOSE(p);
#ifdef _WIN32
    // _pclose returns the spawned process's exit code directly (or -1 on error).
    int exit_code = rc;
#else
    int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
    auto t1 = std::clock();
    int elapsed_ms = (int)(((t1 - t0) * 1000) / CLOCKS_PER_SEC);

    std::string banner = "[exit=" + std::to_string(exit_code)
                       + " " + std::to_string(elapsed_ms) + "ms] " + cmd;
    std::string body = output.empty() ? banner : (banner + "\n" + output);
    return make_text_result(body, exit_code != 0);
}

// ── Tool descriptors (served by tools/list) ─────────────────────
// Keep names + descriptions byte-for-byte aligned with mcp/server.jdb's
// BUILD_TOOLS so existing client prompts keep working unchanged.

Value build_input_schema(const std::vector<std::pair<std::string, std::string>>& props,
                         const std::vector<std::string>& required) {
    Value schema = Value::make_object();
    obj_set(schema, "type", make_string_v("object"));

    Value properties = Value::make_object();
    for (auto& [name, desc] : props) {
        Value p = Value::make_object();
        obj_set(p, "type", make_string_v("string"));
        obj_set(p, "description", make_string_v(desc));
        obj_set(properties, name, std::move(p));
    }
    obj_set(schema, "properties", std::move(properties));

    if (!required.empty()) {
        Value req = Value::make_array();
        for (auto& r : required) req.as_array()->elements.push_back(make_string_v(r));
        obj_set(schema, "required", std::move(req));
    }
    return schema;
}

Value tool_descriptor(const std::string& name, const std::string& desc, Value schema) {
    Value t = Value::make_object();
    obj_set(t, "name", make_string_v(name));
    obj_set(t, "description", make_string_v(desc));
    obj_set(t, "inputSchema", std::move(schema));
    return t;
}

// ── User-defined tool registry ──────────────────────────────────
//
// Loaded at startup from a directory of JSON manifests. Each manifest:
//   { "name": ..., "description": ..., "inputSchema": {...},
//     "module": "path/to/module.jdb", "handler": "func_name" }
//
// At startup we EXECUTE each unique module on the persistent VM so the
// handler FUNCs are defined globally. tools/list appends each user tool
// after the built-ins; tools/call dispatches by name into the handler
// FUNC, passing the call's "arguments" object as a JSON string (the
// handler is responsible for JSON.PARSE$-ing it if it cares).

struct UserTool {
    std::string name;
    std::string description;
    Value       input_schema;   // OBJECT
    std::string handler_fn;
    std::string module_path;
};
static std::vector<UserTool>            g_user_tools;
static std::unordered_set<std::string>  g_user_modules_loaded;

void load_user_tools(VM& vm, const std::string& dir) {
    if (dir.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        log_line("user-tools dir not found: " + dir);
        return;
    }
    // Pass 1: parse manifests.
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        if (!f.is_open()) continue;
        std::stringstream ss; ss << f.rdbuf();
        try {
            Value manifest = parse_json(vm, ss.str());
            UserTool t;
            t.name = obj_get_str(manifest, "name");
            t.description = obj_get_str(manifest, "description");
            if (Value* sch = obj_get(manifest, "inputSchema")) {
                t.input_schema = *sch;
            } else {
                t.input_schema = build_input_schema({}, {});
            }
            t.handler_fn = obj_get_str(manifest, "handler");
            t.module_path = obj_get_str(manifest, "module");
            if (t.name.empty() || t.handler_fn.empty()) {
                log_line("manifest missing name or handler: " + entry.path().string());
                continue;
            }
            log_line("registered user tool: " + t.name +
                     " → " + t.handler_fn +
                     " (in " + t.module_path + ")");
            g_user_tools.push_back(std::move(t));
        } catch (const std::exception& e) {
            log_line("manifest parse error in " + entry.path().string() +
                     ": " + e.what());
        }
    }
    // Pass 2: pre-load each unique module on the VM.
    for (auto& t : g_user_tools) {
        if (t.module_path.empty()) continue;
        if (g_user_modules_loaded.count(t.module_path)) continue;
        std::ifstream f(t.module_path);
        if (!f.is_open()) {
            log_line("module not found: " + t.module_path);
            continue;
        }
        std::stringstream ss; ss << f.rdbuf();
        try {
            run_on_vm(vm, ss.str());
            g_user_modules_loaded.insert(t.module_path);
            log_line("loaded module: " + t.module_path);
        } catch (const std::exception& e) {
            log_line("module load error in " + t.module_path + ": " + e.what());
        }
    }
}

// Replace single quotes inside the JSON args so they survive being
// embedded in a jdBasic string literal. Mirrors the SQLite-string
// escape used elsewhere in deusexmachina.
std::string escape_for_basic_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') { out += "\\\""; continue; }
        if (c == '\\') { out += "\\\\"; continue; }
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        out += c;
    }
    return out;
}

Value invoke_user_tool(VM& vm, const UserTool& t, const Value& args) {
    // Serialize args to JSON, expose as a global the handler can parse.
    std::string args_json = stringify_json(vm, args);
    vm.set_global("__MCP_ARGS$", Value::make_string(args_json));
    vm.set_global("__MCP_RESULT$", Value::make_string(""));
    // handler_fn(args_json_string$) → string
    std::string src = "__MCP_RESULT$ = " + t.handler_fn + "(__MCP_ARGS$)\n";
    try {
        run_on_vm(vm, src);
    } catch (const std::exception& e) {
        return make_text_result(std::string("user tool '") + t.name +
                                "' threw: " + e.what(), true);
    }
    // Read the result global back.
    auto& gnames = vm.get_global_names();
    auto& globals = vm.get_globals();
    auto it = gnames.find("__MCP_RESULT$");
    if (it == gnames.end() || it->second >= globals.size()) {
        return make_text_result("", false);
    }
    const Value& r = globals[it->second];
    std::string out;
    if (r.type == ValueType::STRING && r.as_string()) {
        out = r.as_string()->data;
    } else {
        out = r.to_string();
    }
    return make_text_result(out, false);
}

Value build_tools() {
    Value tools = Value::make_array();
    auto& a = tools.as_array()->elements;

    a.push_back(tool_descriptor(
        "jdb_eval",
        "Execute jdBasic statements on the persistent VM and return any captured stdout. Variables defined here persist between calls.",
        build_input_schema({{"code", "jdBasic source to EXECUTE. Use PRINT to surface values."}}, {"code"})));

    a.push_back(tool_descriptor(
        "jdb_check",
        "Validate jdBasic source by running Lex + Parse only. Returns 'ok' if valid, or the parse-error message otherwise. Does NOT execute the code or touch the persistent VM.",
        build_input_schema({{"code", "jdBasic source to validate."}}, {"code"})));

    a.push_back(tool_descriptor(
        "jdb_load",
        "Read a jdBasic source file (path is relative to the server's working directory) and EXECUTE its contents on the persistent VM. Returns captured stdout, or isError on read/parse/runtime failure.",
        build_input_schema({{"path", "Relative path to a .jdb file."}}, {"path"})));

    a.push_back(tool_descriptor(
        "jdb_resume",
        "Continue a script that paused via the STOP statement. While paused, the stopped frame's locals are exposed as globals so jdb_eval can inspect/mutate them; jdb_resume folds those changes back and continues execution from the next opcode. Returns any captured stdout. No-op (with a friendly message) if the VM is not stopped.",
        build_input_schema({}, {})));

    a.push_back(tool_descriptor(
        "jdb_stop",
        "Pause a running script (e.g. a long-running jdb_load) by setting an external STOP flag. The VM's dispatch loop checks the flag every ~200 opcodes and stashes state exactly like the in-script STOP statement. The reader thread fast-paths this tool, so the response arrives even while another tool call is still executing. Pair with jdb_eval (to inspect/mutate during the pause) and jdb_resume.",
        build_input_schema({}, {})));

    a.push_back(tool_descriptor(
        "jdb_status",
        "Report the VM's current state: 'running' (worker executing a script — most tools are busy-rejected, call jdb_stop first), 'stopped' (paused at a STOP — eval/resume work), or 'idle' (no script running, ready for jdb_load).",
        build_input_schema({}, {})));

    a.push_back(tool_descriptor(
        "jdb_recompile",
        "Re-read a .jdb file from disk, parse + compile it, and merge its FUNC/SUB definitions into the live VM. Used for live-coding while a script is STOPped: after editing the file, call jdb_recompile, then jdb_resume to continue with the updated code. Same-name FUNC/SUB overwrites; new ones append. The main-chunk's top-level statements are NOT re-applied to the running script (the stopped frames hold the OLD main chunk) — keep iterable logic inside SUBs/FUNCs. If `path` is omitted, defaults to the most recent jdb_load path.",
        build_input_schema({{"path", "Optional .jdb file path. Defaults to the last jdb_load path."}}, {})));

    a.push_back(tool_descriptor(
        "jdb_vars",
        "List user-defined global variables on the persistent VM. Server internals and engine-noise (dotted, __-prefixed) are filtered out.",
        build_input_schema({}, {})));

    a.push_back(tool_descriptor(
        "jdb_funcs",
        "List user-defined FUNC / SUB / ASYNC FUNC currently registered on the persistent VM, with their parameter signatures.",
        build_input_schema({}, {})));

    a.push_back(tool_descriptor(
        "jdb_doc",
        "Look up jdBasic syntax / built-in functions in doc/languages.md. Returns the bullet entry or section that contains the query string (case-insensitive). Up to 8 hits are concatenated.",
        build_input_schema({{"query", "Symbol name or topic substring."}}, {"query"})));

    a.push_back(tool_descriptor(
        "jdb_run_native",
        "Run a command in a child process and capture combined stdout+stderr plus the exit code. No timeout; long-running processes block.",
        build_input_schema({{"command", "Shell command line to execute."}}, {"command"})));

    a.push_back(tool_descriptor(
        "jdb_savews",
        "Persist user-set globals (filtered to skip VM-builtin constants and empty values) plus FUNC/SUB definitions to '<name>.jsws' in the server's working directory. JSON format with shape-preserving array wrapping. Mirrors the REPL's SAVEWS. Useful for project-scoped DSL toolkits — call once per stable milestone, then restore with jdb_loadws at the start of the next session.",
        build_input_schema({{"name", "Workspace name (file '<name>.jsws' is created)."}}, {"name"})));

    a.push_back(tool_descriptor(
        "jdb_loadws",
        "Reset the VM and restore variables + functions from '<name>.jsws'. Falls back to legacy '<name>.jdws' if no .jsws exists. Replaces the current state — anything defined this session is lost unless you saved it first. Mirrors the REPL's LOADWS command.",
        build_input_schema({{"name", "Workspace name (file '<name>.jsws' is read; .jdws as fallback)."}}, {"name"})));

    a.push_back(tool_descriptor(
        "echo",
        "Echo back the message you sent. Connectivity smoke test.",
        build_input_schema({{"message", "Text to echo back"}}, {"message"})));

    // Append user-defined tools registered from --tools <dir>.
    for (auto& t : g_user_tools) {
        a.push_back(tool_descriptor(t.name, t.description, t.input_schema));
    }

    return tools;
}

// ── Dispatch ────────────────────────────────────────────────────

Value dispatch_tool(VM& vm, const std::string& name, const Value& args) {
    if (name == "jdb_eval")       return tool_jdb_eval(vm, args);
    if (name == "jdb_check")      return tool_jdb_check(vm, args);
    if (name == "jdb_load")       return tool_jdb_load(vm, args);
    if (name == "jdb_resume")     return tool_jdb_resume(vm, args);
    if (name == "jdb_stop")       return tool_jdb_stop(vm, args);
    if (name == "jdb_status")     return tool_jdb_status(vm, args);
    if (name == "jdb_recompile")  return tool_jdb_recompile(vm, args);
    if (name == "jdb_vars")       return tool_jdb_vars(vm, args);
    if (name == "jdb_funcs")      return tool_jdb_funcs(vm, args);
    if (name == "jdb_doc")        return tool_jdb_doc(vm, args);
    if (name == "jdb_run_native") return tool_jdb_run_native(vm, args);
    if (name == "jdb_savews")     return tool_jdb_savews(vm, args);
    if (name == "jdb_loadws")     return tool_jdb_loadws(vm, args);
    if (name == "echo")           return tool_echo(vm, args);
    // User tools registered via --tools <dir>.
    for (auto& t : g_user_tools) {
        if (t.name == name) return invoke_user_tool(vm, t, args);
    }
    return make_text_result("Unknown tool: " + name, true);
}

Value handle_rpc(VM& vm, const Value& rpc) {
    Value resp = Value::make_object();
    obj_set(resp, "jsonrpc", make_string_v("2.0"));
    if (Value* id = obj_get(rpc, "id")) obj_set(resp, "id", *id);

    std::string method = obj_get_str(rpc, "method");

    if (method == "initialize") {
        Value result = Value::make_object();
        obj_set(result, "protocolVersion", make_string_v("2024-11-05"));
        Value caps = Value::make_object();
        obj_set(caps, "tools", Value::make_object());
        obj_set(result, "capabilities", std::move(caps));
        Value info = Value::make_object();
        obj_set(info, "name", make_string_v("jdbasic-mcp-stdio"));
        obj_set(info, "version", make_string_v("0.1.0"));
        obj_set(result, "serverInfo", std::move(info));
        obj_set(resp, "result", std::move(result));
        return resp;
    }

    if (method == "tools/list") {
        Value result = Value::make_object();
        obj_set(result, "tools", build_tools());
        obj_set(resp, "result", std::move(result));
        return resp;
    }

    if (method == "tools/call") {
        Value* params = obj_get(rpc, "params");
        if (!params) {
            Value err = Value::make_object();
            obj_set(err, "code", Value::make_i64(-32602));
            obj_set(err, "message", make_string_v("missing params"));
            obj_set(resp, "error", std::move(err));
            return resp;
        }
        std::string tname = obj_get_str(*params, "name");
        Value* targs = obj_get(*params, "arguments");
        Value empty_args = Value::make_object();
        obj_set(resp, "result", dispatch_tool(vm, tname, targs ? *targs : empty_args));
        return resp;
    }

    Value err = Value::make_object();
    obj_set(err, "code", Value::make_i64(-32601));
    obj_set(err, "message", make_string_v("Method not found: " + method));
    obj_set(resp, "error", std::move(err));
    return resp;
}

} // namespace

// ── Reader thread + inbox ───────────────────────────────────────
// The main dispatch thread blocks for the duration of every tool call
// (synchronous run_on_vm). To let an MCP client deliver jdb_stop while
// a script is running, a separate reader thread reads stdin
// continuously, fast-paths jdb_stop (sets vm.stop_requested + replies
// inline), and posts other frames to a queue the main loop drains.
//
// The reader does NOT touch the VM's value system (no parse_json /
// stringify_json from the reader thread) — those allocate via the VM's
// arena and aren't thread-safe. Detection is byte-level on the raw
// frame body; the response is a hand-built JSON literal.

struct McpInbox {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::string> bodies;
    bool eof = false;
};

// Byte-level detection of `tools/call` for `jdb_stop`. We control the
// client (MCP frame format) so we can rely on quoted-key shapes; this
// is a fast-path, the slow path through normal dispatch still works.
bool is_stop_request(const std::string& body, std::string& id_literal) {
    if (body.find("\"name\":\"jdb_stop\"") == std::string::npos) return false;
    if (body.find("\"method\":\"tools/call\"") == std::string::npos) return false;
    auto p = body.find("\"id\":");
    if (p == std::string::npos) { id_literal = "null"; return true; }
    p += 5;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) p++;
    size_t end = p;
    if (p < body.size() && body[p] == '"') {
        end = p + 1;
        while (end < body.size() && body[end] != '"') {
            if (body[end] == '\\' && end + 1 < body.size()) end += 2;
            else end++;
        }
        if (end < body.size()) end++;  // consume closing quote
    } else {
        while (end < body.size() && body[end] != ',' && body[end] != '}'
               && body[end] != ' ' && body[end] != '\t' && body[end] != '\n'
               && body[end] != '\r') end++;
    }
    id_literal = body.substr(p, end - p);
    if (id_literal.empty()) id_literal = "null";
    return true;
}

void reader_loop(VM& vm, McpInbox& inbox) {
    std::string body;
    while (read_frame(body)) {
        std::string id_lit;
        if (is_stop_request(body, id_lit)) {
            vm.stop_requested.store(true);
            // Hand-built response — bypasses parse_json / stringify_json,
            // both of which mutate VM-arena state and aren't safe from a
            // non-VM thread.
            std::string resp = "{\"jsonrpc\":\"2.0\",\"id\":" + id_lit
                + ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"[stop requested]\"}],\"isError\":false}}";
            write_frame(resp);
            log_line("fast-path jdb_stop");
            continue;
        }
        {
            std::lock_guard<std::mutex> g(inbox.m);
            inbox.bodies.push_back(std::move(body));
        }
        inbox.cv.notify_one();
    }
    {
        std::lock_guard<std::mutex> g(inbox.m);
        inbox.eof = true;
    }
    inbox.cv.notify_one();
    log_line("reader: stdin closed");
}

int run_mcp_stdio(VM& vm, const std::string& user_tools_dir) {
#ifdef _WIN32
    // Windows defaults stdin/stdout to text mode, which translates LF↔CRLF
    // and would corrupt JSON containing escaped newlines. Force binary.
    // stderr is fully-buffered by default; switch to unbuffered so the
    // JDBASIC_MCP_LOG=1 stream surfaces in real time.
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif
    capture_boot_set(vm);
    log_line("starting stdio server");
    if (!user_tools_dir.empty()) {
        log_line("scanning user-tools dir: " + user_tools_dir);
        load_user_tools(vm, user_tools_dir);
        log_line("user tools loaded: " + std::to_string(g_user_tools.size()));
    }

    // Persistent VM worker — owns the VM during job execution. SDL
    // thread-affinity requires a single thread for the entire session,
    // not a fresh thread per jdb_load/jdb_resume call.
    g_worker.t = std::thread(worker_loop, std::ref(vm));

    McpInbox inbox;
    std::thread reader([&]{ reader_loop(vm, inbox); });

    while (true) {
        std::string body;
        {
            std::unique_lock<std::mutex> lk(inbox.m);
            inbox.cv.wait(lk, [&]{ return !inbox.bodies.empty() || inbox.eof; });
            if (inbox.bodies.empty() && inbox.eof) break;
            body = std::move(inbox.bodies.front());
            inbox.bodies.pop_front();
        }

        Value rpc;
        try {
            rpc = parse_json(vm, body);
        } catch (const std::exception& e) {
            log_line(std::string("parse error: ") + e.what());
            // Per JSON-RPC, a parse error gets a response with id=null.
            Value resp = Value::make_object();
            obj_set(resp, "jsonrpc", make_string_v("2.0"));
            obj_set(resp, "id", Value::make_none());
            Value err = Value::make_object();
            obj_set(err, "code", Value::make_i64(-32700));
            obj_set(err, "message", make_string_v(std::string("Parse error: ") + e.what()));
            obj_set(resp, "error", std::move(err));
            write_frame(stringify_json(vm, resp));
            continue;
        }

        std::string method = obj_get_str(rpc, "method");
        // Notifications: per JSON-RPC, no response.
        if (method.size() >= 14 && method.compare(0, 14, "notifications/") == 0) {
            log_line("notification " + method);
            continue;
        }

        log_line("rpc " + method);
        Value resp;
        try {
            resp = handle_rpc(vm, rpc);
        } catch (const std::exception& e) {
            log_line(std::string("handler exception: ") + e.what());
            resp = Value::make_object();
            obj_set(resp, "jsonrpc", make_string_v("2.0"));
            if (Value* id = obj_get(rpc, "id")) obj_set(resp, "id", *id);
            Value err = Value::make_object();
            obj_set(err, "code", Value::make_i64(-32603));
            obj_set(err, "message", make_string_v(std::string("Internal error: ") + e.what()));
            obj_set(resp, "error", std::move(err));
        }
        write_frame(stringify_json(vm, resp));
    }

    log_line("stdin closed, exiting");
    if (reader.joinable()) reader.join();
    // Drain any in-flight job, then shut the worker down. We don't try
    // to interrupt — a long-running game loop will exit on its own
    // (END or running=0) or via stop_requested if the client sent one.
    g_worker.shutdown.store(true);
    g_worker.cv.notify_one();
    if (g_worker.t.joinable()) g_worker.t.join();
    return 0;
}

#endif // MCPSERVER
