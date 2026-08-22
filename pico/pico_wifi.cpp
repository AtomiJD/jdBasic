// WiFi on the W boards: station mode over the cyw43, lwIP underneath
// in threadsafe-background mode - the radio IRQ pumps the stack, so a
// REPL blocked in getchar keeps the connection alive. HTTP.GET$ is a
// plain-HTTP fetch over raw TCP, one transfer at a time.

#include "../src/vm.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"

static bool g_sta_up = false;

static void wifi_sta_up() {
    if (!g_sta_up) {
        cyw43_arch_enable_sta_mode();
        g_sta_up = true;
    }
}

// ── DNS, synchronous ─────────────────────────────────────────────────

struct DnsWait {
    volatile bool done = false;
    volatile bool ok = false;
    ip_addr_t addr;
};

static void dns_cb(const char* name, const ip_addr_t* ipaddr, void* arg) {
    (void)name;
    DnsWait* w = (DnsWait*)arg;
    if (ipaddr) { w->addr = *ipaddr; w->ok = true; }
    w->done = true;
}

static bool resolve(const char* host, ip_addr_t* out, int timeout_ms) {
    DnsWait w;
    cyw43_arch_lwip_begin();
    err_t rc = dns_gethostbyname(host, &w.addr, dns_cb, &w);
    cyw43_arch_lwip_end();
    if (rc == ERR_OK) { *out = w.addr; return true; }
    if (rc != ERR_INPROGRESS) return false;
    for (int t = 0; t < timeout_ms && !w.done; t += 10) sleep_ms(10);
    if (w.done && w.ok) { *out = w.addr; return true; }
    return false;
}

// ── One HTTP transfer ────────────────────────────────────────────────

struct HttpXfer {
    struct tcp_pcb* pcb = nullptr;
    std::string request;
    std::string response;
    size_t sent = 0;
    volatile bool connected = false;
    volatile bool closed = false;
    volatile bool failed = false;
};

static void http_finish(HttpXfer* x) {
    if (x->pcb) {
        tcp_arg(x->pcb, nullptr);
        tcp_recv(x->pcb, nullptr);
        tcp_sent(x->pcb, nullptr);
        tcp_err(x->pcb, nullptr);
        if (tcp_close(x->pcb) != ERR_OK) tcp_abort(x->pcb);
        x->pcb = nullptr;
    }
    x->closed = true;
}

static void http_err_cb(void* arg, err_t err) {
    (void)err;
    HttpXfer* x = (HttpXfer*)arg;
    if (!x) return;
    x->pcb = nullptr;
    x->failed = true;
    x->closed = true;
}

static err_t http_recv_cb(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    HttpXfer* x = (HttpXfer*)arg;
    if (!x) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p) { http_finish(x); return ERR_OK; }
    if (err == ERR_OK) {
        for (struct pbuf* q = p; q; q = q->next)
            x->response.append((const char*)q->payload, q->len);
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static void http_send_more(HttpXfer* x) {
    while (x->sent < x->request.size()) {
        size_t room = tcp_sndbuf(x->pcb);
        if (!room) break;
        size_t n = x->request.size() - x->sent;
        if (n > room) n = room;
        if (tcp_write(x->pcb, x->request.data() + x->sent, n, TCP_WRITE_FLAG_COPY) != ERR_OK)
            break;
        x->sent += n;
    }
    tcp_output(x->pcb);
}

static err_t http_sent_cb(void* arg, struct tcp_pcb* pcb, u16_t len) {
    (void)pcb; (void)len;
    HttpXfer* x = (HttpXfer*)arg;
    if (x && x->sent < x->request.size()) http_send_more(x);
    return ERR_OK;
}

static err_t http_connected_cb(void* arg, struct tcp_pcb* pcb, err_t err) {
    (void)pcb;
    HttpXfer* x = (HttpXfer*)arg;
    if (!x) return ERR_OK;
    if (err != ERR_OK) { x->failed = true; x->closed = true; return ERR_OK; }
    x->connected = true;
    http_send_more(x);
    return ERR_OK;
}

// url: http://host[:port]/path - returns the body, or an empty string
// on any failure.
static std::string http_get(const std::string& url, int timeout_ms) {
    std::string rest;
    if (url.rfind("http://", 0) == 0) rest = url.substr(7);
    else if (url.find("://") == std::string::npos) rest = url;
    else return "";

    std::string host = rest, path = "/";
    size_t slash = rest.find('/');
    if (slash != std::string::npos) { host = rest.substr(0, slash); path = rest.substr(slash); }
    int port = 80;
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = atoi(host.c_str() + colon + 1);
        host = host.substr(0, colon);
    }
    if (host.empty()) return "";

    ip_addr_t addr;
    if (!resolve(host.c_str(), &addr, 5000)) return "";

    HttpXfer x;
    x.request = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                "\r\nConnection: close\r\nUser-Agent: jdBasic-pico\r\n\r\n";

    cyw43_arch_lwip_begin();
    x.pcb = tcp_new();
    if (x.pcb) {
        tcp_arg(x.pcb, &x);
        tcp_recv(x.pcb, http_recv_cb);
        tcp_sent(x.pcb, http_sent_cb);
        tcp_err(x.pcb, http_err_cb);
        if (tcp_connect(x.pcb, &addr, port, http_connected_cb) != ERR_OK) {
            tcp_abort(x.pcb);
            x.pcb = nullptr;
        }
    }
    cyw43_arch_lwip_end();
    if (!x.pcb) return "";

    for (int t = 0; t < timeout_ms && !x.closed; t += 10) sleep_ms(10);

    cyw43_arch_lwip_begin();
    if (!x.closed) { x.failed = true; http_finish(&x); }
    cyw43_arch_lwip_end();

    if (x.failed && x.response.empty()) return "";
    size_t body = x.response.find("\r\n\r\n");
    return body == std::string::npos ? x.response : x.response.substr(body + 4);
}

// ── Registration ─────────────────────────────────────────────────────

void register_pico_wifi(VM& vm) {
    vm.register_native("WIFI.CONNECT", 2, 3, [](const std::vector<Value>& args) -> Value {
        wifi_sta_up();
        int timeout = args.size() >= 3 ? (int)args[2].to_double() : 30000;
        int rc = cyw43_arch_wifi_connect_timeout_ms(
            args[0].to_string().c_str(), args[1].to_string().c_str(),
            CYW43_AUTH_WPA2_MIXED_PSK, timeout);
        return Value::make_i64(rc);
    });
    vm.register_native("WIFI.STATUS", 0, 0, [](const std::vector<Value>&) -> Value {
        wifi_sta_up();
        return Value::make_i64(cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA));
    });
    vm.register_native("WIFI.IP$", 0, 0, [](const std::vector<Value>&) -> Value {
        struct netif* n = netif_default;
        if (!n || !netif_is_up(n)) return Value::make_string("");
        return Value::make_string(ip4addr_ntoa(netif_ip4_addr(n)));
    });
    vm.register_native("HTTP.GET$", 1, 2, [](const std::vector<Value>& args) -> Value {
        int timeout = args.size() >= 2 ? (int)args[1].to_double() : 10000;
        return Value::make_string(http_get(args[0].to_string(), timeout));
    });
}
