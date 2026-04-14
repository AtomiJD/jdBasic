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
static int g_http_last_status = 0;
static std::mutex g_http_mutex;

// Server state
static std::unique_ptr<httplib::Server> g_server;
static std::thread g_server_thread;
static std::mutex g_server_mutex;
static VM* g_server_vm = nullptr;
static std::unordered_map<std::string, std::string> g_get_handlers;  // path → func name
static std::unordered_map<std::string, std::string> g_post_handlers;

// ── Helpers ──────────────────────────────────────────────────

static httplib::Headers get_custom_headers() {
    httplib::Headers h;
    std::lock_guard<std::mutex> lock(g_http_mutex);
    for (auto& [k, v] : g_http_headers) h.emplace(k, v);
    return h;
}

static Value request_to_map(const httplib::Request& req) {
    Value m = Value::make_object();
    m.as_object()->set("PATH", Value::make_string(req.path));
    m.as_object()->set("METHOD", Value::make_string(req.method));
    m.as_object()->set("BODY", Value::make_string(req.body));

    // Headers as map
    Value hdrs = Value::make_object();
    for (auto& [k, v] : req.headers)
        hdrs.as_object()->set(k, Value::make_string(v));
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

        // Parse URL: scheme://host[:port]/path
        bool use_ssl = (url.substr(0, 5) == "https");
        size_t scheme_end = url.find("://");
        if (scheme_end == std::string::npos) throw std::runtime_error("HTTP: Invalid URL");
        std::string host_part = url.substr(scheme_end + 3);
        size_t path_start = host_part.find('/');
        std::string host = (path_start != std::string::npos) ? host_part.substr(0, path_start) : host_part;
        std::string path = (path_start != std::string::npos) ? host_part.substr(path_start) : "/";

        auto headers = get_custom_headers();

        if (use_ssl) {
            httplib::SSLClient cli(host);
            cli.set_follow_location(true);
            cli.set_connection_timeout(10, 0);
            httplib::Headers hdr_map(headers.begin(), headers.end());
            auto res = cli.Get(path, hdr_map);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP GET failed: " + host + path);
        } else {
            httplib::Client cli(host);
            cli.set_follow_location(true);
            cli.set_connection_timeout(10, 0);
            auto res = cli.Get(path, headers);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP GET failed: " + host + path);
        }
    });

    vm.register_native("HTTP.POST$", [](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string content_type = (args.size() >= 3) ? args[2].as_string()->data : "application/json";

        bool use_ssl = (url.substr(0, 5) == "https");
        size_t scheme_end = url.find("://");
        if (scheme_end == std::string::npos) throw std::runtime_error("HTTP: Invalid URL");
        std::string host_part = url.substr(scheme_end + 3);
        size_t path_start = host_part.find('/');
        std::string host = (path_start != std::string::npos) ? host_part.substr(0, path_start) : host_part;
        std::string path = (path_start != std::string::npos) ? host_part.substr(path_start) : "/";

        auto headers = get_custom_headers();
        // Remove Content-Type from custom headers — httplib sets it from the parameter
        for (auto it = headers.begin(); it != headers.end(); ) {
            if (it->first == "Content-Type" || it->first == "content-type")
                it = headers.erase(it);
            else ++it;
        }

        if (use_ssl) {
            httplib::SSLClient cli(host);
            cli.set_follow_location(true);
            httplib::Headers hdr_map(headers.begin(), headers.end());
            auto res = cli.Post(path, hdr_map, data, content_type);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP POST failed");
        } else {
            httplib::Client cli(host);
            cli.set_follow_location(true);
            auto res = cli.Post(path, headers, data, content_type);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP POST failed");
        }
    });

    vm.register_native("HTTP.PUT$", [](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string content_type = (args.size() >= 3) ? args[2].as_string()->data : "application/json";

        bool use_ssl = (url.substr(0, 5) == "https");
        size_t scheme_end = url.find("://");
        if (scheme_end == std::string::npos) throw std::runtime_error("HTTP: Invalid URL");
        std::string host_part = url.substr(scheme_end + 3);
        size_t path_start = host_part.find('/');
        std::string host = (path_start != std::string::npos) ? host_part.substr(0, path_start) : host_part;
        std::string path = (path_start != std::string::npos) ? host_part.substr(path_start) : "/";

        if (use_ssl) {
            httplib::SSLClient cli(host);
            cli.set_follow_location(true);
            auto res = cli.Put(path, data, content_type);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
        } else {
            httplib::Client cli(host);
            cli.set_follow_location(true);
            auto res = cli.Put(path, data, content_type);
            if (res) {
                std::lock_guard<std::mutex> lock(g_http_mutex);
                g_http_last_status = res->status;
                return Value::make_string(res->body);
            }
        }
        throw std::runtime_error("HTTP PUT failed");
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
        httplib::Headers hdrs = get_custom_headers();
        return launch_async([url, hdrs]() -> Value {
            bool use_ssl = (url.substr(0, 5) == "https");
            size_t se = url.find("://"); if (se == std::string::npos) throw std::runtime_error("Invalid URL");
            std::string hp = url.substr(se + 3);
            size_t ps = hp.find('/');
            std::string host = (ps != std::string::npos) ? hp.substr(0, ps) : hp;
            std::string path = (ps != std::string::npos) ? hp.substr(ps) : "/";
            if (use_ssl) {
                httplib::SSLClient cli(host); cli.set_follow_location(true); cli.set_connection_timeout(30, 0);
                httplib::Headers hm(hdrs.begin(), hdrs.end());
                auto res = cli.Get(path, hm);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
            } else {
                httplib::Client cli(host); cli.set_follow_location(true); cli.set_connection_timeout(30, 0);
                auto res = cli.Get(path, hdrs);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
            }
            throw std::runtime_error("HTTP GET failed: " + url);
        });
    });

    vm.register_native("HTTP.POST_ASYNC$", [launch_async](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string ct = (args.size() >= 3) ? args[2].as_string()->data : "application/json";
        httplib::Headers hdrs = get_custom_headers();
        return launch_async([url, data, ct, hdrs]() -> Value {
            bool use_ssl = (url.substr(0, 5) == "https");
            size_t se = url.find("://"); if (se == std::string::npos) throw std::runtime_error("Invalid URL");
            std::string hp = url.substr(se + 3);
            size_t ps = hp.find('/');
            std::string host = (ps != std::string::npos) ? hp.substr(0, ps) : hp;
            std::string path = (ps != std::string::npos) ? hp.substr(ps) : "/";
            if (use_ssl) {
                httplib::SSLClient cli(host); cli.set_follow_location(true);
                httplib::Headers hm(hdrs.begin(), hdrs.end());
                auto res = cli.Post(path, hm, data, ct);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
            } else {
                httplib::Client cli(host); cli.set_follow_location(true);
                auto res = cli.Post(path, hdrs, data, ct);
                if (res) {
                    { std::lock_guard<std::mutex> lock(g_http_mutex); g_http_last_status = res->status; }
                    return Value::make_string(res->body);
                }
            }
            throw std::runtime_error("HTTP POST failed: " + url);
        });
    });

    vm.register_native("HTTP.PUT_ASYNC$", [launch_async](const std::vector<Value>& args) -> Value {
        std::string url = args[0].as_string()->data;
        std::string data = args[1].as_string()->data;
        std::string ct = (args.size() >= 3) ? args[2].as_string()->data : "application/json";
        return launch_async([url, data, ct]() -> Value {
            bool use_ssl = (url.substr(0, 5) == "https");
            size_t se = url.find("://"); if (se == std::string::npos) throw std::runtime_error("Invalid URL");
            std::string hp = url.substr(se + 3);
            size_t ps = hp.find('/');
            std::string host = (ps != std::string::npos) ? hp.substr(0, ps) : hp;
            std::string path = (ps != std::string::npos) ? hp.substr(ps) : "/";
            if (use_ssl) {
                httplib::SSLClient cli(host); cli.set_follow_location(true);
                auto res = cli.Put(path, data, ct);
                if (res) return Value::make_string(res->body);
            } else {
                httplib::Client cli(host); cli.set_follow_location(true);
                auto res = cli.Put(path, data, ct);
                if (res) return Value::make_string(res->body);
            }
            throw std::runtime_error("HTTP PUT failed: " + url);
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
        int port = (int)args[0].to_int();

        std::lock_guard<std::mutex> lock(g_server_mutex);
        if (g_server) throw std::runtime_error("Server already running");

        g_server = std::make_unique<httplib::Server>();

        // Register all GET handlers
        for (auto& [path, func_name] : g_get_handlers) {
            std::string fn = func_name;
            g_server->Get(path, [fn](const httplib::Request& req, httplib::Response& res) {
                try {
                    Value req_map = request_to_map(req);
                    Value result = g_server_vm->call_function(fn, {req_map});

                    if (result.type == ValueType::OBJECT && !false) {
                        // Map → JSON
                        Value json = g_server_vm->call_function("JSON.STRINGIFY$", {result});
                        res.set_content(json.as_string()->data, "application/json");
                    } else {
                        res.set_content(result.to_string(), "text/html");
                    }
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("Error: ") + e.what(), "text/plain");
                }
            });
        }

        // Register all POST handlers
        for (auto& [path, func_name] : g_post_handlers) {
            std::string fn = func_name;
            g_server->Post(path, [fn](const httplib::Request& req, httplib::Response& res) {
                try {
                    Value req_map = request_to_map(req);
                    Value result = g_server_vm->call_function(fn, {req_map});

                    if (result.type == ValueType::OBJECT && !false) {
                        Value json = g_server_vm->call_function("JSON.STRINGIFY$", {result});
                        res.set_content(json.as_string()->data, "application/json");
                    } else {
                        res.set_content(result.to_string(), "text/html");
                    }
                } catch (const std::exception& e) {
                    res.status = 500;
                    res.set_content(std::string("Error: ") + e.what(), "text/plain");
                }
            });
        }

        // Start server in background thread
        auto* srv = g_server.get();
        g_server_thread = std::thread([srv, port]() {
            srv->listen("0.0.0.0", port);
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
}

#endif // HTTP
