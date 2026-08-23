// A web server on the board. The lwIP callbacks run in the radio
// interrupt, so they only collect bytes; a request is handed to its
// jdBasic handler from the main loop, where calling into the VM is
// safe. HTTP.SERVER.POLL does one pass of that, HTTP.SERVER.WAIT keeps
// doing it.
//
// Two connections at a time and a capped request, because the whole
// board has about 56 KB of heap to play with.

#include "../src/vm.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include <string>
#include <map>

#define MAX_CONNS   2
#define MAX_REQUEST 4096

struct Conn {
    struct tcp_pcb* pcb = nullptr;
    std::string in;
    std::string out;
    size_t sent = 0;
    bool ready = false;      // a whole request has arrived
    bool serving = false;    // the handler is running or its answer is going out
};

static Conn g_conns[MAX_CONNS];
static struct tcp_pcb* g_listen = nullptr;
static std::map<std::string, std::string> g_get_routes;
static std::map<std::string, std::string> g_post_routes;
static std::string g_notfound;
static uint32_t g_served = 0;

static void conn_release(Conn* c) {
    if (c->pcb) {
        tcp_arg(c->pcb, nullptr);
        tcp_recv(c->pcb, nullptr);
        tcp_sent(c->pcb, nullptr);
        tcp_err(c->pcb, nullptr);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
        c->pcb = nullptr;
    }
    c->in.clear();
    c->out.clear();
    c->sent = 0;
    c->ready = false;
    c->serving = false;
}

// A request is whole once the headers are in and the body has reached
// the length they promised.
static bool request_complete(const std::string& s) {
    size_t head = s.find("\r\n\r\n");
    if (head == std::string::npos) return false;
    size_t at = s.find("Content-Length:");
    if (at == std::string::npos) at = s.find("content-length:");
    if (at == std::string::npos) return true;
    long want = strtol(s.c_str() + at + 15, nullptr, 10);
    return (long)(s.size() - (head + 4)) >= want;
}

static void srv_err_cb(void* arg, err_t) {
    Conn* c = (Conn*)arg;
    if (!c) return;
    c->pcb = nullptr;          // lwIP freed it already
    conn_release(c);
}

static err_t srv_recv_cb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    Conn* c = (Conn*)arg;
    if (!c) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p) { conn_release(c); return ERR_OK; }
    if (err == ERR_OK && !c->ready) {
        for (struct pbuf* q = p; q; q = q->next) {
            if (c->in.size() + q->len > MAX_REQUEST) break;
            c->in.append((const char*)q->payload, q->len);
        }
        tcp_recved(pcb, p->tot_len);
        if (request_complete(c->in)) c->ready = true;
    }
    pbuf_free(p);
    return ERR_OK;
}

static void srv_send_more(Conn* c) {
    while (c->sent < c->out.size() && c->pcb) {
        size_t room = tcp_sndbuf(c->pcb);
        if (!room) break;
        size_t n = c->out.size() - c->sent;
        if (n > room) n = room;
        if (tcp_write(c->pcb, c->out.data() + c->sent, n, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        c->sent += n;
    }
    if (c->pcb) tcp_output(c->pcb);
    if (c->sent >= c->out.size()) conn_release(c);
}

static err_t srv_sent_cb(void* arg, struct tcp_pcb*, u16_t) {
    Conn* c = (Conn*)arg;
    if (c && c->serving) srv_send_more(c);
    return ERR_OK;
}

static err_t srv_accept_cb(void*, struct tcp_pcb* newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    Conn* slot = nullptr;
    for (auto& c : g_conns) if (!c.pcb) { slot = &c; break; }
    if (!slot) { tcp_abort(newpcb); return ERR_ABRT; }
    slot->pcb = newpcb;
    slot->in.clear();
    slot->out.clear();
    slot->sent = 0;
    slot->ready = false;
    slot->serving = false;
    tcp_arg(newpcb, slot);
    tcp_recv(newpcb, srv_recv_cb);
    tcp_sent(newpcb, srv_sent_cb);
    tcp_err(newpcb, srv_err_cb);
    return ERR_OK;
}

// ── Turning a request into a map, and an answer into bytes ───────────

static std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else out += s[i];
    }
    return out;
}

static Value request_to_map(const std::string& raw) {
    Value m = Value::make_object();
    size_t line_end = raw.find("\r\n");
    std::string line = raw.substr(0, line_end == std::string::npos ? raw.size() : line_end);

    size_t sp1 = line.find(' ');
    size_t sp2 = line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
    std::string method = sp1 == std::string::npos ? "GET" : line.substr(0, sp1);
    std::string target = (sp1 == std::string::npos || sp2 == std::string::npos)
                       ? "/" : line.substr(sp1 + 1, sp2 - sp1 - 1);

    std::string path = target, query;
    size_t qm = target.find('?');
    if (qm != std::string::npos) { path = target.substr(0, qm); query = target.substr(qm + 1); }

    m.as_object()->set("METHOD", Value::make_string(method));
    m.as_object()->set("PATH", Value::make_string(path));

    size_t head = raw.find("\r\n\r\n");
    m.as_object()->set("BODY", Value::make_string(
        head == std::string::npos ? "" : raw.substr(head + 4)));

    // Header names are case-insensitive on the wire, so they arrive
    // lowercased and a handler can look one up without guessing.
    Value hdrs = Value::make_object();
    size_t at = (line_end == std::string::npos) ? raw.size() : line_end + 2;
    while (at < raw.size()) {
        size_t e = raw.find("\r\n", at);
        if (e == std::string::npos || e == at) break;
        std::string h = raw.substr(at, e - at);
        size_t colon = h.find(':');
        if (colon != std::string::npos) {
            std::string k = h.substr(0, colon);
            std::string v = h.substr(colon + 1);
            while (!v.empty() && v[0] == ' ') v.erase(0, 1);
            for (auto& ch : k) ch = (char)tolower((unsigned char)ch);
            hdrs.as_object()->set(k, Value::make_string(v));
        }
        at = e + 2;
    }
    m.as_object()->set("HEADERS", std::move(hdrs));

    Value params = Value::make_object();
    size_t p = 0;
    while (p < query.size()) {
        size_t amp = query.find('&', p);
        std::string pair = query.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
        size_t eq = pair.find('=');
        if (eq != std::string::npos)
            params.as_object()->set(url_decode(pair.substr(0, eq)),
                                    Value::make_string(url_decode(pair.substr(eq + 1))));
        else if (!pair.empty())
            params.as_object()->set(url_decode(pair), Value::make_string(""));
        if (amp == std::string::npos) break;
        p = amp + 1;
    }
    m.as_object()->set("PARAMS", std::move(params));
    return m;
}

static std::string wrap_response(int status, const std::string& ctype,
                                 const std::string& body) {
    const char* text = status == 200 ? "OK" : (status == 404 ? "Not Found" : "Error");
    std::string r = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\n";
    r += "Content-Type: " + ctype + "\r\n";
    r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    // One request per connection. Reverse proxies pool upstream sockets
    // and a pooled-then-closed one reads as a truncated answer.
    r += "Connection: close\r\n\r\n";
    r += body;
    return r;
}

// One pass over the connections: hand each finished request to its
// handler and start the answer on its way. Returns how many were run.
// A handler must not be entered from inside another one, and HTTP.GET$
// pumps this while it waits, so the guard is not theoretical.
static bool g_polling = false;

static int httpd_poll(VM& vm) {
    if (g_polling) return 0;
    g_polling = true;
    struct Ungate { ~Ungate() { g_polling = false; } } ungate;
    int done = 0;
    for (auto& c : g_conns) {
        if (!c.pcb || !c.ready || c.serving) continue;
        c.serving = true;

        Value req = request_to_map(c.in);
        Value* mv = req.as_object()->get("METHOD");
        Value* pv = req.as_object()->get("PATH");
        std::string method = mv ? mv->to_string() : "GET";
        std::string path = pv ? pv->to_string() : "/";

        auto& routes = (method == "POST") ? g_post_routes : g_get_routes;
        auto it = routes.find(path);
        std::string fn = (it != routes.end()) ? it->second : g_notfound;
        int status = (it != routes.end()) ? 200 : 404;

        std::string body;
        std::string ctype = "text/html; charset=utf-8";
        if (fn.empty()) {
            body = "<h1>404</h1>";
        } else {
            try {
                Value out = vm.call_function(fn, { req });
                if (out.type == ValueType::OBJECT) {
                    body = vm.call_function("JSON.STRINGIFY$", { out }).to_string();
                    ctype = "application/json";
                } else {
                    body = out.to_string();
                }
            } catch (const std::exception& e) {
                status = 500;
                body = std::string("<h1>500</h1><pre>") + e.what() + "</pre>";
            }
        }

        c.out = wrap_response(status, ctype, body);
        c.in.clear();
        c.sent = 0;
        cyw43_arch_lwip_begin();
        srv_send_more(&c);
        cyw43_arch_lwip_end();
        g_served++;
        done++;
    }
    return done;
}

// So a client call can keep the server alive while it waits: on one
// core with one thread, a blocking fetch would otherwise stall every
// visitor, and the board could never fetch its own page.
static VM* g_vm = nullptr;

void pico_httpd_pump() {
    // NO_SYS keeps looped-back packets in a queue until someone drains it.
    cyw43_arch_lwip_begin();
    netif_poll_all();
    cyw43_arch_lwip_end();
    if (g_listen && g_vm) httpd_poll(*g_vm);
}

void register_pico_httpd(VM& vm) {
    g_vm = &vm;
    vm.register_native("HTTP.SERVER.ON_GET", 2, 2, [](const std::vector<Value>& args) -> Value {
        g_get_routes[args[0].to_string()] = args[1].to_string();
        return Value();
    });
    vm.register_native("HTTP.SERVER.ON_POST", 2, 2, [](const std::vector<Value>& args) -> Value {
        g_post_routes[args[0].to_string()] = args[1].to_string();
        return Value();
    });
    vm.register_native("HTTP.SERVER.ON_NOTFOUND", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_notfound = args[0].to_string();
        return Value();
    });
    // The board has one address, so there is nothing to bind to but all
    // of it; the port is the only argument that means anything here.
    vm.register_native("HTTP.SERVER.START", 1, 2, [](const std::vector<Value>& args) -> Value {
        int port = (int)args[0].to_double();
        cyw43_arch_lwip_begin();
        // A program that ended without stopping the server would
        // otherwise hold the port until the next reboot, and every
        // later run would be refused.
        for (auto& c : g_conns) conn_release(&c);
        if (g_listen) { tcp_close(g_listen); g_listen = nullptr; }
        struct tcp_pcb* p = tcp_new();
        err_t rc = p ? tcp_bind(p, IP_ANY_TYPE, (u16_t)port) : ERR_MEM;
        if (rc == ERR_OK) {
            g_listen = tcp_listen_with_backlog(p, MAX_CONNS);
            if (g_listen) tcp_accept(g_listen, srv_accept_cb);
            else tcp_abort(p);
        } else if (p) {
            tcp_abort(p);
        }
        cyw43_arch_lwip_end();
        return Value::make_i64(g_listen ? 0 : -1);
    });
    vm.register_native("HTTP.SERVER.STOP", 0, 0, [](const std::vector<Value>&) -> Value {
        cyw43_arch_lwip_begin();
        for (auto& c : g_conns) conn_release(&c);
        if (g_listen) { tcp_close(g_listen); g_listen = nullptr; }
        cyw43_arch_lwip_end();
        return Value();
    });
    vm.register_native("HTTP.SERVER.POLL", 0, 0, [&vm](const std::vector<Value>&) -> Value {
        return Value::make_i64(httpd_poll(vm));
    });
    // Serve until the time runs out, or for good when no time is given.
    vm.register_native("HTTP.SERVER.WAIT", 0, 1, [&vm](const std::vector<Value>& args) -> Value {
        int ms = args.size() >= 1 ? (int)args[0].to_double() : 0;
        uint32_t start = to_ms_since_boot(get_absolute_time());
        for (;;) {
            httpd_poll(vm);
            if (vm.is_halted) break;
            if (ms > 0 && (int)(to_ms_since_boot(get_absolute_time()) - start) >= ms) break;
            // Without this a long wait owns the board until it expires:
            // nothing else here reads the keyboard.
            int key = getchar_timeout_us(0);
            if (key == 0x1B || key == 3) break;
            sleep_ms(5);
        }
        return Value::make_i64((int64_t)g_served);
    });
    vm.register_native("HTTP.SERVER.SERVED", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)g_served);
    });
}
