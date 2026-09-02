// WiFi on the Fruit Jam. The radio is an ESP32-C6 carrying Adafruit's
// nina firmware, which runs its own TCP/IP stack, so a socket here is a
// number the chip hands out rather than anything lwIP knows about.
//
// The verbs are the ones the W boards and the ESP32 port already use, so
// a jdBasic program that fetches a page does not care which radio is
// underneath it.

#include "../../src/vm.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>
#include <string>
#include <stdexcept>
#include <sys/time.h>

extern "C" {
int  fruitjam_esp_call(int cmd, const uint8_t** par, const uint16_t* len, int n,
                       uint16_t* off, uint16_t* rlen, int n_max, int* n_got,
                       int len16_in, int len16_out);
int  fruitjam_esp_call_u8(int cmd, const uint8_t** par, const uint16_t* len, int n);
const uint8_t* fruitjam_esp_data(void);
int  fruitjam_esp_fw(char* out, int cap);
int  fruitjam_esp_error(void);
void fruitjam_esp_reset(void);
}

#define C_SET_NET          0x10
#define C_SET_PASSPHRASE   0x11
#define C_GET_CONN_STATUS  0x20
#define C_GET_IPADDR       0x21
#define C_GET_MACADDR      0x22
#define C_GET_CURR_SSID    0x23
#define C_GET_CURR_RSSI    0x25
#define C_SCAN_NETWORKS    0x27
#define C_DATA_SENT_TCP    0x2A
#define C_AVAIL_DATA_TCP   0x2B
#define C_START_CLIENT_TCP 0x2D
#define C_STOP_CLIENT_TCP  0x2E
#define C_GET_CLIENT_STATE 0x2F
#define C_DISCONNECT       0x30
#define C_GET_IDX_RSSI     0x32
#define C_GET_IDX_ENCT     0x33
#define C_REQ_HOST_BY_NAME 0x34
#define C_GET_HOST_BY_NAME 0x35
#define C_START_SCAN       0x36
#define C_GET_TIME         0x3B
#define C_GET_IDX_CHAN     0x3D
#define C_GET_SOCKET       0x3F
#define C_SEND_DATA_TCP    0x44
#define C_GET_DATABUF_TCP  0x45

#define WL_CONNECTED       3
#define SOCKET_ESTABLISHED 4
#define TCP_MODE 0
#define TLS_MODE 2

static int g_http_status = 0;

static bool esp_ok(int cmd, const uint8_t** par, const uint16_t* len, int n,
                   uint16_t* off, uint16_t* rlen, int n_max, int* got,
                   int in16 = 0, int out16 = 0) {
    return fruitjam_esp_call(cmd, par, len, n, off, rlen, n_max, got,
                             in16, out16) == 0;
}

// ── station ──────────────────────────────────────────────────────────

static int wifi_status() {
    return fruitjam_esp_call_u8(C_GET_CONN_STATUS, nullptr, nullptr, 0);
}

// Returns 0 once the chip reports a connection, the way the W boards do.
static int wifi_join(const char* ssid, const char* pass, int timeout_ms) {
    const uint8_t* par[2];
    uint16_t len[2];
    par[0] = (const uint8_t*)ssid;
    len[0] = (uint16_t)strlen(ssid);
    par[1] = (const uint8_t*)pass;
    len[1] = (uint16_t)strlen(pass);

    int rc = len[1] ? fruitjam_esp_call_u8(C_SET_PASSPHRASE, par, len, 2)
                    : fruitjam_esp_call_u8(C_SET_NET, par, len, 1);
    if (rc != 1) return -1;

    absolute_time_t end = make_timeout_time_ms(timeout_ms);
    while (!time_reached(end)) {
        if (wifi_status() == WL_CONNECTED) return 0;
        sleep_ms(200);
    }
    return -2;
}

static std::string dotted(const uint8_t* p) {
    char b[20];
    snprintf(b, sizeof b, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    return b;
}

// ip, netmask and gateway come back together; index picks one.
static std::string wifi_addr(int which) {
    const uint8_t ff = 0xFF;
    const uint8_t* par[1] = { &ff };
    uint16_t len[1] = { 1 };
    uint16_t off[3], rlen[3];
    int got = 0;
    if (!esp_ok(C_GET_IPADDR, par, len, 1, off, rlen, 3, &got) || got <= which)
        return "";
    if (rlen[which] < 4) return "";
    return dotted(fruitjam_esp_data() + off[which]);
}

static uint32_t wifi_resolve(const char* host) {
    const uint8_t* par[1] = { (const uint8_t*)host };
    uint16_t len[1] = { (uint16_t)strlen(host) };
    if (fruitjam_esp_call_u8(C_REQ_HOST_BY_NAME, par, len, 1) != 1) return 0;

    uint16_t off[1], rlen[1];
    int got = 0;
    if (!esp_ok(C_GET_HOST_BY_NAME, nullptr, nullptr, 0, off, rlen, 1, &got))
        return 0;
    if (got < 1 || rlen[0] < 4) return 0;
    const uint8_t* d = fruitjam_esp_data() + off[0];
    return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
           ((uint32_t)d[2] << 8) | d[3];
}

// ── sockets ──────────────────────────────────────────────────────────

static int sock_open(const char* host, uint32_t ip, int port, int mode) {
    int sock = fruitjam_esp_call_u8(C_GET_SOCKET, nullptr, nullptr, 0);
    if (sock < 0 || sock == 255) return -1;

    uint8_t ip4[4] = { (uint8_t)(ip >> 24), (uint8_t)(ip >> 16),
                       (uint8_t)(ip >> 8), (uint8_t)ip };
    uint8_t pb[2] = { (uint8_t)(port >> 8), (uint8_t)port };
    uint8_t sb = (uint8_t)sock;
    uint8_t mb = (uint8_t)mode;
    const uint8_t zero[4] = { 0, 0, 0, 0 };

    const uint8_t* par[5];
    uint16_t len[5];
    int n;
    if (mode == TLS_MODE) {
        // Encrypted connections are opened by name: the certificate is
        // checked against it.
        par[0] = (const uint8_t*)host; len[0] = (uint16_t)strlen(host);
        par[1] = zero; len[1] = 4;
        par[2] = pb;   len[2] = 2;
        par[3] = &sb;  len[3] = 1;
        par[4] = &mb;  len[4] = 1;
        n = 5;
    } else {
        par[0] = ip4; len[0] = 4;
        par[1] = pb;  len[1] = 2;
        par[2] = &sb; len[2] = 1;
        par[3] = &mb; len[3] = 1;
        n = 4;
    }
    if (fruitjam_esp_call_u8(C_START_CLIENT_TCP, par, len, n) != 1) return -1;
    return sock;
}

static int sock_state(int sock) {
    uint8_t sb = (uint8_t)sock;
    const uint8_t* par[1] = { &sb };
    uint16_t len[1] = { 1 };
    return fruitjam_esp_call_u8(C_GET_CLIENT_STATE, par, len, 1);
}

static void sock_close(int sock) {
    uint8_t sb = (uint8_t)sock;
    const uint8_t* par[1] = { &sb };
    uint16_t len[1] = { 1 };
    fruitjam_esp_call_u8(C_STOP_CLIENT_TCP, par, len, 1);
}

// The firmware takes writes in chunks of sixty four bytes.
static bool sock_write(int sock, const char* data, int n) {
    uint8_t sb = (uint8_t)sock;
    int sent = 0;
    while (sent < n) {
        int chunk = n - sent;
        if (chunk > 64) chunk = 64;
        const uint8_t* par[2] = { &sb, (const uint8_t*)(data + sent) };
        uint16_t len[2] = { 1, (uint16_t)chunk };
        uint16_t off[1], rlen[1];
        int got = 0;
        if (!esp_ok(C_SEND_DATA_TCP, par, len, 2, off, rlen, 1, &got, 1, 0))
            return false;
        if (got < 1 || rlen[0] < 1) return false;
        int wrote = fruitjam_esp_data()[off[0]];
        if (rlen[0] >= 2) wrote |= fruitjam_esp_data()[off[0] + 1] << 8;
        if (wrote <= 0) return false;
        sent += wrote;
    }
    const uint8_t* par[1] = { &sb };
    uint16_t len[1] = { 1 };
    return fruitjam_esp_call_u8(C_DATA_SENT_TCP, par, len, 1) == 1;
}

static int sock_available(int sock) {
    uint8_t sb = (uint8_t)sock;
    const uint8_t* par[1] = { &sb };
    uint16_t len[1] = { 1 };
    uint16_t off[1], rlen[1];
    int got = 0;
    if (!esp_ok(C_AVAIL_DATA_TCP, par, len, 1, off, rlen, 1, &got)) return -1;
    if (got < 1 || rlen[0] < 2) return 0;
    const uint8_t* d = fruitjam_esp_data() + off[0];
    return d[0] | (d[1] << 8);
}

static int sock_read(int sock, std::string& into, int want) {
    if (want > 512) want = 512;
    uint8_t sb = (uint8_t)sock;
    uint8_t wb[2] = { (uint8_t)(want & 0xFF), (uint8_t)(want >> 8) };
    const uint8_t* par[2] = { &sb, wb };
    uint16_t len[2] = { 1, 2 };
    uint16_t off[1], rlen[1];
    int got = 0;
    if (!esp_ok(C_GET_DATABUF_TCP, par, len, 2, off, rlen, 1, &got, 1, 1))
        return -1;
    if (got < 1 || rlen[0] == 0) return 0;
    into.append((const char*)fruitjam_esp_data() + off[0], rlen[0]);
    return rlen[0];
}

// ── HTTP ─────────────────────────────────────────────────────────────

struct Url {
    std::string host, path;
    int port = 80;
    bool tls = false;
};

static bool parse_url(const std::string& url, Url& u) {
    std::string rest = url;
    if (rest.compare(0, 8, "https://") == 0) {
        u.tls = true;
        u.port = 443;
        rest = rest.substr(8);
    } else if (rest.compare(0, 7, "http://") == 0) {
        rest = rest.substr(7);
    }
    size_t slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        u.port = atoi(hostport.c_str() + colon + 1);
        u.host = hostport.substr(0, colon);
    } else {
        u.host = hostport;
    }
    return !u.host.empty();
}

static std::string http_request(const char* verb, const std::string& url,
                                const std::string& body, const std::string& ctype,
                                int timeout_ms) {
    g_http_status = 0;
    Url u;
    if (!parse_url(url, u)) return "";

    uint32_t ip = 0;
    if (!u.tls) {
        ip = wifi_resolve(u.host.c_str());
        if (!ip) return "";
    }
    int sock = sock_open(u.host.c_str(), ip, u.port, u.tls ? TLS_MODE : TCP_MODE);
    if (sock < 0) return "";

    absolute_time_t end = make_timeout_time_ms(timeout_ms);
    while (sock_state(sock) != SOCKET_ESTABLISHED) {
        if (time_reached(end)) { sock_close(sock); return ""; }
        sleep_ms(20);
    }

    std::string req = std::string(verb) + " " + u.path + " HTTP/1.1\r\nHost: " +
                      u.host + "\r\nConnection: close\r\n" +
                      "User-Agent: jdBasic\r\n";
    if (!body.empty()) {
        char n[32];
        snprintf(n, sizeof n, "%u", (unsigned)body.size());
        req += "Content-Type: " + (ctype.empty() ? std::string("application/json") : ctype) +
               "\r\nContent-Length: " + n + "\r\n";
    }
    req += "\r\n" + body;

    if (!sock_write(sock, req.data(), (int)req.size())) {
        sock_close(sock);
        return "";
    }

    std::string resp;
    while (!time_reached(end)) {
        int avail = sock_available(sock);
        if (avail > 0) {
            if (sock_read(sock, resp, avail) <= 0) break;
            continue;
        }
        if (sock_state(sock) != SOCKET_ESTABLISHED) {
            // Whatever is still buffered belongs to us.
            if (sock_available(sock) <= 0) break;
            continue;
        }
        sleep_ms(10);
    }
    sock_close(sock);

    if (resp.compare(0, 5, "HTTP/") == 0) {
        size_t sp = resp.find(' ');
        if (sp != std::string::npos) g_http_status = atoi(resp.c_str() + sp + 1);
    }
    size_t split = resp.find("\r\n\r\n");
    return split == std::string::npos ? resp : resp.substr(split + 4);
}

// ── verbs ────────────────────────────────────────────────────────────

void register_fruitjam_wifi(VM& vm) {
    vm.register_native("WIFI.CONNECT", 2, 3, [](const std::vector<Value>& args) -> Value {
        int timeout = args.size() >= 3 ? (int)args[2].to_double() : 20000;
        return Value::make_i64(wifi_join(args[0].to_string().c_str(),
                                         args[1].to_string().c_str(), timeout));
    });
    // 3 is a live connection here as it is on the W boards, so the same
    // test works on both.
    vm.register_native("WIFI.STATUS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(wifi_status());
    });
    // The same two lines every networked program would otherwise carry:
    // ssid and password from /wifi.txt.
    vm.register_native("WIFI.AUTO", 0, 0, [](const std::vector<Value>&) -> Value {
        FILE* f = fopen("/wifi.txt", "r");
        if (!f) return Value::make_i64(-2);
        char ssid[64] = {0}, pass[80] = {0};
        bool ok = fgets(ssid, sizeof ssid, f) && fgets(pass, sizeof pass, f);
        fclose(f);
        if (!ok) return Value::make_i64(-2);
        for (char* p : { ssid, pass })
            for (int i = (int)strlen(p) - 1;
                 i >= 0 && (p[i] == 10 || p[i] == 13 || p[i] == 32); i--)
                p[i] = 0;
        if (!ssid[0]) return Value::make_i64(-2);
        return Value::make_i64(wifi_join(ssid, pass, 30000));
    });
    vm.register_native("WIFI.IP$", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_string(wifi_addr(0));
    });
    vm.register_native("WIFI.OFF", 0, 0, [](const std::vector<Value>&) -> Value {
        fruitjam_esp_call_u8(C_DISCONNECT, nullptr, nullptr, 0);
        return Value();
    });
    vm.register_native("WIFI.MAC$", 0, 0, [](const std::vector<Value>&) -> Value {
        const uint8_t ff = 0xFF;
        const uint8_t* par[1] = { &ff };
        uint16_t len[1] = { 1 };
        uint16_t off[1], rlen[1];
        int got = 0;
        if (!esp_ok(C_GET_MACADDR, par, len, 1, off, rlen, 1, &got) ||
            got < 1 || rlen[0] < 6)
            return Value::make_string("");
        // The chip hands its address back last byte first.
        const uint8_t* d = fruitjam_esp_data() + off[0];
        char b[20];
        snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X",
                 d[5], d[4], d[3], d[2], d[1], d[0]);
        return Value::make_string(b);
    });
    // One row per network: name, signal, channel, and whether it is open.
    vm.register_native("WIFI.SCAN", 0, 1, [](const std::vector<Value>& args) -> Value {
        Value out = Value::make_array();
        if (fruitjam_esp_call_u8(C_START_SCAN, nullptr, nullptr, 0) != 1) return out;

        int waits = args.size() >= 1 ? (int)args[0].to_double() : 6;
        uint16_t off[32], rlen[32];
        int got = 0;
        for (int attempt = 0; attempt < waits; attempt++) {
            sleep_ms(500);
            if (esp_ok(C_SCAN_NETWORKS, nullptr, nullptr, 0, off, rlen, 32, &got) &&
                got > 0)
                break;
            got = 0;
        }
        if (got <= 0) return out;

        // The names arrive in one answer; everything else is asked for
        // per index afterwards, so the names are copied out first.
        std::vector<std::string> names;
        const uint8_t* data = fruitjam_esp_data();
        for (int i = 0; i < got; i++)
            names.push_back(std::string((const char*)data + off[i], rlen[i]));

        for (int i = 0; i < got; i++) {
            uint8_t ib = (uint8_t)i;
            const uint8_t* par[1] = { &ib };
            uint16_t len[1] = { 1 };
            int rssi = 0, chan = 0, enc = 0;
            uint16_t o[1], l[1];
            int n = 0;
            if (esp_ok(C_GET_IDX_RSSI, par, len, 1, o, l, 1, &n) && n && l[0] >= 1)
                rssi = (int8_t)fruitjam_esp_data()[o[0]];
            if (esp_ok(C_GET_IDX_CHAN, par, len, 1, o, l, 1, &n) && n && l[0] >= 1)
                chan = fruitjam_esp_data()[o[0]];
            if (esp_ok(C_GET_IDX_ENCT, par, len, 1, o, l, 1, &n) && n && l[0] >= 1)
                enc = fruitjam_esp_data()[o[0]];

            Value row = Value::make_array();
            auto& c = row.as_array()->elements;
            c.push_back(Value::make_string(names[i]));
            c.push_back(Value::make_i64(rssi));
            c.push_back(Value::make_i64(chan));
            c.push_back(Value::make_bool(enc == 7));
            out.as_array()->elements.push_back(std::move(row));
        }
        return out;
    });
    vm.register_native("WIFI.DIAG$", 0, 0, [](const std::vector<Value>&) -> Value {
        char fw[32];
        fruitjam_esp_fw(fw, sizeof fw);
        char b[128];
        snprintf(b, sizeof b, "nina %s status %d ip %s err %d",
                 fw[0] ? fw : "?", wifi_status(), wifi_addr(0).c_str(),
                 fruitjam_esp_error());
        return Value::make_string(b);
    });
    // The radio keeps its own clock once it is online.
    vm.register_native("NTP.SYNC", 0, 2, [](const std::vector<Value>& args) -> Value {
        double offset = args.size() >= 2 ? args[1].to_double()
                      : (args.size() >= 1 ? args[0].to_double() : 0.0);
        uint16_t off[1], rlen[1];
        int got = 0;
        for (int i = 0; i < 20; i++) {
            if (esp_ok(C_GET_TIME, nullptr, nullptr, 0, off, rlen, 1, &got) &&
                got >= 1 && rlen[0] >= 4) {
                const uint8_t* d = fruitjam_esp_data() + off[0];
                uint32_t secs = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                                ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
                if (secs > 1600000000u) {
                    int64_t local = (int64_t)secs + (int64_t)(offset * 3600.0);
                    struct timeval tv;
                    tv.tv_sec = (time_t)local;
                    tv.tv_usec = 0;
                    settimeofday(&tv, nullptr);
                    return Value::make_i64(local);
                }
            }
            sleep_ms(500);
        }
        return Value::make_i64(0);
    });
    vm.register_native("HTTP.GET$", 1, 2, [](const std::vector<Value>& args) -> Value {
        int timeout = args.size() >= 2 ? (int)args[1].to_double() : 10000;
        return Value::make_string(http_request("GET", args[0].to_string(), "", "", timeout));
    });
    vm.register_native("HTTP.POST$", 2, 4, [](const std::vector<Value>& args) -> Value {
        std::string ctype = args.size() >= 3 ? args[2].to_string() : "";
        int timeout = args.size() >= 4 ? (int)args[3].to_double() : 10000;
        return Value::make_string(http_request("POST", args[0].to_string(),
                                               args[1].to_string(), ctype, timeout));
    });
    vm.register_native("HTTP.STATUS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(g_http_status);
    });
}
