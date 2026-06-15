#ifdef HTTP
// HTTP Client + Server for jdBasic
// Only compiled when HTTP is defined
// Requires: httplib.h (cpp-httplib), OpenSSL for HTTPS

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "value.h"
#include "vm.h"

// httplib.h must come after our headers to avoid conflicts
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include "httplib.h"

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <iostream>
#include "async_task.h"
#include <unordered_map>

// ── Shared state ─────────────────────────────────────────────

static std::unordered_map<std::string, std::string> g_http_headers;
static std::unordered_map<std::string, std::string> g_http_cookies;
static std::vector<std::pair<std::string, std::string>> g_http_params;
static int g_http_last_status = 0;
static int g_http_timeout_sec = 10;
static bool g_http_follow = true;
static std::mutex g_http_mutex;

// Server state
static std::unique_ptr<httplib::Server> g_server;
static std::thread g_server_thread;
static std::mutex g_server_mutex;
// Serialises every HTTP handler's access to g_server_vm. httplib spawns a
// thread per accepted request, but the embedded VM is single-threaded —
// without this lock two concurrent POSTs would race on the shared bytecode
// (Phase-3 commit message has the gory details). Held across the user
// handler and the auto-JSON-encode call that follows.
static std::mutex g_server_vm_mutex;
static VM* g_server_vm = nullptr;
static std::unordered_map<std::string, std::string> g_get_handlers;  // path → func name
static std::unordered_map<std::string, std::string> g_post_handlers;

// ── Helpers ──────────────────────────────────────────────────

static std::string url_encode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

struct HttpSnapshot {
    httplib::Headers headers;     // custom headers + Cookie
    std::vector<std::pair<std::string,std::string>> params;
    int timeout_sec;
    bool follow;
};

static HttpSnapshot snapshot_client_state() {
    HttpSnapshot s;
    std::lock_guard<std::mutex> lock(g_http_mutex);
    for (auto& [k, v] : g_http_headers) s.headers.emplace(k, v);
    if (!g_http_cookies.empty()) {
        std::string cookie;
        bool first = true;
        for (auto& [k, v] : g_http_cookies) {
            if (!first) cookie += "; ";
            cookie += k + "=" + v;
            first = false;
        }
        s.headers.emplace("Cookie", cookie);
    }
    s.params = g_http_params;
    s.timeout_sec = g_http_timeout_sec;
    s.follow = g_http_follow;
    return s;
}

// Append g_http_params as a query string to path (combining with any existing ?).
static std::string apply_query_params(const std::string& path,
                                      const std::vector<std::pair<std::string,std::string>>& params) {
    if (params.empty()) return path;
    std::string out = path;
    bool has_q = (out.find('?') != std::string::npos);
    for (auto& [k, v] : params) {
        out += (has_q ? '&' : '?');
        out += url_encode(k) + "=" + url_encode(v);
        has_q = true;
    }
    return out;
}

// Parse a URL into (use_ssl, host, path). Throws on malformed input.
static void parse_url(const std::string& url, bool& use_ssl,
                      std::string& host, std::string& path) {
    use_ssl = (url.substr(0, 6) == "https:");
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) throw std::runtime_error("HTTP: Invalid URL");
    std::string host_part = url.substr(scheme_end + 3);
    size_t path_start = host_part.find('/');
    host = (path_start != std::string::npos) ? host_part.substr(0, path_start) : host_part;
    path = (path_start != std::string::npos) ? host_part.substr(path_start) : "/";
}

template <typename ClientT>
static void configure_client(ClientT& cli, const HttpSnapshot& s) {
    cli.set_follow_location(s.follow);
    cli.set_connection_timeout(s.timeout_sec, 0);
    cli.set_read_timeout(s.timeout_sec, 0);
    cli.set_write_timeout(s.timeout_sec, 0);
}

// If the handler returned a map carrying __http_status, copy its
// status / __http_body / __http_headers / __http_content_type fields onto
// the outgoing response and return true. Otherwise return false so the
// caller can fall back to the legacy auto-JSON / auto-HTML path.
template <typename RespT>
static bool apply_rich_response(const Value& result, RespT& res) {
    if (result.type != ValueType::OBJECT || !result.as_object()) return false;
    auto* obj = result.as_object();
    Value* status_v = obj->get("__http_status");
    if (!status_v) return false;

    res.status = (int)status_v->to_int();

    if (Value* headers_v = obj->get("__http_headers")) {
        if (headers_v->type == ValueType::OBJECT && headers_v->as_object()) {
            for (auto& [k, v] : headers_v->as_object()->fields) {
                res.set_header(k, v.type == ValueType::STRING
                                       ? v.as_string()->data
                                       : v.to_string());
            }
        }
    }

    Value* body_v = obj->get("__http_body");
    Value* ct_v   = obj->get("__http_content_type");
    std::string content_type = (ct_v && ct_v->type == ValueType::STRING)
                                   ? ct_v->as_string()->data
                                   : "application/json";
    if (body_v) {
        const std::string body_str = (body_v->type == ValueType::STRING)
                                         ? body_v->as_string()->data
                                         : body_v->to_string();
        res.set_content(body_str, content_type.c_str());
    } else if (ct_v) {
        res.set_content("", content_type.c_str());
    }
    return true;
}

// Compact one-line console log for an incoming request — body is clipped to
// keep the server console scannable. Pulls Mcp-Session-Id out separately
// because that's the header we care about most for MCP debugging.
static void log_request(const httplib::Request& req) {
    constexpr size_t kMax = 240;
    std::string body_short = req.body.size() <= kMax
                                 ? req.body
                                 : req.body.substr(0, kMax) + "...";
    for (auto& c : body_short) if (c == '\n' || c == '\r') c = ' ';
    std::string sid;
    auto it = req.headers.find("Mcp-Session-Id");
    if (it != req.headers.end()) sid = it->second;
    std::cerr << "[" << req.method << " " << req.path << "]"
              << (sid.empty() ? "" : (" sid=" + sid))
              << " body=" << body_short << std::endl;
}

// Build a rich response map: { status, body, headers }.
template <typename RespT>
static Value response_to_map(const RespT& res) {
    Value m = Value::make_object();
    m.as_object()->set("status", Value::make_i64(res->status));
    m.as_object()->set("body", Value::make_string(res->body));
    Value hdrs = Value::make_object();
    for (auto& [k, v] : res->headers) hdrs.as_object()->set(k, Value::make_string(v));
    m.as_object()->set("headers", std::move(hdrs));
    return m;
}

static Value request_to_map(const httplib::Request& req) {
    Value m = Value::make_object();
    m.as_object()->set("PATH", Value::make_string(req.path));
    m.as_object()->set("METHOD", Value::make_string(req.method));
    m.as_object()->set("BODY", Value::make_string(req.body));

    // Headers as map — normalised to lowercase keys. HTTP header names are
    // case-insensitive (RFC 7230), but jdBasic maps are case-sensitive, so
    // handlers should look up `req.HEADERS{"mcp-session-id"}` regardless of
    // what the client sent on the wire.
    Value hdrs = Value::make_object();
    for (auto& [k, v] : req.headers) {
        std::string lower = k;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
        hdrs.as_object()->set(lower, Value::make_string(v));
    }
    m.as_object()->set("HEADERS", std::move(hdrs));

    // Query params
    Value params = Value::make_object();
    for (auto& [k, v] : req.params)
        params.as_object()->set(k, Value::make_string(v));
    m.as_object()->set("PARAMS", std::move(params));

    return m;
}

// ── Register HTTP natives ────────────────────────────────────

void register_http_builtins(VM& vm) {
    g_server_vm = &vm;

    // ── Client functions ─────────────────────────────────────

    vm.register_native("HTTP.GET$", [](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        bool use_ssl; std::string host, path;
        parse_url(url, use_ssl, host, path);
        HttpSnapshot s = snapshot_client_state();
        path = apply_query_params(path, s.params);

        auto run = [&](auto& cli) -> Value {
            configure_client(cli, s);
            auto res = cli.Get(path, s.headers);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP GET failed: " + host + path);
        };
        if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
        else         { httplib::Client    cli(host); return run(cli); }
    });

    vm.register_native("HTTP.POST$", [](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string content_type = (args.size() >= 3) ? args[2].as_string()->data : "application/json";
        bool use_ssl; std::string host, path;
        parse_url(url, use_ssl, host, path);
        HttpSnapshot s = snapshot_client_state();
        path = apply_query_params(path, s.params);
        // Content-Type is passed as an explicit argument to httplib::Post; strip any
        // duplicate from the custom header set.
        for (auto it = s.headers.begin(); it != s.headers.end(); ) {
            if (it->first == "Content-Type" || it->first == "content-type")
                it = s.headers.erase(it);
            else ++it;
        }

        auto run = [&](auto& cli) -> Value {
            configure_client(cli, s);
            auto res = cli.Post(path, s.headers, data, content_type);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP POST failed: " + host + path);
        };
        if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
        else         { httplib::Client    cli(host); return run(cli); }
    });

    vm.register_native("HTTP.PUT$", [](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string content_type = (args.size() >= 3) ? args[2].as_string()->data : "application/json";
        bool use_ssl; std::string host, path;
        parse_url(url, use_ssl, host, path);
        HttpSnapshot s = snapshot_client_state();
        path = apply_query_params(path, s.params);

        auto run = [&](auto& cli) -> Value {
            configure_client(cli, s);
            auto res = cli.Put(path, s.headers, data, content_type);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP PUT failed: " + host + path);
        };
        if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
        else         { httplib::Client    cli(host); return run(cli); }
    });

    vm.register_native("HTTP.SETHEADER", [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_headers[args[0].as_string()->data] = args[1].as_string()->data;
        return Value::make_none();
    });

    vm.register_native("HTTP.CLEARHEADERS", [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_headers.clear();
        return Value::make_none();
    });

    vm.register_native("HTTP.STATUSCODE", [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::lock_guard<std::mutex> lock(g_http_mutex);
        return Value::make_i64(g_http_last_status);
    });

    // ── Client configuration ─────────────────────────────────

    vm.register_native("HTTP.SETTIMEOUT", [](const std::vector<Value>& args) -> Value {
        double sec = args[0].to_double();
        if (sec < 0) sec = 0;
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_timeout_sec = (int)sec;
        return Value::make_none();
    });

    vm.register_native("HTTP.FOLLOWREDIRECTS", [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_follow = args[0].to_bool();
        return Value::make_none();
    });

    vm.register_native("HTTP.SETPARAM", [](const std::vector<Value>& args) -> Value {
        std::string name = args[0].as_string()->data;
        std::string value = args[1].as_string()->data;
        std::lock_guard<std::mutex> lock(g_http_mutex);
        for (auto& [k, v] : g_http_params) {
            if (k == name) { v = value; return Value::make_none(); }
        }
        g_http_params.emplace_back(name, value);
        return Value::make_none();
    });

    vm.register_native("HTTP.CLEARPARAMS", [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_params.clear();
        return Value::make_none();
    });

    vm.register_native("HTTP.SETCOOKIE", [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_cookies[args[0].as_string()->data] = args[1].as_string()->data;
        return Value::make_none();
    });

    vm.register_native("HTTP.CLEARCOOKIES", [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::lock_guard<std::mutex> lock(g_http_mutex);
        g_http_cookies.clear();
        return Value::make_none();
    });

    vm.register_native("HTTP.GETCOOKIE$", [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_http_mutex);
        auto it = g_http_cookies.find(args[0].as_string()->data);
        return Value::make_string(it != g_http_cookies.end() ? it->second : std::string());
    });

    // HTTP.REQUEST(method$, url$ [, body$ [, content_type$]]) -> object
    // Rich response: { status, body, headers }. Never throws on HTTP-level
    // errors (4xx/5xx) — those are surfaced via the `status` field. Transport
    // failures still throw.
    vm.register_native("HTTP.REQUEST", [](const std::vector<Value>& args) -> Value {
        std::string method = args[0].as_string()->data;
        std::string url    = args[1].as_string()->data;
        std::string body   = (args.size() >= 3) ? args[2].as_string()->data : std::string();
        std::string ct     = (args.size() >= 4) ? args[3].as_string()->data : std::string("application/json");
        for (auto& c : method) c = (char)toupper((unsigned char)c);

        bool use_ssl; std::string host, path;
        parse_url(url, use_ssl, host, path);
        HttpSnapshot s = snapshot_client_state();
        path = apply_query_params(path, s.params);
        for (auto it = s.headers.begin(); it != s.headers.end(); ) {
            if (method != "GET" && method != "DELETE" && method != "HEAD" &&
                (it->first == "Content-Type" || it->first == "content-type"))
                it = s.headers.erase(it);
            else ++it;
        }

        auto run = [&](auto& cli) -> Value {
            configure_client(cli, s);
            httplib::Result res(nullptr, httplib::Error::Unknown);
            if (method == "GET")          res = cli.Get(path, s.headers);
            else if (method == "DELETE")  res = cli.Delete(path, s.headers);
            else if (method == "HEAD")    res = cli.Head(path, s.headers);
            else if (method == "POST")    res = cli.Post(path, s.headers, body, ct.c_str());
            else if (method == "PUT")     res = cli.Put(path, s.headers, body, ct.c_str());
            else if (method == "PATCH")   res = cli.Patch(path, s.headers, body, ct.c_str());
            else throw std::runtime_error("HTTP.REQUEST: unknown method " + method);
            if (!res) throw std::runtime_error("HTTP.REQUEST transport failure: " + url);
            { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
            return response_to_map(res);
        };
        if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
        else         { httplib::Client    cli(host); return run(cli); }
    });

    vm.register_native("HTTP.DELETE$", [](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        bool use_ssl; std::string host, path;
        parse_url(url, use_ssl, host, path);
        HttpSnapshot s = snapshot_client_state();
        path = apply_query_params(path, s.params);

        auto run = [&](auto& cli) -> Value {
            configure_client(cli, s);
            auto res = cli.Delete(path, s.headers);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP DELETE failed: " + host + path);
        };
        if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
        else         { httplib::Client    cli(host); return run(cli); }
    });

    // ── Async Client functions ──────────────────────────────
    // These use the same AsyncTask system as ASYNC FUNC / AWAIT

    // Use the async task infrastructure from vm.cpp (declared extern at file scope below)

    auto launch_async = [](std::function<Value()> work) -> Value {
        int task_id = g_async_next_id++;
        auto task = std::make_shared<AsyncTask>();
        task->thread = std::thread([task, work]() {
            try { task->result = work(); }
            catch (const std::exception& e) { task->result = Value::make_string("ERROR: " + std::string(e.what())); }
            task->done = true;
        });
        task->thread.detach();
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          g_async_tasks[task_id] = task; }
        return Value::make_i64(task_id);
    };

    vm.register_native("HTTP.GET_ASYNC$", [launch_async](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        HttpSnapshot s = snapshot_client_state();
        return launch_async([url, s]() -> Value {
            bool use_ssl; std::string host, path;
            parse_url(url, use_ssl, host, path);
            std::string full_path = apply_query_params(path, s.params);
            auto run = [&](auto& cli) -> Value {
                configure_client(cli, s);
                auto res = cli.Get(full_path, s.headers);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
                throw std::runtime_error("HTTP GET failed: " + url);
            };
            if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
            else         { httplib::Client    cli(host); return run(cli); }
        });
    });

    vm.register_native("HTTP.POST_ASYNC$", [launch_async](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string ct = (args.size() >= 3) ? args[2].as_string()->data : "application/json";
        HttpSnapshot s = snapshot_client_state();
        for (auto it = s.headers.begin(); it != s.headers.end(); ) {
            if (it->first == "Content-Type" || it->first == "content-type")
                it = s.headers.erase(it);
            else ++it;
        }
        return launch_async([url, data, ct, s]() -> Value {
            bool use_ssl; std::string host, path;
            parse_url(url, use_ssl, host, path);
            std::string full_path = apply_query_params(path, s.params);
            auto run = [&](auto& cli) -> Value {
                configure_client(cli, s);
                auto res = cli.Post(full_path, s.headers, data, ct);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
                throw std::runtime_error("HTTP POST failed: " + url);
            };
            if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
            else         { httplib::Client    cli(host); return run(cli); }
        });
    });

    vm.register_native("HTTP.PUT_ASYNC$", [launch_async](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string ct = (args.size() >= 3) ? args[2].as_string()->data : "application/json";
        HttpSnapshot s = snapshot_client_state();
        return launch_async([url, data, ct, s]() -> Value {
            bool use_ssl; std::string host, path;
            parse_url(url, use_ssl, host, path);
            std::string full_path = apply_query_params(path, s.params);
            auto run = [&](auto& cli) -> Value {
                configure_client(cli, s);
                auto res = cli.Put(full_path, s.headers, data, ct);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
                throw std::runtime_error("HTTP PUT failed: " + url);
            };
            if (use_ssl) { httplib::SSLClient cli(host); return run(cli); }
            else         { httplib::Client    cli(host); return run(cli); }
        });
    });

    // ── Server functions ─────────────────────────────────────

    vm.register_native("HTTP.SERVER.ON_GET", [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_server_mutex);
        g_get_handlers[args[0].as_string()->data] = args[1].as_string()->data;
        return Value::make_none();
    });

    vm.register_native("HTTP.SERVER.ON_POST", [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_server_mutex);
        g_post_handlers[args[0].as_string()->data] = args[1].as_string()->data;
        return Value::make_none();
    });

    vm.register_native("HTTP.SERVER.START", [&vm](const std::vector<Value>& args) -> Value {
        if (args.empty())
            throw std::runtime_error("HTTP.SERVER.START: port required");
        int port = (int)args[0].to_int();
        // Default bind: localhost only. Pass "0.0.0.0" explicitly to expose
        // the server to the LAN. The default is loopback because anything
        // less is a foot-gun for tools like the MCP server (was 0.0.0.0
        // before — security-relevant default change).
        std::string host = "127.0.0.1";
        if (args.size() >= 2 && args[1].type == ValueType::STRING) {
            host = args[1].as_string()->data;
            if (host.empty()) host = "127.0.0.1";
        }

        std::lock_guard<std::mutex> lock(g_server_mutex);
        if (g_server) throw std::runtime_error("Server already running");

        // Pin g_server_vm to the VM that actually started the server.
        // register_http_builtins() runs once per workspace VM and clobbers
        // g_server_vm each time, so by the time the user calls START from
        // (say) WS1, the global still points at WS4 — and POST handlers
        // would look up the user's FUNC on the wrong VM ("Undefined
        // function: MCP_POST"). Re-binding here makes ON_POST/ON_GET
        // resolve against the workspace that owns the handler functions.
        g_server_vm = &vm;

        g_server = std::make_unique<httplib::Server>();

        // Register all GET handlers
        for (auto& [path, func_name] : g_get_handlers) {
            std::string fn = func_name;
            g_server->Get(path, [fn](const httplib::Request& req, httplib::Response& res) {
                log_request(req);
                try {
                    std::lock_guard<std::mutex> vm_lock(g_server_vm_mutex);
                    Value req_map = request_to_map(req);
                    Value result = g_server_vm->call_function(fn, {req_map});

                    if (apply_rich_response(result, res)) {
                        // Handler controlled status/headers/body.
                    } else if (result.type == ValueType::OBJECT) {
                        // Map → JSON
                        Value json = g_server_vm->call_function("JSON.STRINGIFY$", {result});
                        res.set_content(json.as_string()->data, "application/json");
                    } else {
                        res.set_content(result.to_string(), "text/html");
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[" << req.method << " " << req.path
                              << "] ERROR: " << e.what() << std::endl;
                    res.status = 500;
                    res.set_content(std::string("Error: ") + e.what(), "text/plain");
                }
            });
        }

        // Register all POST handlers
        for (auto& [path, func_name] : g_post_handlers) {
            std::string fn = func_name;
            g_server->Post(path, [fn](const httplib::Request& req, httplib::Response& res) {
                log_request(req);
                try {
                    std::lock_guard<std::mutex> vm_lock(g_server_vm_mutex);
                    Value req_map = request_to_map(req);
                    Value result = g_server_vm->call_function(fn, {req_map});

                    if (apply_rich_response(result, res)) {
                        // Handler controlled status/headers/body.
                    } else if (result.type == ValueType::OBJECT) {
                        Value json = g_server_vm->call_function("JSON.STRINGIFY$", {result});
                        res.set_content(json.as_string()->data, "application/json");
                    } else {
                        res.set_content(result.to_string(), "text/html");
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[" << req.method << " " << req.path
                              << "] ERROR: " << e.what() << std::endl;
                    res.status = 500;
                    res.set_content(std::string("Error: ") + e.what(), "text/plain");
                }
            });
        }

        // Start server in background thread
        auto* srv = g_server.get();
        g_server_thread = std::thread([srv, port, host]() {
            srv->listen(host.c_str(), port);
        });
        g_server_thread.detach();

        // Wait a moment for the server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return Value::make_bool(g_server->is_running());
    });

    vm.register_native("HTTP.SERVER.STOP", [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::lock_guard<std::mutex> lock(g_server_mutex);
        if (g_server) {
            g_server->stop();
            g_server.reset();
        }
        return Value::make_none();
    });

    // Block the calling thread until the server stops, without stepping the
    // VM. Handlers run on httplib worker threads sharing this single VM; a
    // jdBasic `DO ... SLEEP ... LOOP` keep-alive on the main thread would step
    // the VM concurrently with a handler and corrupt it (fatal for handlers
    // that run for seconds, e.g. an LLM query). Parking here in C++ leaves the
    // serialized handlers as the only VM users. Returns when the server is
    // stopped (HTTP.SERVER.STOP from a handler, or process exit).
    vm.register_native("HTTP.SERVER.WAIT", [](const std::vector<Value>& args) -> Value {
        (void)args;
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(g_server_mutex);
                if (!g_server || !g_server->is_running()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        return Value::make_none();
    });
}

#endif // HTTP
