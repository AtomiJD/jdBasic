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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <ctime>

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

void write_frame(const std::string& body) {
    // One JSON object, then '\n'. Flush so the client sees the reply
    // before its handshake timeout fires. NDJSON forbids embedded
    // newlines in the body — JSON.STRINGIFY$ already escapes them.
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

// ── Tools ───────────────────────────────────────────────────────

Value tool_echo(VM&, const Value& args) {
    return make_text_result("Echo: " + obj_get_str(args, "message"), false);
}

Value tool_jdb_eval(VM& vm, const Value& args) {
    std::string code = obj_get_str(args, "code");
    OutputCapture cap(vm);
    try {
        run_on_vm(vm, code + "\n");
        return make_text_result(cap.buf, false);
    } catch (const std::exception& e) {
        std::string err = "Error: ";
        err += e.what();
        std::string body = cap.buf.empty() ? err : (cap.buf + err);
        return make_text_result(body, true);
    }
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

Value tool_jdb_load(VM& vm, const Value& args) {
    std::string path = obj_get_str(args, "path");
    std::ifstream in(path);
    if (!in.is_open()) {
        return make_text_result("Cannot read " + path, true);
    }
    std::stringstream ss; ss << in.rdbuf();
    std::string source = ss.str();

    OutputCapture cap(vm);
    try {
        run_on_vm(vm, source);
        std::string banner = "[loaded " + path + ", " + std::to_string(source.size()) + " bytes]";
        std::string body = cap.buf.empty() ? banner : (banner + "\n" + cap.buf);
        return make_text_result(body, false);
    } catch (const std::exception& e) {
        std::string err = "Error in " + path + ": " + e.what();
        std::string body = cap.buf.empty() ? err : (cap.buf + err);
        return make_text_result(body, true);
    }
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
        "echo",
        "Echo back the message you sent. Connectivity smoke test.",
        build_input_schema({{"message", "Text to echo back"}}, {"message"})));

    return tools;
}

// ── Dispatch ────────────────────────────────────────────────────

Value dispatch_tool(VM& vm, const std::string& name, const Value& args) {
    if (name == "jdb_eval")       return tool_jdb_eval(vm, args);
    if (name == "jdb_check")      return tool_jdb_check(vm, args);
    if (name == "jdb_load")       return tool_jdb_load(vm, args);
    if (name == "jdb_vars")       return tool_jdb_vars(vm, args);
    if (name == "jdb_funcs")      return tool_jdb_funcs(vm, args);
    if (name == "jdb_doc")        return tool_jdb_doc(vm, args);
    if (name == "jdb_run_native") return tool_jdb_run_native(vm, args);
    if (name == "echo")           return tool_echo(vm, args);
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

int run_mcp_stdio(VM& vm) {
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

    std::string body;
    while (read_frame(body)) {
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
    return 0;
}

#endif // MCPSERVER
