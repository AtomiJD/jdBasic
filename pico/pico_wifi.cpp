// WiFi on the W boards: station mode over the cyw43, lwIP underneath
// in threadsafe-background mode - the radio IRQ pumps the stack, so a
// REPL blocked in getchar keeps the connection alive. HTTP.GET$ is a
// plain-HTTP fetch over raw TCP, one transfer at a time.

#include "../src/vm.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include <sys/time.h>

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

// The last transfer's trail, for WIFI.DIAG$: how far it came and what
// the stack said on the way.
static volatile int g_dg_stage = 0;   // 1 resolved 2 pcb 3 connected 4 sent 5 data 6 closed
static volatile int g_dg_err = 0;
static volatile int g_dg_got = 0;

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
    g_dg_err = err;
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
        g_dg_stage = 5;
        g_dg_got = (int)x->response.size();
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
    if (err != ERR_OK) { g_dg_err = err; x->failed = true; x->closed = true; return ERR_OK; }
    g_dg_stage = 3;
    x->connected = true;
    http_send_more(x);
    if (x->sent == x->request.size()) g_dg_stage = 4;
    return ERR_OK;
}

// url: http://host[:port]/path - returns the body, or an empty string
// on any failure. A body turns it into a POST.
static std::string http_request(const std::string& method, const std::string& url,
                                const std::string& body, const std::string& ctype,
                                int timeout_ms) {
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

    g_dg_stage = 0; g_dg_err = 0; g_dg_got = 0;
    ip_addr_t addr;
    if (!resolve(host.c_str(), &addr, 8000)) { g_dg_stage = -1; return ""; }
    g_dg_stage = 1;

    HttpXfer x;
    // HTTP/1.0 keeps the reply un-chunked, so the body needs no
    // transfer decoding.
    x.request = method + " " + path + " HTTP/1.0\r\nHost: " + host +
                "\r\nConnection: close\r\nUser-Agent: jdBasic-pico\r\n";
    if (!body.empty()) {
        x.request += "Content-Type: " + ctype + "\r\n";
        x.request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    x.request += "\r\n" + body;

    cyw43_arch_lwip_begin();
    x.pcb = tcp_new();
    if (x.pcb) {
        g_dg_stage = 2;
        tcp_arg(x.pcb, &x);
        tcp_recv(x.pcb, http_recv_cb);
        tcp_sent(x.pcb, http_sent_cb);
        tcp_err(x.pcb, http_err_cb);
        err_t crc = tcp_connect(x.pcb, &addr, port, http_connected_cb);
        if (crc != ERR_OK) {
            g_dg_err = crc;
            tcp_abort(x.pcb);
            x.pcb = nullptr;
        }
    }
    cyw43_arch_lwip_end();
    if (!x.pcb) return "";

    // Keep serving while waiting: the board has one thread, so a fetch
    // that just slept would freeze its own web server.
    extern void pico_httpd_pump();
    for (int t = 0; t < timeout_ms && !x.closed; t += 10) {
        pico_httpd_pump();
        sleep_ms(10);
    }

    cyw43_arch_lwip_begin();
    if (!x.closed) { x.failed = true; http_finish(&x); }
    cyw43_arch_lwip_end();
    if (x.closed && !x.failed) g_dg_stage = 6;

    if (x.failed && x.response.empty()) return "";
    size_t head = x.response.find("\r\n\r\n");
    return head == std::string::npos ? x.response : x.response.substr(head + 4);
}

// ── The clock ────────────────────────────────────────────────────────
//
// A board with no battery wakes up in 1970. One SNTP exchange fixes
// that: a 48-byte datagram to port 123, and the answer carries seconds
// since 1900 in bytes 40 to 43.

#define NTP_EPOCH_DELTA 2208988800u   // 1900 to 1970, in seconds

static volatile uint32_t g_ntp_secs = 0;
static volatile bool g_ntp_done = false;

static void ntp_recv_cb(void*, struct udp_pcb*, struct pbuf* p,
                        const ip_addr_t*, u16_t) {
    if (p && p->tot_len >= 48) {
        uint8_t stamp[4];
        if (pbuf_copy_partial(p, stamp, 4, 40) == 4) {
            uint32_t secs1900 = ((uint32_t)stamp[0] << 24) | ((uint32_t)stamp[1] << 16) |
                                ((uint32_t)stamp[2] << 8) | stamp[3];
            if (secs1900 > NTP_EPOCH_DELTA) {
                g_ntp_secs = secs1900 - NTP_EPOCH_DELTA;
                g_ntp_done = true;
            }
        }
    }
    if (p) pbuf_free(p);
}

// Returns the epoch second, or 0 when the server stayed silent.
static uint32_t ntp_query(const char* host, int timeout_ms) {
    ip_addr_t addr;
    if (!resolve(host, &addr, 8000)) return 0;

    g_ntp_done = false;
    g_ntp_secs = 0;

    cyw43_arch_lwip_begin();
    struct udp_pcb* u = udp_new();
    if (!u) { cyw43_arch_lwip_end(); return 0; }
    udp_recv(u, ntp_recv_cb, nullptr);
    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, 48, PBUF_RAM);
    if (p) {
        memset(p->payload, 0, 48);
        ((uint8_t*)p->payload)[0] = 0x1B;   // no leap warning, version 3, client
        udp_sendto(u, p, &addr, 123);
        pbuf_free(p);
    }
    cyw43_arch_lwip_end();

    for (int t = 0; t < timeout_ms && !g_ntp_done; t += 10) sleep_ms(10);

    cyw43_arch_lwip_begin();
    udp_remove(u);
    cyw43_arch_lwip_end();

    return g_ntp_done ? g_ntp_secs : 0;
}

// ── Registration ─────────────────────────────────────────────────────

void register_pico_wifi(VM& vm) {
    vm.register_native("WIFI.CONNECT", 2, 3, [](const std::vector<Value>& args) -> Value {
        wifi_sta_up();
        int timeout = args.size() >= 3 ? (int)args[2].to_double() : 30000;
        int rc = cyw43_arch_wifi_connect_timeout_ms(
            args[0].to_string().c_str(), args[1].to_string().c_str(),
            CYW43_AUTH_WPA2_MIXED_PSK, timeout);
        // Default power save naps through unicast packets: DHCP gets
        // through on broadcast, then ARP replies, SYN-ACKs and DNS
        // answers vanish. Performance mode keeps the receiver awake.
        if (rc == 0) {
            cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
            // A public resolver as backup: lwIP fails over to it inside
            // a single query when the advertised server never answers.
            // Index 0 belongs to DHCP.
            ip_addr_t fb;
            IP4_ADDR(&fb, 8, 8, 8, 8);
            cyw43_arch_lwip_begin();
            dns_setserver(1, &fb);
            cyw43_arch_lwip_end();
        }
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
    vm.register_native("WIFI.DNS$", 0, 0, [](const std::vector<Value>&) -> Value {
        const ip_addr_t* s = dns_getserver(0);
        return Value::make_string(ipaddr_ntoa(s));
    });
    vm.register_native("WIFI.DNS", 1, 1, [](const std::vector<Value>& args) -> Value {
        ip_addr_t a;
        if (!ipaddr_aton(args[0].to_string().c_str(), &a)) return Value::make_i64(-1);
        cyw43_arch_lwip_begin();
        dns_setserver(0, &a);
        cyw43_arch_lwip_end();
        return Value::make_i64(0);
    });
    vm.register_native("WIFI.DIAG$", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[64];
        snprintf(buf, sizeof buf, "stage=%d err=%d got=%d",
                 (int)g_dg_stage, (int)g_dg_err, (int)g_dg_got);
        return Value::make_string(buf);
    });
    // NTP.SYNC([server$] [, offset_hours]) -> the epoch second it set,
    // or 0 when nothing answered. The offset goes into the clock, so
    // DATE$ and TIME$ read local time on a board with no timezone.
    vm.register_native("NTP.SYNC", 0, 2, [](const std::vector<Value>& args) -> Value {
        std::string host = args.size() >= 1 ? args[0].to_string() : "pool.ntp.org";
        double offset = args.size() >= 2 ? args[1].to_double() : 0.0;
        uint32_t secs = ntp_query(host.c_str(), 5000);
        if (!secs && host != "time.google.com")
            secs = ntp_query("time.google.com", 5000);
        if (!secs) return Value::make_i64(0);
        int64_t local = (int64_t)secs + (int64_t)(offset * 3600.0);
        struct timeval tv;
        tv.tv_sec = (time_t)local;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        return Value::make_i64(local);
    });
    vm.register_native("HTTP.GET$", 1, 2, [](const std::vector<Value>& args) -> Value {
        int timeout = args.size() >= 2 ? (int)args[1].to_double() : 10000;
        return Value::make_string(http_request("GET", args[0].to_string(), "", "", timeout));
    });
    // HTTP.POST$(url$, body$ [, content_type$ [, timeout_ms]])
    vm.register_native("HTTP.POST$", 2, 4, [](const std::vector<Value>& args) -> Value {
        std::string ctype = args.size() >= 3 ? args[2].to_string() : "application/json";
        int timeout = args.size() >= 4 ? (int)args[3].to_double() : 10000;
        return Value::make_string(http_request("POST", args[0].to_string(),
                                               args[1].to_string(), ctype, timeout));
    });
}
