// node.cpp - the Node Bus (doc/design/node_bus_plan.md): jdBasic programs on
// one machine find each other by name, send values into each other's inbox,
// call each other and publish topics that others subscribe to. Whatever
// arrives is an ordinary channel (src/channels.h); values travel as JDV1
// (src/jdv.h) in the frames of plan section 5.2.
//
// Every node owns a listening socket (AF_UNIX, loopback TCP where that is
// missing) and a file in the node directory that names its socket and the
// topics it publishes. One I/O thread per process serves every socket; the
// VM threads queue frames under g_mu and wake it.
//
// A node in the "lan" scope also listens on TCP and sends a beacon once a
// second (multicast, broadcast and the seeds in JDB_NODE_SEEDS). Nodes with
// the same key find each other through the beacons, prove the key with
// HMAC-SHA256 over both nonces before any other frame, and keep measuring
// each other's clock offset with PING/PONG.

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#endif

#include "node.h"
#include "channels.h"
#include "jdb_crypto.h"
#include "jdv.h"
#include "vm.h"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── sockets ─────────────────────────────────────────────────────────

#if defined(_WIN32)
using sock_t = SOCKET;
const sock_t BAD_SOCK = INVALID_SOCKET;
using pollfd_t = WSAPOLLFD;
int poll_fds(pollfd_t* fds, size_t n, int timeout) { return WSAPoll(fds, (ULONG)n, timeout); }
void close_sock(sock_t s) { if (s != BAD_SOCK) closesocket(s); }
bool would_block() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
using sock_t = int;
const sock_t BAD_SOCK = -1;
using pollfd_t = struct pollfd;
int poll_fds(pollfd_t* fds, size_t n, int timeout) { return ::poll(fds, (nfds_t)n, timeout); }
void close_sock(sock_t s) { if (s != BAD_SOCK) ::close(s); }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
#endif

#if defined(MSG_NOSIGNAL)
const int SEND_FLAGS = MSG_NOSIGNAL;
#else
const int SEND_FLAGS = 0;
#endif

bool sockets_ready() {
#if defined(_WIN32)
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ok;
#else
    static std::once_flag once;
    std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });
    return true;
#endif
}

void set_nonblocking(sock_t s) {
#if defined(_WIN32)
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    fcntl(s, F_SETFD, FD_CLOEXEC);
#endif
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

void tune_stream(sock_t s, bool tcp) {
    int buf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));
    if (tcp) {
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
}

int64_t now_ns() {
    return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t self_pid() {
#if defined(_WIN32)
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

bool pid_alive(int64_t pid) {
    if (pid <= 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The machine's name; JDB_NODE_HOST stands in for it, so a test can play
// a second machine on this one.
std::string host_name() {
    static std::string h = [] {
        const char* env = std::getenv("JDB_NODE_HOST");
        if (env && *env) return lower(env);
        sockets_ready();
        char buf[256] = {};
        gethostname(buf, sizeof(buf) - 1);
        return lower(buf);
    }();
    return h;
}

std::string hex_of(const uint8_t* p, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s.push_back(digits[p[i] >> 4]);
        s.push_back(digits[p[i] & 15]);
    }
    return s;
}

std::string random_hex(size_t n) {
    std::vector<uint8_t> b(n);
    if (!jdb_crypto::random_bytes(b.data(), n)) {
        std::mt19937_64 gen((uint64_t)now_ns() ^ ((uint64_t)self_pid() << 20));
        for (auto& x : b) x = (uint8_t)gen();
    }
    return hex_of(b.data(), n);
}

std::string hmac_hex(const std::string& key, const std::string& msg) {
    uint8_t out[32];
    jdb_crypto::hmac_sha256((const uint8_t*)key.data(), key.size(), (const uint8_t*)msg.data(), msg.size(), out);
    return hex_of(out, 32);
}

// The first 8 bytes of SHA-256 of the key: nodes with different keys tell
// from the beacon alone that they do not belong together.
std::string key_fingerprint(const std::string& key) {
    jdb_crypto::Sha256 s;
    jdb_crypto::sha256_init(s);
    jdb_crypto::sha256_update(s, (const uint8_t*)key.data(), key.size());
    uint8_t out[32];
    jdb_crypto::sha256_final(s, out);
    return hex_of(out, 8);
}

// Comparison whose time does not depend on where the strings differ.
bool same_secret(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

// ── frames (plan 5.2) ───────────────────────────────────────────────

enum Kind : uint8_t {
    K_HELLO = 1, K_WELCOME = 2, K_BYE = 3, K_AUTH = 4,
    K_MSG = 10, K_CALL = 11, K_REPLY = 12, K_FAIL = 13,
    K_SUB = 20, K_UNSUB = 22, K_PUB = 23, K_CREDIT = 24,
    K_PING = 30, K_PONG = 31,
};

const size_t HDR = 24;
const uint16_t F_TIME = 1;
const uint32_t MAX_FRAME = 64u * 1024 * 1024;

const char* kind_name(uint8_t k) {
    switch (k) {
        case K_HELLO: return "HELLO"; case K_WELCOME: return "WELCOME"; case K_BYE: return "BYE";
        case K_MSG: return "MSG"; case K_CALL: return "CALL"; case K_REPLY: return "REPLY";
        case K_FAIL: return "FAIL"; case K_SUB: return "SUB"; case K_UNSUB: return "UNSUB";
        case K_PUB: return "PUB"; case K_CREDIT: return "CREDIT"; case K_AUTH: return "AUTH";
        case K_PING: return "PING"; case K_PONG: return "PONG";
    }
    return "?";
}

template <typename T>
void put_le(std::string& out, T v) {
    char b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    out.append(b, sizeof(T));
}

template <typename T>
T get_le(const char* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

// A frame around an encoded JDV value (no magic: the frame is the envelope).
std::string make_frame(uint8_t kind, uint32_t stream, uint64_t seq, const std::string& body, bool with_time = false) {
    std::string f;
    f.reserve(HDR + (with_time ? 8 : 0) + body.size());
    f.push_back('J');
    f.push_back('B');
    f.push_back((char)1);
    f.push_back((char)kind);
    put_le<uint16_t>(f, with_time ? F_TIME : 0);
    put_le<uint16_t>(f, 0);
    put_le<uint32_t>(f, stream);
    put_le<uint32_t>(f, (uint32_t)body.size());
    put_le<uint64_t>(f, seq);
    if (with_time) put_le<int64_t>(f, now_ns());
    f.append(body);
    return f;
}

std::string body_of(const Value& v) {
    std::string out;
    jdv::encode(out, v, 0);
    return out;
}

Value value_of(const std::string& body) {
    jdv::Reader r{(const uint8_t*)body.data(), body.size()};
    Value v = jdv::decode(r, 0);
    if (r.at != r.n) r.fail("trailing bytes after the value");
    return v;
}

Value map_of(std::initializer_list<std::pair<const char*, Value>> kv) {
    Value m = Value::make_object();
    for (auto& p : kv) m.as_object()->set(p.first, p.second);
    return m;
}

std::string field_str(const Value& m, const char* key) {
    if (m.type != ValueType::OBJECT) return "";
    Value* v = m.as_object()->get(key);
    return v ? v->to_string() : "";
}

int64_t field_int(const Value& m, const char* key, int64_t dflt = 0) {
    if (m.type != ValueType::OBJECT) return dflt;
    Value* v = m.as_object()->get(key);
    return v ? v->to_int() : dflt;
}

// ── state ───────────────────────────────────────────────────────────

struct Link {
    int64_t id = 0;
    sock_t fd = BAD_SOCK;
    int64_t node = 0;          // the local node this link belongs to
    std::string peer_id;       // the remote node's id
    std::string peer_name;     // the remote node's name
    std::string peer_host;
    bool outbound = false;
    bool welcomed = false;
    bool dead = false;
    std::string rx;
    std::deque<std::string> tx;
    size_t tx_off = 0;
    size_t tx_bytes = 0;
    // lan links: nothing but the handshake passes until the key is proven;
    // frames sent before that wait in held
    bool lan = false;
    bool authed = false;
    std::string nonce_mine, nonce_peer;
    std::deque<std::string> held;
    int64_t next_ping = 0;
    int pings = 0;
    std::deque<std::pair<int64_t, int64_t>> samples;   // round trip, offset
};

// What PING/PONG measured about a peer's clock: its time minus ours, and
// half the shortest round trip as the error bound.
struct ClockOffset {
    int64_t offset = 0;
    int64_t error = 0;
};

// One topic of one publisher that a subscription is attached to.
struct Attachment {
    uint32_t id = 0;           // the stream id of its PUB frames
    int64_t sub = 0;
    int64_t link = 0;
    std::string peer_id;
    std::string peer_name;
    std::string topic;
    int64_t to_return = 0;     // consumed values not yet credited back
};

struct Subscription {
    int64_t id = 0;
    int64_t node = 0;
    std::string pattern;
    std::string policy;        // queue, latest, ring, share
    int64_t cap = 256;
    bool envelope = false;
    std::shared_ptr<Channel> ch;
    int64_t handle = 0;
    std::shared_ptr<std::atomic<bool>> closed;
    std::deque<uint32_t> pushed;   // attachment of every queued value, oldest first
    uint64_t pushed_total = 0;
    uint64_t accounted = 0;
    uint64_t received = 0;
    uint64_t bytes = 0;
};

struct SubRef {
    int64_t link = 0;
    uint32_t att = 0;
    std::string policy;
    int64_t credits = 0;
    uint64_t dropped = 0;
};

struct Publisher {
    int64_t node = 0;
    std::string topic;
    bool blocking = false;
    bool retain = false;
    bool has_retained = false;
    std::string retained;
    uint64_t seq = 0;
    uint64_t sent = 0;
    uint64_t dropped = 0;
    uint64_t bytes = 0;
    std::vector<SubRef> subs;
    size_t rr = 0;
    int64_t handle = 0;
    std::shared_ptr<std::atomic<bool>> closed;
};

struct LocalNode {
    int64_t handle = 0;
    std::string id;
    std::string name;          // lower case, the key in the directory
    std::string display;       // as JOIN was given it
    sock_t lfd = BAD_SOCK;
    bool unix_sock = true;
    std::string sock_path;
    int tcp_port = 0;
    std::string dir_file;
    int64_t started = 0;
    std::shared_ptr<Channel> inbox;
    int64_t inbox_handle = 0;
    std::shared_ptr<Channel> served;
    std::map<std::string, std::string> serve;   // METHOD -> handler name
    std::shared_ptr<Channel> events;
    int64_t events_handle = 0;
    std::set<std::string> known;                // peer names EVENTS has seen
    bool lan = false;
    std::string key, fp;
    sock_t lan_lfd = BAD_SOCK;
    int lan_port = 0;
};

// A node this one can reach: from the directory, or for lan nodes from a
// beacon (then ip, fp and heard are set and tcp is its lan port).
struct DirEntry {
    std::string id, name, display, sock, host;
    int64_t pid = 0;
    int tcp = 0;
    int64_t started = 0;
    std::vector<std::string> topics;
    std::string file;
    bool lan = false;
    uint32_t ip = 0;           // network byte order
    std::string fp;
    int64_t heard = 0;
};

struct PendingCall {
    std::shared_ptr<Channel> ch;
    bool raw = false;          // CALL_ASYNC: the bare reply, or a failure map
};

struct CallToken {
    int64_t link = 0;
    uint32_t call_id = 0;
};

std::mutex g_mu;
std::condition_variable g_cv;
std::map<int64_t, LocalNode> g_nodes;
std::map<int64_t, Link> g_links;
std::map<int64_t, Subscription> g_subs;
std::map<uint32_t, Attachment> g_atts;
std::map<int64_t, Publisher> g_pubs;          // by publisher channel handle
std::map<uint32_t, PendingCall> g_calls;
std::map<int64_t, CallToken> g_tokens;
int64_t g_default = 0;
int64_t g_next_node = 1, g_next_link = 1, g_next_sub = 1, g_next_token = 1;
uint32_t g_next_att = 1, g_next_call = 1;
std::thread g_io;
bool g_io_running = false;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_trace{false};
sock_t g_wake = BAD_SOCK;
sockaddr_in g_wake_addr{};
std::string g_last_error;
std::map<std::string, DirEntry> g_lan_peers;    // by node id, from beacons
std::map<std::string, ClockOffset> g_offsets;   // by node id
sock_t g_beacon = BAD_SOCK;
int64_t g_next_beacon = 0;

const uint16_t BEACON_PORT = 47474;
const char* BEACON_GROUP = "239.255.74.66";
const int64_t BEACON_EVERY = 1000000000LL;
const int64_t PEER_EXPIRES = 3500000000LL;

const size_t LATEST_BACKLOG = 8u * 1024 * 1024;   // a latest/ring frame waits behind at most this much
const int64_t INBOX_CAP = 4096;

void set_error(const std::string& e) { g_last_error = e; }

void trace(const char* dir, const Link& l, uint8_t kind, uint32_t stream, size_t bytes) {
    if (!g_trace.load()) return;
    std::fprintf(stderr, "[node] %s %s %s stream=%u bytes=%zu\n", dir,
                 l.peer_name.empty() ? "?" : l.peer_name.c_str(), kind_name(kind), stream, bytes);
}

void wake_io() {
    if (g_wake == BAD_SOCK) return;
    char b = 1;
    sendto(g_wake, &b, 1, 0, (const sockaddr*)&g_wake_addr, sizeof(g_wake_addr));
}

void push_value(Channel& ch, Value v) {
    {
        std::lock_guard<std::mutex> lock(ch.mtx);
        if (ch.closed.load()) return;
        if (ch.policy != ChanPolicy::Block) {
            while (ch.buffer.size() >= (size_t)std::max<int64_t>(1, ch.capacity)) {
                ch.buffer.pop_front();
                ++ch.dropped;
            }
        }
        ch.buffer.push_back(std::move(v));
    }
    ch.cv_recv.notify_one();
}

std::shared_ptr<Channel> new_channel(int64_t cap, ChanPolicy policy, int64_t& handle) {
    auto ch = std::make_shared<Channel>();
    ch->capacity = cap;
    ch->policy = policy;
    handle = chan_register(ch);
    return ch;
}

void send_frame(Link& l, std::string frame) {
    if (l.dead) return;
    uint8_t kind = (uint8_t)frame[3];
    if (l.lan && !l.authed && kind != K_HELLO && kind != K_WELCOME && kind != K_AUTH) {
        l.held.push_back(std::move(frame));
        return;
    }
    trace("->", l, kind, get_le<uint32_t>(frame.data() + 8), frame.size());
    size_t off = 0;
    if (l.tx.empty()) {
        // Nothing queued ahead of it: straight onto the socket, the I/O
        // thread only takes what does not fit.
        int k = send(l.fd, frame.data(), (int)std::min<size_t>(frame.size(), 1u << 30), SEND_FLAGS);
        if (k < 0) {
            if (!would_block()) {
                l.dead = true;
                return;
            }
            k = 0;
        }
        if ((size_t)k == frame.size()) return;
        off = (size_t)k;
    }
    l.tx_bytes += frame.size();
    if (l.tx.empty()) l.tx_off = off;
    l.tx.push_back(std::move(frame));
}

void release_held(Link& l) {
    l.authed = true;
    while (!l.held.empty()) {
        std::string f = std::move(l.held.front());
        l.held.pop_front();
        send_frame(l, std::move(f));
    }
}

// ── the node directory ──────────────────────────────────────────────

fs::path node_dir() {
    const char* env = std::getenv("JDB_NODE_DIR");
    fs::path dir;
    if (env && *env) {
        dir = fs::u8path(env);
    } else {
#if defined(_WIN32)
        const char* base = std::getenv("LOCALAPPDATA");
        dir = fs::u8path(base && *base ? base : ".") / "jdBasic" / "nodes";
#elif defined(__APPLE__)
        const char* base = std::getenv("TMPDIR");
        dir = fs::u8path(base && *base ? base : "/tmp") / "jdbasic" / "nodes";
#else
        const char* base = std::getenv("XDG_RUNTIME_DIR");
        if (base && *base) dir = fs::u8path(base) / "jdbasic" / "nodes";
        else dir = fs::u8path("/tmp") / ("jdbasic-" + std::to_string((long)getuid())) / "nodes";
#endif
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
        else out.push_back(c);
    }
    return out;
}

std::string node_json(const LocalNode& n) {
    std::ostringstream o;
    o << "{\"version\": 1, \"id\": \"" << n.id << "\", \"name\": \"" << json_escape(n.name)
      << "\", \"display\": \"" << json_escape(n.display) << "\", \"pid\": " << self_pid()
      << ", \"host\": \"" << json_escape(host_name()) << "\", \"sock\": \""
      << json_escape(n.unix_sock ? n.sock_path : "") << "\", \"tcp\": " << n.tcp_port
      << ", \"started\": " << n.started << ", \"topics\": [";
    bool first = true;
    for (auto& kv : g_pubs) {
        if (kv.second.node != n.handle) continue;
        o << (first ? "" : ", ") << "\"" << json_escape(kv.second.topic) << "\"";
        first = false;
    }
    o << "]}\n";
    return o.str();
}

// The small, flat JSON node_json writes: strings, integers, one string array.
bool parse_entry(const std::string& text, DirEntry& e) {
    size_t i = 0;
    auto skip = [&] { while (i < text.size() && std::isspace((unsigned char)text[i])) ++i; };
    auto str = [&](std::string& out) -> bool {
        skip();
        if (i >= text.size() || text[i] != '"') return false;
        ++i;
        out.clear();
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                if (text[i] == 'u' && i + 4 < text.size()) {
                    out.push_back((char)std::strtol(text.substr(i + 1, 4).c_str(), nullptr, 16));
                    i += 4;
                } else {
                    out.push_back(text[i]);
                }
            } else {
                out.push_back(text[i]);
            }
            ++i;
        }
        if (i >= text.size()) return false;
        ++i;
        return true;
    };
    skip();
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    for (;;) {
        skip();
        if (i < text.size() && text[i] == '}') return !e.id.empty() && !e.name.empty();
        std::string key;
        if (!str(key)) return false;
        skip();
        if (i >= text.size() || text[i] != ':') return false;
        ++i;
        skip();
        if (i < text.size() && text[i] == '"') {
            std::string v;
            if (!str(v)) return false;
            if (key == "id") e.id = v;
            else if (key == "name") e.name = v;
            else if (key == "display") e.display = v;
            else if (key == "host") e.host = v;
            else if (key == "sock") e.sock = v;
        } else if (i < text.size() && text[i] == '[') {
            ++i;
            for (;;) {
                skip();
                if (i < text.size() && text[i] == ']') { ++i; break; }
                std::string v;
                if (!str(v)) return false;
                if (key == "topics") e.topics.push_back(v);
                skip();
                if (i < text.size() && text[i] == ',') ++i;
            }
        } else {
            size_t start = i;
            while (i < text.size() && (std::isdigit((unsigned char)text[i]) || text[i] == '-')) ++i;
            int64_t v = std::strtoll(text.substr(start, i - start).c_str(), nullptr, 10);
            if (key == "pid") e.pid = v;
            else if (key == "tcp") e.tcp = (int)v;
            else if (key == "started") e.started = v;
        }
        skip();
        if (i < text.size() && text[i] == ',') ++i;
    }
}

bool read_entry(const fs::path& file, DirEntry& e) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    e = DirEntry();
    e.file = file.u8string();
    return parse_entry(ss.str(), e);
}

// Every live node in the directory. Files of processes that are gone are removed.
std::vector<DirEntry> read_directory() {
    std::vector<DirEntry> out;
    std::error_code ec;
    fs::path dir = node_dir();
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() != ".json") continue;
        DirEntry e;
        if (!read_entry(it->path(), e)) continue;
        if (!pid_alive(e.pid)) {
            std::error_code rm;
            fs::remove(it->path(), rm);
            if (!e.sock.empty()) fs::remove(fs::u8path(e.sock), rm);
            continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

void write_dir_file(const LocalNode& n) {
    fs::path file = fs::u8path(n.dir_file);
    fs::path tmp = file;
    tmp += ".tmp" + std::to_string(self_pid());
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        o << node_json(n);
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) fs::remove(tmp, ec);
}

bool glob_match(const std::string& pat, const std::string& s) {
    size_t p = 0, i = 0, star = std::string::npos, mark = 0;
    while (i < s.size()) {
        if (p < pat.size() && (pat[p] == '?' || pat[p] == s[i])) { ++p; ++i; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; mark = i; }
        else if (star != std::string::npos) { p = star + 1; i = ++mark; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

// ── links ───────────────────────────────────────────────────────────

LocalNode* node_of(int64_t h) {
    auto it = g_nodes.find(h);
    return it == g_nodes.end() ? nullptr : &it->second;
}

Link* link_to(int64_t node, const std::string& peer_id) {
    for (auto& kv : g_links)
        if (kv.second.node == node && kv.second.peer_id == peer_id && !kv.second.dead) return &kv.second;
    return nullptr;
}

Value hello_map(const LocalNode& n) {
    return map_of({{"ID", Value::make_string(n.id)},
                   {"NAME", Value::make_string(n.display)},
                   {"PID", Value::make_i64(self_pid())},
                   {"HOST", Value::make_string(host_name())},
                   {"VERSION", Value::make_i64(1)}});
}

std::string hello_body(const LocalNode& n) { return body_of(hello_map(n)); }

// A TCP connection to ip:port within ms milliseconds, or BAD_SOCK.
sock_t tcp_connect(uint32_t ip, int port, int ms) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) return s;
    set_nonblocking(s);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = ip;
    a.sin_port = htons((uint16_t)port);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) {
        if (!would_block()) {
            close_sock(s);
            return BAD_SOCK;
        }
        pollfd_t p{};
        p.fd = s;
        p.events = POLLOUT;
        if (poll_fds(&p, 1, ms) <= 0 || (p.revents & (POLLERR | POLLHUP))) {
            close_sock(s);
            return BAD_SOCK;
        }
        int err = 0;
#if defined(_WIN32)
        int len = sizeof(err);
#else
        socklen_t len = sizeof(err);
#endif
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        if (err != 0) {
            close_sock(s);
            return BAD_SOCK;
        }
    }
    return s;
}

std::string ip_text(uint32_t ip) {
    char b[INET_ADDRSTRLEN] = "";
    in_addr a{};
    a.s_addr = ip;
    inet_ntop(AF_INET, &a, b, sizeof(b));
    return b;
}

// Connects node to a lan peer; the handshake starts with HELLO and its nonce.
Link* connect_lan(LocalNode& n, const DirEntry& e) {
    sock_t s = tcp_connect(e.ip, e.tcp, 2000);
    if (s == BAD_SOCK) {
        set_error("cannot connect to node " + e.display + "@" + e.host + " at " + ip_text(e.ip) + ":" + std::to_string(e.tcp));
        return nullptr;
    }
    tune_stream(s, true);
    Link l;
    l.id = g_next_link++;
    l.fd = s;
    l.node = n.handle;
    l.peer_id = e.id;
    l.peer_name = e.display;
    l.peer_host = e.host;
    l.outbound = true;
    l.lan = true;
    l.nonce_mine = random_hex(16);
    auto& stored = g_links[l.id] = std::move(l);
    Value hello = hello_map(n);
    hello.as_object()->set("NONCE", Value::make_string(stored.nonce_mine));
    send_frame(stored, make_frame(K_HELLO, 0, 0, body_of(hello)));
    wake_io();
    return &stored;
}

// Connects node to the peer of e; the caller holds g_mu.
Link* connect_link(LocalNode& n, const DirEntry& e) {
    if (Link* l = link_to(n.handle, e.id)) return l;
    if (e.lan) return connect_lan(n, e);
    sock_t s = BAD_SOCK;
    bool tcp = false;
    if (!e.sock.empty()) {
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s != BAD_SOCK) {
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            std::strncpy(a.sun_path, e.sock.c_str(), sizeof(a.sun_path) - 1);
            if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) {
                close_sock(s);
                s = BAD_SOCK;
            }
        }
    }
    if (s == BAD_SOCK && e.tcp > 0) {
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s != BAD_SOCK) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons((uint16_t)e.tcp);
            if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) {
                close_sock(s);
                s = BAD_SOCK;
            } else {
                tcp = true;
            }
        }
    }
    if (s == BAD_SOCK) {
        set_error("cannot connect to node " + e.display);
        return nullptr;
    }
    tune_stream(s, tcp);
    set_nonblocking(s);
    Link l;
    l.id = g_next_link++;
    l.fd = s;
    l.node = n.handle;
    l.peer_id = e.id;
    l.peer_name = e.display;
    l.peer_host = host_name();
    l.outbound = true;
    auto& stored = g_links[l.id] = std::move(l);
    send_frame(stored, make_frame(K_HELLO, 0, 0, hello_body(n)));
    wake_io();
    return &stored;
}

// The lan peers n may talk to: heard recently, with n's key.
std::vector<DirEntry> lan_peers_for(const LocalNode* n) {
    std::vector<DirEntry> out;
    if (!n || !n->lan) return out;
    int64_t now = now_ns();
    for (auto& kv : g_lan_peers)
        if (kv.second.fp == n->fp && now - kv.second.heard < PEER_EXPIRES) out.push_back(kv.second);
    return out;
}

// Every node n can reach: the directory of this machine, then the lan.
std::vector<DirEntry> peers_for(const LocalNode* n) {
    std::vector<DirEntry> out = read_directory();
    for (auto& e : lan_peers_for(n)) out.push_back(e);
    return out;
}

bool is_here(const std::string& host) {
    return host == host_name() || host == "localhost" || host == "127.0.0.1";
}

// The entry of the node addr names (name or name@host): this machine
// first, then the lan peers n hears.
bool find_peer(const LocalNode* n, const std::string& addr, DirEntry& out) {
    std::string name = lower(addr);
    std::string host;
    size_t at = name.find('@');
    if (at != std::string::npos) {
        host = name.substr(at + 1);
        name = name.substr(0, at);
    }
    if (host.empty() || is_here(host)) {
        for (auto& e : read_directory()) {
            if (e.name == name) {
                out = e;
                return true;
            }
        }
    }
    if (host.empty() || !is_here(host)) {
        for (auto& e : lan_peers_for(n)) {
            if (e.name == name && (host.empty() || e.host == host)) {
                out = e;
                return true;
            }
        }
    }
    if (!host.empty() && !is_here(host) && !(n && n->lan))
        set_error(addr + " is on another machine; join with scope \"lan\" to reach it");
    else
        set_error("no node named " + addr);
    return false;
}

// A live link from n to the node addr names: an open one when there is
// one, otherwise opened from the directory or the lan. The caller holds g_mu.
Link* reach(LocalNode& n, const std::string& addr) {
    std::string name = lower(addr);
    std::string host;
    size_t at = name.find('@');
    if (at != std::string::npos) {
        host = name.substr(at + 1);
        name = name.substr(0, at);
    }
    for (auto& kv : g_links) {
        Link& l = kv.second;
        if (l.node != n.handle || l.dead || l.peer_name.empty() || lower(l.peer_name) != name) continue;
        if (host.empty() || (is_here(host) ? is_here(l.peer_host) : l.peer_host == host)) return &l;
    }
    DirEntry e;
    if (!find_peer(&n, addr, e)) return nullptr;
    return connect_link(n, e);
}

void close_link(Link& l, bool say_bye) {
    if (l.dead) return;
    if (say_bye && l.fd != BAD_SOCK) {
        std::string bye = make_frame(K_BYE, 0, 0, body_of(Value::make_none()));
        send(l.fd, bye.data(), (int)bye.size(), SEND_FLAGS);
    }
    l.dead = true;
}

// Everything that hung on a link that is gone.
void forget_link(int64_t id) {
    for (auto& kv : g_pubs) {
        auto& subs = kv.second.subs;
        subs.erase(std::remove_if(subs.begin(), subs.end(), [&](const SubRef& r) { return r.link == id; }), subs.end());
    }
    for (auto it = g_atts.begin(); it != g_atts.end();) {
        if (it->second.link == id) it = g_atts.erase(it);
        else ++it;
    }
    for (auto it = g_tokens.begin(); it != g_tokens.end();) {
        if (it->second.link == id) it = g_tokens.erase(it);
        else ++it;
    }
    g_cv.notify_all();
}

// ── delivery ────────────────────────────────────────────────────────

void deliver_pub(Attachment& a, const std::string& body, uint64_t seq, int64_t t) {
    auto sit = g_subs.find(a.sub);
    if (sit == g_subs.end()) return;
    Subscription& s = sit->second;
    Value v = value_of(body);
    auto oit = g_offsets.find(a.peer_id);
    if (oit != g_offsets.end()) t -= oit->second.offset;
    if (s.envelope) {
        v = map_of({{"DATA", v},
                    {"FROM", Value::make_string(a.peer_name)},
                    {"TOPIC", Value::make_string(a.topic)},
                    {"SEQ", Value::make_i64((int64_t)seq)},
                    {"T", Value::make_i64(t)}});
    }
    s.received++;
    s.bytes += body.size();
    if (s.policy == "queue" || s.policy == "share") {
        s.pushed.push_back(a.id);
        s.pushed_total++;
    }
    push_value(*s.ch, std::move(v));
}

void handle_frame(Link& l, uint8_t kind, uint16_t flags, uint32_t stream, uint64_t seq, int64_t t, const std::string& body) {
    trace("<-", l, kind, stream, body.size() + HDR);
    LocalNode* n = node_of(l.node);
    if (!n) return;
    if (l.lan && !l.authed) {
        // the server takes HELLO then AUTH, the client WELCOME; nothing else
        bool expected = l.outbound ? kind == K_WELCOME : (kind == K_HELLO && l.nonce_peer.empty()) || (kind == K_AUTH && !l.nonce_peer.empty());
        if (!expected) {
            std::fprintf(stderr, "[node] lan link sent %s before the key was proven, closed\n", kind_name(kind));
            l.dead = true;
            return;
        }
    }
    switch (kind) {
        case K_HELLO: {
            Value m = value_of(body);
            l.peer_id = field_str(m, "ID");
            l.peer_name = field_str(m, "NAME");
            l.peer_host = field_str(m, "HOST");
            l.welcomed = true;
            if (!l.lan) {
                send_frame(l, make_frame(K_WELCOME, 0, 0, hello_body(*n)));
                return;
            }
            l.nonce_peer = field_str(m, "NONCE");
            if (l.nonce_peer.size() != 32) {
                l.dead = true;
                return;
            }
            l.nonce_mine = random_hex(16);
            Value w = hello_map(*n);
            w.as_object()->set("NONCE", Value::make_string(l.nonce_mine));
            w.as_object()->set("PROOF", Value::make_string(hmac_hex(n->key, "S" + l.nonce_peer + l.nonce_mine)));
            send_frame(l, make_frame(K_WELCOME, 0, 0, body_of(w)));
            return;
        }
        case K_WELCOME: {
            Value m = value_of(body);
            l.peer_name = field_str(m, "NAME");
            l.peer_host = field_str(m, "HOST");
            l.welcomed = true;
            if (!l.lan) return;
            l.nonce_peer = field_str(m, "NONCE");
            if (!same_secret(field_str(m, "PROOF"), hmac_hex(n->key, "S" + l.nonce_mine + l.nonce_peer))) {
                std::fprintf(stderr, "[node] %s@%s does not prove the key, link closed\n", l.peer_name.c_str(), l.peer_host.c_str());
                l.dead = true;
                return;
            }
            Value a = map_of({{"PROOF", Value::make_string(hmac_hex(n->key, "C" + l.nonce_mine + l.nonce_peer))}});
            send_frame(l, make_frame(K_AUTH, 0, 0, body_of(a)));
            release_held(l);
            return;
        }
        case K_AUTH: {
            Value m = value_of(body);
            if (!l.lan || !same_secret(field_str(m, "PROOF"), hmac_hex(n->key, "C" + l.nonce_peer + l.nonce_mine))) {
                std::fprintf(stderr, "[node] %s@%s does not prove the key, link closed\n", l.peer_name.c_str(), l.peer_host.c_str());
                l.dead = true;
                return;
            }
            release_held(l);
            return;
        }
        case K_PING: {
            Value m = value_of(body);
            int64_t t = now_ns();
            Value p = map_of({{"T1", Value::make_i64(field_int(m, "T1"))}, {"T2", Value::make_i64(t)}, {"T3", Value::make_i64(t)}});
            send_frame(l, make_frame(K_PONG, 0, 0, body_of(p)));
            return;
        }
        case K_PONG: {
            int64_t t4 = now_ns();
            Value m = value_of(body);
            int64_t t1 = field_int(m, "T1"), t2 = field_int(m, "T2"), t3 = field_int(m, "T3");
            int64_t rtt = (t4 - t1) - (t3 - t2);
            int64_t off = ((t2 - t1) + (t3 - t4)) / 2;
            l.samples.push_back({rtt, off});
            if (l.samples.size() > 32) l.samples.pop_front();
            auto best = *std::min_element(l.samples.begin(), l.samples.end());
            g_offsets[l.peer_id] = ClockOffset{best.second, best.first / 2};
            return;
        }
        case K_BYE:
            l.dead = true;
            return;
        case K_MSG:
            push_value(*n->inbox, value_of(body));
            return;
        case K_CALL: {
            Value m = value_of(body);
            int64_t token = g_next_token++;
            g_tokens[token] = CallToken{l.id, stream};
            std::string method = field_str(m, "M");
            Value* arg = m.as_object()->get("A");
            Value call = map_of({{"METHOD", Value::make_string(method)},
                                 {"ARG", arg ? *arg : Value::make_none()},
                                 {"FROM", Value::make_string(l.peer_name)},
                                 {"__call__", Value::make_i64(token)}});
            bool served = n->serve.count(lower(method)) > 0;
            push_value(served ? *n->served : *n->inbox, std::move(call));
            return;
        }
        case K_REPLY:
        case K_FAIL: {
            auto it = g_calls.find(stream);
            if (it == g_calls.end()) return;
            Value v = value_of(body);
            Value out;
            if (it->second.raw) {
                out = kind == K_REPLY ? v : map_of({{"ERROR", v}, {"__node_fail__", Value::make_bool(true)}});
            } else {
                out = map_of({{kind == K_REPLY ? "__reply__" : "__fail__", v}});
            }
            push_value(*it->second.ch, std::move(out));
            if (it->second.raw) chan_close(*it->second.ch);
            g_calls.erase(it);
            return;
        }
        case K_SUB: {
            Value m = value_of(body);
            std::string topic = field_str(m, "TOPIC");
            Publisher* pub = nullptr;
            for (auto& kv : g_pubs)
                if (kv.second.node == n->handle && kv.second.topic == topic) pub = &kv.second;
            if (!pub) {
                send_frame(l, make_frame(K_UNSUB, stream, 0, body_of(Value::make_none())));
                return;
            }
            SubRef r;
            r.link = l.id;
            r.att = stream;
            r.policy = field_str(m, "POLICY");
            r.credits = field_int(m, "CREDIT", 1);
            if (pub->has_retained) {
                if (r.policy == "queue" || r.policy == "share") r.credits--;
                send_frame(l, make_frame(K_PUB, stream, pub->seq, pub->retained, true));
            }
            pub->subs.push_back(r);
            g_cv.notify_all();
            return;
        }
        case K_UNSUB: {
            for (auto& kv : g_pubs) {
                auto& subs = kv.second.subs;
                subs.erase(std::remove_if(subs.begin(), subs.end(),
                                          [&](const SubRef& r) { return r.link == l.id && r.att == stream; }),
                           subs.end());
            }
            auto ait = g_atts.find(stream);
            if (ait != g_atts.end() && ait->second.link == l.id) g_atts.erase(ait);
            g_cv.notify_all();
            return;
        }
        case K_PUB: {
            auto ait = g_atts.find(stream);
            if (ait == g_atts.end() || ait->second.link != l.id) return;
            deliver_pub(ait->second, body, seq, (flags & F_TIME) ? t : now_ns());
            return;
        }
        case K_CREDIT: {
            int64_t add = value_of(body).to_int();
            for (auto& kv : g_pubs)
                for (auto& r : kv.second.subs)
                    if (r.link == l.id && r.att == stream) r.credits += add;
            g_cv.notify_all();
            return;
        }
    }
}

// Parses whatever complete frames l.rx holds.
void read_frames(Link& l) {
    size_t off = 0;
    while (l.rx.size() - off >= HDR) {
        const char* p = l.rx.data() + off;
        if (p[0] != 'J' || p[1] != 'B' || p[2] != 1) {
            std::fprintf(stderr, "[node] malformed frame from %s, link closed\n", l.peer_name.c_str());
            l.dead = true;
            return;
        }
        uint8_t kind = (uint8_t)p[3];
        uint16_t flags = get_le<uint16_t>(p + 4);
        uint32_t stream = get_le<uint32_t>(p + 8);
        uint32_t len = get_le<uint32_t>(p + 12);
        uint64_t seq = get_le<uint64_t>(p + 16);
        if (len > MAX_FRAME) {
            std::fprintf(stderr, "[node] frame of %u bytes from %s, link closed\n", len, l.peer_name.c_str());
            l.dead = true;
            return;
        }
        size_t extra = (flags & F_TIME) ? 8 : 0;
        if (l.rx.size() - off < HDR + extra + len) break;
        int64_t t = extra ? get_le<int64_t>(p + HDR) : 0;
        std::string body(p + HDR + extra, len);
        off += HDR + extra + len;
        try {
            handle_frame(l, kind, flags, stream, seq, t, body);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "[node] bad frame from %s (%s), link closed\n", l.peer_name.c_str(), ex.what());
            l.dead = true;
            return;
        }
        if (l.dead) break;
    }
    l.rx.erase(0, off);
}

void write_link(Link& l) {
    while (!l.tx.empty()) {
        const std::string& f = l.tx.front();
        int k = send(l.fd, f.data() + l.tx_off, (int)std::min<size_t>(f.size() - l.tx_off, 1 << 20), SEND_FLAGS);
        if (k < 0) {
            if (!would_block()) l.dead = true;
            return;
        }
        l.tx_off += (size_t)k;
        if (l.tx_off == f.size()) {
            l.tx_bytes -= f.size();
            l.tx.pop_front();
            l.tx_off = 0;
        }
    }
}

// ── subscriptions ───────────────────────────────────────────────────

// Attaches s to every topic of the directory that matches and is not attached yet.
void attach(Subscription& s, const std::vector<DirEntry>& dir) {
    LocalNode* n = node_of(s.node);
    if (!n) return;
    for (const DirEntry& e : dir) {
        for (const std::string& topic : e.topics) {
            if (!glob_match(s.pattern, topic)) continue;
            bool have = false;
            for (auto& kv : g_atts)
                if (kv.second.sub == s.id && kv.second.peer_id == e.id && kv.second.topic == topic) have = true;
            if (have) continue;
            Link* l = connect_link(*n, e);
            if (!l) continue;
            Attachment a;
            a.id = g_next_att++;
            a.sub = s.id;
            a.link = l->id;
            a.peer_id = e.id;
            a.peer_name = e.display;
            a.topic = topic;
            g_atts[a.id] = a;
            Value req = map_of({{"TOPIC", Value::make_string(topic)},
                                {"POLICY", Value::make_string(s.policy)},
                                {"CREDIT", Value::make_i64(s.cap)}});
            send_frame(*l, make_frame(K_SUB, a.id, 0, body_of(req)));
        }
    }
}

// Credits back to publishers for the values a queue subscription's reader took.
bool return_credits() {
    bool busy = false;
    for (auto& kv : g_subs) {
        Subscription& s = kv.second;
        if (s.policy != "queue" && s.policy != "share") continue;
        size_t len;
        {
            std::lock_guard<std::mutex> lock(s.ch->mtx);
            len = s.ch->buffer.size();
        }
        uint64_t taken = s.pushed_total - len - s.accounted;
        for (uint64_t i = 0; i < taken && !s.pushed.empty(); ++i) {
            auto ait = g_atts.find(s.pushed.front());
            if (ait != g_atts.end()) ait->second.to_return++;
            s.pushed.pop_front();
            s.accounted++;
        }
        if (len > 0) busy = true;
    }
    for (auto& kv : g_atts) {
        Attachment& a = kv.second;
        if (a.to_return <= 0) continue;
        auto sit = g_subs.find(a.sub);
        int64_t batch = sit == g_subs.end() ? 1 : std::max<int64_t>(1, sit->second.cap / 4);
        bool drained = sit != g_subs.end() && sit->second.pushed.empty();
        if (a.to_return < batch && !drained) continue;
        auto lit = g_links.find(a.link);
        if (lit != g_links.end())
            send_frame(lit->second, make_frame(K_CREDIT, a.id, 0, body_of(Value::make_i64(a.to_return))));
        a.to_return = 0;
    }
    return busy;
}

void unsubscribe_locked(int64_t sub_id) {
    for (auto it = g_atts.begin(); it != g_atts.end();) {
        if (it->second.sub == sub_id) {
            auto lit = g_links.find(it->second.link);
            if (lit != g_links.end()) send_frame(lit->second, make_frame(K_UNSUB, it->first, 0, body_of(Value::make_none())));
            it = g_atts.erase(it);
        } else {
            ++it;
        }
    }
    g_subs.erase(sub_id);
}

void unpublish_locked(int64_t handle) {
    auto it = g_pubs.find(handle);
    if (it == g_pubs.end()) return;
    for (auto& r : it->second.subs) {
        auto lit = g_links.find(r.link);
        if (lit != g_links.end()) send_frame(lit->second, make_frame(K_UNSUB, r.att, 0, body_of(Value::make_none())));
    }
    int64_t node = it->second.node;
    g_pubs.erase(it);
    if (LocalNode* n = node_of(node)) {
        write_dir_file(*n);
        if (n->lan) g_next_beacon = 0;
    }
    g_cv.notify_all();
}

// ── beacons (plan 7.2) ──────────────────────────────────────────────

// The IPv4 addresses of this machine's interfaces that are up, without
// loopback, in network byte order.
std::vector<uint32_t> local_ipv4s() {
    std::vector<uint32_t> out;
#if defined(_WIN32)
    char name[256] = {};
    gethostname(name, sizeof(name) - 1);
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
        for (addrinfo* p = res; p; p = p->ai_next) {
            uint32_t ip = ((sockaddr_in*)p->ai_addr)->sin_addr.s_addr;
            if ((ntohl(ip) >> 24) != 127 && std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
        }
        freeaddrinfo(res);
    }
#else
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* p = ifs; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
            if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
            uint32_t ip = ((sockaddr_in*)p->ifa_addr)->sin_addr.s_addr;
            if (std::find(out.begin(), out.end(), ip) == out.end()) out.push_back(ip);
        }
        freeifaddrs(ifs);
    }
#endif
    return out;
}

// JDB_NODE_SEEDS: hosts that get every beacon by unicast as well, for
// networks that carry no multicast or broadcast.
std::vector<uint32_t> seed_ips() {
    static std::vector<uint32_t> seeds = [] {
        std::vector<uint32_t> out;
        const char* env = std::getenv("JDB_NODE_SEEDS");
        if (!env) return out;
        std::stringstream ss(env);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (item.empty()) continue;
            addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            if (getaddrinfo(item.c_str(), nullptr, &hints, &res) == 0 && res) {
                out.push_back(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
                freeaddrinfo(res);
            } else {
                std::fprintf(stderr, "[node] JDB_NODE_SEEDS: cannot resolve %s\n", item.c_str());
            }
        }
        return out;
    }();
    return seeds;
}

void open_beacon() {
    if (g_beacon != BAD_SOCK) return;
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK) return;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
#if defined(SO_REUSEPORT)
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof(one));
#endif
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(BEACON_PORT);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
        std::fprintf(stderr, "[node] cannot bind the beacon port %u; only JDB_NODE_SEEDS will find peers\n", BEACON_PORT);
    }
    unsigned char ttl = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    unsigned char loop = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loop, sizeof(loop));
    ip_mreq mreq{};
    inet_pton(AF_INET, BEACON_GROUP, &mreq.imr_multiaddr);
    std::vector<uint32_t> ips = local_ipv4s();
    if (ips.empty()) ips.push_back(htonl(INADDR_ANY));
    for (uint32_t ip : ips) {
        mreq.imr_interface.s_addr = ip;
        setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));
    }
#if defined(_WIN32)
    // a unicast beacon to a closed port must not turn into an error on the next read
    DWORD no_reset = FALSE, ret = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &no_reset, sizeof(no_reset), nullptr, 0, &ret, nullptr, nullptr);
#endif
    set_nonblocking(s);
    g_beacon = s;
}

std::string beacon_packet(const LocalNode& n, bool leaving) {
    Value topics = Value::make_array();
    for (auto& kv : g_pubs)
        if (kv.second.node == n.handle) topics.as_array()->elements.push_back(Value::make_string(kv.second.topic));
    Value m = map_of({{"ID", Value::make_string(n.id)},
                      {"NAME", Value::make_string(n.display)},
                      {"HOST", Value::make_string(host_name())},
                      {"PORT", Value::make_i64(n.lan_port)},
                      {"PID", Value::make_i64(self_pid())},
                      {"FP", Value::make_string(n.fp)},
                      {"STARTED", Value::make_i64(n.started)},
                      {"TOPICS", topics},
                      {"LEAVE", Value::make_bool(leaving)}});
    return "JNB1" + body_of(m);
}

void send_beacon_to(const std::string& pkt, uint32_t ip) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = ip;
    to.sin_port = htons(BEACON_PORT);
    sendto(g_beacon, pkt.data(), (int)pkt.size(), 0, (const sockaddr*)&to, sizeof(to));
}

// One beacon of n to the group on every interface, to the broadcast
// address and to each seed.
void send_beacon(const LocalNode& n, bool leaving) {
    if (g_beacon == BAD_SOCK) return;
    std::string pkt = beacon_packet(n, leaving);
    uint32_t group = 0;
    inet_pton(AF_INET, BEACON_GROUP, &group);
    for (uint32_t ip : local_ipv4s()) {
        in_addr ifa{};
        ifa.s_addr = ip;
        setsockopt(g_beacon, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&ifa, sizeof(ifa));
        send_beacon_to(pkt, group);
    }
    send_beacon_to(pkt, htonl(INADDR_BROADCAST));
    for (uint32_t ip : seed_ips()) send_beacon_to(pkt, ip);
}

void send_beacons() {
    int64_t now = now_ns();
    if (now < g_next_beacon) return;
    g_next_beacon = now + BEACON_EVERY;
    for (auto& kv : g_nodes)
        if (kv.second.lan) send_beacon(kv.second, false);
}

void read_beacons() {
    if (g_beacon == BAD_SOCK) return;
    char buf[65536];
    for (;;) {
        sockaddr_in from{};
#if defined(_WIN32)
        int flen = sizeof(from);
#else
        socklen_t flen = sizeof(from);
#endif
        int k = recvfrom(g_beacon, buf, sizeof(buf), 0, (sockaddr*)&from, &flen);
        if (k <= 0) return;
        if (k < 4 || std::memcmp(buf, "JNB1", 4) != 0) continue;
        Value m;
        try {
            m = value_of(std::string(buf + 4, (size_t)k - 4));
        } catch (const std::exception&) {
            continue;
        }
        DirEntry e;
        e.id = field_str(m, "ID");
        e.display = field_str(m, "NAME");
        e.name = lower(e.display);
        e.host = lower(field_str(m, "HOST"));
        if (e.id.empty() || e.name.empty() || e.host == host_name()) continue;
        bool ours = false;
        for (auto& kv : g_nodes) if (kv.second.id == e.id) ours = true;
        if (ours) continue;
        if (field_int(m, "LEAVE") != 0) {
            g_lan_peers.erase(e.id);
            continue;
        }
        e.lan = true;
        e.ip = from.sin_addr.s_addr;
        e.tcp = (int)field_int(m, "PORT");
        e.pid = field_int(m, "PID");
        e.fp = field_str(m, "FP");
        e.started = field_int(m, "STARTED");
        e.heard = now_ns();
        if (Value* t = m.as_object()->get("TOPICS"))
            if (t->type == ValueType::ARRAY)
                for (auto& x : t->as_array()->elements) e.topics.push_back(x.to_string());
        bool fresh = !g_lan_peers.count(e.id);
        g_lan_peers[e.id] = e;
        if (fresh)
            for (auto& kv : g_nodes)
                if (kv.second.lan && kv.second.fp == e.fp) send_beacon_to(beacon_packet(kv.second, false), e.ip);
    }
}

void expire_lan_peers() {
    int64_t now = now_ns();
    for (auto it = g_lan_peers.begin(); it != g_lan_peers.end();) {
        if (now - it->second.heard >= PEER_EXPIRES) it = g_lan_peers.erase(it);
        else ++it;
    }
}

void ping_links() {
    int64_t now = now_ns();
    for (auto& kv : g_links) {
        Link& l = kv.second;
        if (!l.lan || !l.authed || l.dead || now < l.next_ping) continue;
        send_frame(l, make_frame(K_PING, 0, 0, body_of(map_of({{"T1", Value::make_i64(now)}}))));
        l.pings++;
        l.next_ping = now + (l.pings < 30 ? 100000000LL : 1000000000LL);
    }
}

// ── the I/O thread ──────────────────────────────────────────────────

void accept_on(LocalNode& n, bool lan) {
    for (;;) {
        sock_t s = accept(lan ? n.lan_lfd : n.lfd, nullptr, nullptr);
        if (s == BAD_SOCK) return;
        tune_stream(s, lan || !n.unix_sock);
        set_nonblocking(s);
        Link l;
        l.id = g_next_link++;
        l.fd = s;
        l.node = n.handle;
        l.lan = lan;
        g_links[l.id] = std::move(l);
    }
}

void scan_directory(std::chrono::steady_clock::time_point& next_scan) {
    auto now = std::chrono::steady_clock::now();
    if (now < next_scan) return;
    next_scan = now + std::chrono::milliseconds(250);
    bool need = false;
    for (auto& kv : g_nodes) if (kv.second.events) need = true;
    if (!g_subs.empty()) need = true;
    if (!need) return;
    std::vector<DirEntry> dir = read_directory();
    std::map<int64_t, std::vector<DirEntry>> seen;   // what each node reaches
    for (auto& kv : g_nodes) {
        std::vector<DirEntry> all = dir;
        for (auto& e : lan_peers_for(&kv.second)) all.push_back(e);
        seen[kv.first] = std::move(all);
    }
    for (auto& kv : g_subs) attach(kv.second, seen[kv.second.node]);
    for (auto& kv : g_nodes) {
        LocalNode& n = kv.second;
        if (!n.events) continue;
        std::set<std::string> now_names;
        for (auto& e : seen[kv.first]) if (e.id != n.id) now_names.insert(e.display);
        for (auto& nm : now_names)
            if (!n.known.count(nm))
                push_value(*n.events, map_of({{"EVENT", Value::make_string("JOIN")}, {"ADDR", Value::make_string(nm)}}));
        for (auto& nm : n.known)
            if (!now_names.count(nm))
                push_value(*n.events, map_of({{"EVENT", Value::make_string("LEAVE")}, {"ADDR", Value::make_string(nm)}}));
        n.known = now_names;
    }
}

void io_loop() {
    enum Owner : char { O_WAKE, O_LINK, O_LISTEN, O_LAN_LISTEN, O_BEACON };
    std::vector<pollfd_t> fds;
    std::vector<std::pair<Owner, int64_t>> owners;
    auto next_scan = std::chrono::steady_clock::now();
    bool busy = false;
    auto watch = [&](sock_t fd, short events, Owner o, int64_t id) {
        pollfd_t p{};
        p.fd = fd;
        p.events = events;
        fds.push_back(p);
        owners.push_back({o, id});
    };
    while (!g_stop.load()) {
        fds.clear();
        owners.clear();
        {
            std::lock_guard<std::mutex> lock(g_mu);
            watch(g_wake, POLLIN, O_WAKE, 0);
            if (g_beacon != BAD_SOCK) watch(g_beacon, POLLIN, O_BEACON, 0);
            for (auto& kv : g_nodes) {
                watch(kv.second.lfd, POLLIN, O_LISTEN, kv.first);
                if (kv.second.lan_lfd != BAD_SOCK) watch(kv.second.lan_lfd, POLLIN, O_LAN_LISTEN, kv.first);
            }
            for (auto& kv : g_links) {
                if (kv.second.dead) continue;
                watch(kv.second.fd, (short)(POLLIN | (kv.second.tx.empty() ? 0 : POLLOUT)), O_LINK, kv.first);
            }
        }
        poll_fds(fds.data(), fds.size(), busy ? 5 : 100);
        if (g_stop.load()) break;
        char buf[65536];
        if (fds[0].revents & POLLIN) {
            while (recv(g_wake, buf, sizeof(buf), 0) > 0) {}
        }
        std::lock_guard<std::mutex> lock(g_mu);
        for (size_t i = 1; i < fds.size(); ++i) {
            Owner o = owners[i].first;
            if (o == O_BEACON) {
                if (fds[i].revents & POLLIN) read_beacons();
                continue;
            }
            if (o == O_LISTEN || o == O_LAN_LISTEN) {
                if (fds[i].revents & POLLIN)
                    if (LocalNode* n = node_of(owners[i].second)) accept_on(*n, o == O_LAN_LISTEN);
                continue;
            }
            auto it = g_links.find(owners[i].second);
            if (it == g_links.end()) continue;
            Link& l = it->second;
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                for (;;) {
                    int k = recv(l.fd, buf, sizeof(buf), 0);
                    if (k > 0) {
                        l.rx.append(buf, (size_t)k);
                        if (k < (int)sizeof(buf)) break;
                        continue;
                    }
                    if (k == 0 || !would_block()) l.dead = true;
                    break;
                }
                read_frames(l);
            }
        }
        for (auto& kv : g_links) if (!kv.second.dead && !kv.second.tx.empty()) write_link(kv.second);
        std::vector<int64_t> gone_subs, gone_pubs;
        for (auto& kv : g_subs) if (kv.second.closed->load()) gone_subs.push_back(kv.first);
        for (auto& kv : g_pubs) if (kv.second.closed->load()) gone_pubs.push_back(kv.first);
        for (int64_t id : gone_subs) unsubscribe_locked(id);
        for (int64_t h : gone_pubs) unpublish_locked(h);
        for (auto it = g_links.begin(); it != g_links.end();) {
            if (it->second.dead) {
                close_sock(it->second.fd);
                forget_link(it->first);
                it = g_links.erase(it);
            } else {
                ++it;
            }
        }
        busy = return_credits();
        ping_links();
        send_beacons();
        expire_lan_peers();
        for (auto& kv : g_links) if (!kv.second.tx.empty()) busy = true;
        scan_directory(next_scan);
    }
}

void start_io() {
    if (g_io_running) return;
    g_wake = socket(AF_INET, SOCK_DGRAM, 0);
    g_wake_addr.sin_family = AF_INET;
    g_wake_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_wake_addr.sin_port = 0;
    bind(g_wake, (sockaddr*)&g_wake_addr, sizeof(g_wake_addr));
#if defined(_WIN32)
    int len = sizeof(g_wake_addr);
#else
    socklen_t len = sizeof(g_wake_addr);
#endif
    getsockname(g_wake, (sockaddr*)&g_wake_addr, &len);
    set_nonblocking(g_wake);
    g_stop = false;
    g_io = std::thread(io_loop);
    g_io_running = true;
}

// ── nodes ───────────────────────────────────────────────────────────

std::string new_id() {
    std::random_device rd;
    std::mt19937_64 gen(((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)now_ns() ^ ((uint64_t)self_pid() << 16));
    char b[17];
    std::snprintf(b, sizeof(b), "%016llx", (unsigned long long)gen());
    return b;
}

bool valid_name(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

void leave_locked(int64_t handle) {
    LocalNode* n = node_of(handle);
    if (!n) return;
    for (auto& kv : g_links) if (kv.second.node == handle) close_link(kv.second, true);
    std::vector<int64_t> subs, pubs;
    for (auto& kv : g_subs) if (kv.second.node == handle) subs.push_back(kv.first);
    for (auto& kv : g_pubs) if (kv.second.node == handle) pubs.push_back(kv.first);
    for (int64_t s : subs) {
        auto ch = g_subs[s].ch;
        g_subs.erase(s);
        if (ch) chan_close(*ch);
    }
    for (int64_t p : pubs) g_pubs.erase(p);
    if (n->lan) {
        send_beacon(*n, true);
        close_sock(n->lan_lfd);
    }
    close_sock(n->lfd);
    std::error_code ec;
    fs::remove(fs::u8path(n->dir_file), ec);
    if (n->unix_sock) fs::remove(fs::u8path(n->sock_path), ec);
    if (n->inbox) chan_close(*n->inbox);
    if (n->served) chan_close(*n->served);
    if (n->events) chan_close(*n->events);
    g_nodes.erase(handle);
    if (g_default == handle) g_default = g_nodes.empty() ? 0 : g_nodes.begin()->first;
    g_cv.notify_all();
}

void shutdown_all() {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        std::vector<int64_t> hs;
        for (auto& kv : g_nodes) hs.push_back(kv.first);
        for (int64_t h : hs) leave_locked(h);
    }
    if (g_io_running) {
        g_stop = true;
        wake_io();
        if (g_io.joinable()) g_io.join();
        g_io_running = false;
    }
}

int64_t join(const std::string& display, const std::string& scope, const std::string& key_arg) {
    if (!valid_name(display))
        throw std::runtime_error("a node name is 1 to 64 letters, digits, '_', '-' or '.', not \"" + display + "\"");
    std::string sc = lower(scope);
    std::string key = key_arg;
    if (sc == "lan" && key.empty()) {
        const char* env = std::getenv("JDB_NODE_KEY");
        if (env) key = env;
        if (key.empty())
            throw std::runtime_error("the lan scope needs a key: set JDB_NODE_KEY or pass it as the third argument");
    }
    if (sc != "local" && sc != "lan") throw std::runtime_error("scope must be \"local\" or \"lan\", not \"" + scope + "\"");
    if (!sockets_ready()) throw std::runtime_error("the socket layer did not start");
    std::string name = lower(display);
    std::lock_guard<std::mutex> lock(g_mu);
    for (auto& kv : g_nodes)
        if (kv.second.name == name) throw std::runtime_error("this program already has a node named " + display);

    fs::path dir = node_dir();
    fs::path file = dir / (name + ".json");
    DirEntry old;
    if (read_entry(file, old)) {
        if (pid_alive(old.pid))
            throw std::runtime_error("name \"" + display + "\" is in use by pid " + std::to_string(old.pid));
        std::error_code ec;
        fs::remove(file, ec);
        if (!old.sock.empty()) fs::remove(fs::u8path(old.sock), ec);
    }

    LocalNode n;
    n.handle = g_next_node++;
    n.id = new_id();
    n.name = name;
    n.display = display;
    n.started = (int64_t)std::time(nullptr);
    n.dir_file = file.u8string();
    n.sock_path = (dir / (n.id + ".sock")).u8string();
    n.lfd = BAD_SOCK;
    if (n.sock_path.size() < sizeof(sockaddr_un::sun_path)) {
        sock_t s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s != BAD_SOCK) {
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            std::strncpy(a.sun_path, n.sock_path.c_str(), sizeof(a.sun_path) - 1);
            std::error_code ec;
            fs::remove(fs::u8path(n.sock_path), ec);
            if (bind(s, (sockaddr*)&a, sizeof(a)) == 0 && listen(s, 64) == 0) n.lfd = s;
            else close_sock(s);
        }
    }
    if (n.lfd == BAD_SOCK) {
        n.unix_sock = false;
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (s == BAD_SOCK || bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 64) != 0) {
            close_sock(s);
            throw std::runtime_error("cannot open a socket for node " + display);
        }
#if defined(_WIN32)
        int len = sizeof(a);
#else
        socklen_t len = sizeof(a);
#endif
        getsockname(s, (sockaddr*)&a, &len);
        n.tcp_port = ntohs(a.sin_port);
        n.lfd = s;
    }
    set_nonblocking(n.lfd);
    if (sc == "lan") {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = 0;
        if (s == BAD_SOCK || bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 64) != 0) {
            close_sock(s);
            close_sock(n.lfd);
            throw std::runtime_error("cannot open a lan socket for node " + display);
        }
#if defined(_WIN32)
        int len = sizeof(a);
#else
        socklen_t len = sizeof(a);
#endif
        getsockname(s, (sockaddr*)&a, &len);
        set_nonblocking(s);
        n.lan = true;
        n.key = key;
        n.fp = key_fingerprint(key);
        n.lan_lfd = s;
        n.lan_port = ntohs(a.sin_port);
    }

    {
        // The exclusive create is the name lock between processes.
#if defined(_WIN32)
        FILE* f = _wfopen(file.wstring().c_str(), L"wx");
#else
        FILE* f = std::fopen(file.c_str(), "wx");
#endif
        if (!f) {
            close_sock(n.lfd);
            std::error_code ec;
            if (n.unix_sock) fs::remove(fs::u8path(n.sock_path), ec);
            throw std::runtime_error("name \"" + display + "\" was taken at the same moment");
        }
        std::fclose(f);
    }
    n.inbox = new_channel(INBOX_CAP, ChanPolicy::Ring, n.inbox_handle);
    int64_t served_handle = 0;
    n.served = new_channel(INBOX_CAP, ChanPolicy::Ring, served_handle);
    int64_t h = n.handle;
    auto& stored = g_nodes[h] = std::move(n);
    write_dir_file(stored);
    if (stored.lan) {
        open_beacon();
        g_next_beacon = 0;
        wake_io();
    }
    if (!g_default) g_default = h;
    start_io();
    return h;
}

LocalNode& require_node(int64_t h, const char* fn) {
    if (h == 0) h = g_default;
    LocalNode* n = node_of(h);
    if (!n) throw std::runtime_error(std::string(fn) + ": no node; call NODE.JOIN first");
    return *n;
}

std::string scope_arg(const std::vector<Value>& args) {
    return args.size() > 1 && args[1].type != ValueType::NONE ? args[1].to_string() : "local";
}

std::string key_arg(const std::vector<Value>& args) {
    return args.size() > 2 && args[2].type != ValueType::NONE ? args[2].to_string() : "";
}

int64_t node_arg(const std::vector<Value>& args, size_t i) {
    return args.size() > i && args[i].type != ValueType::NONE ? args[i].to_int() : 0;
}

void parse_policy(const std::string& text, std::string& policy, int64_t& cap) {
    std::string p = lower(text);
    size_t colon = p.find(':');
    std::string kind = p.substr(0, colon);
    int64_t n = colon == std::string::npos ? 0 : std::atoll(p.c_str() + colon + 1);
    if (kind == "latest") { policy = "latest"; cap = 1; return; }
    if (kind == "queue") { policy = "queue"; cap = n > 0 ? n : 256; return; }
    if (kind == "ring") { policy = "ring"; cap = n > 0 ? n : 64; return; }
    throw std::runtime_error("policy must be \"queue:N\", \"latest\" or \"ring:N\", not \"" + text + "\"");
}

int64_t subscribe(int64_t node, const std::string& pattern, const std::string& policy, int64_t cap, bool envelope) {
    std::lock_guard<std::mutex> lock(g_mu);
    LocalNode& n = require_node(node, "TOPIC.SUBSCRIBE");
    Subscription s;
    s.id = g_next_sub++;
    s.node = n.handle;
    s.pattern = pattern;
    s.policy = policy;
    s.cap = cap;
    s.envelope = envelope;
    ChanPolicy cp = policy == "latest" ? ChanPolicy::Latest : policy == "ring" ? ChanPolicy::Ring : ChanPolicy::Block;
    s.ch = new_channel(cap, cp, s.handle);
    s.closed = std::make_shared<std::atomic<bool>>(false);
    auto flag = s.closed;
    s.ch->on_close = [flag] { flag->store(true); wake_io(); };
    int64_t h = s.handle;
    auto& stored = g_subs[s.id] = std::move(s);
    attach(stored, peers_for(&n));
    wake_io();
    return h;
}

void publish(int64_t handle, const Value& v) {
    std::string body = body_of(v);
    std::unique_lock<std::mutex> lock(g_mu);
    auto it = g_pubs.find(handle);
    if (it == g_pubs.end()) throw std::runtime_error("CHAN.SEND: the topic is closed");
    Publisher* p = &it->second;
    auto credit_ok = [&](const SubRef& r) { return (r.policy != "queue" && r.policy != "share") || r.credits > 0; };
    if (p->blocking) {
        g_cv.wait(lock, [&] {
            auto again = g_pubs.find(handle);
            if (again == g_pubs.end() || g_stop.load()) return true;
            p = &again->second;
            bool share_free = false, any_share = false;
            for (auto& r : p->subs) {
                if (r.policy == "share") { any_share = true; if (r.credits > 0) share_free = true; }
                else if (!credit_ok(r)) return false;
            }
            return !any_share || share_free;
        });
        it = g_pubs.find(handle);
        if (it == g_pubs.end()) throw std::runtime_error("CHAN.SEND: the topic is closed");
        p = &it->second;
    }
    p->seq++;
    p->sent++;
    if (p->retain) {
        p->retained = body;
        p->has_retained = true;
    }
    std::vector<size_t> shares;
    for (size_t i = 0; i < p->subs.size(); ++i)
        if (p->subs[i].policy == "share") shares.push_back(i);
    if (!shares.empty()) {
        bool done = false;
        for (size_t k = 0; k < shares.size() && !done; ++k) {
            SubRef& r = p->subs[shares[(p->rr + k) % shares.size()]];
            if (r.credits <= 0) continue;
            auto lit = g_links.find(r.link);
            if (lit == g_links.end()) continue;
            r.credits--;
            send_frame(lit->second, make_frame(K_PUB, r.att, p->seq, body, true));
            p->bytes += body.size();
            p->rr = (p->rr + k + 1) % shares.size();
            done = true;
        }
        if (!done) p->dropped++;
    }
    for (auto& r : p->subs) {
        if (r.policy == "share") continue;
        auto lit = g_links.find(r.link);
        if (lit == g_links.end()) continue;
        Link& l = lit->second;
        if (r.policy == "queue") {
            if (r.credits <= 0) { r.dropped++; p->dropped++; continue; }
            r.credits--;
        } else if (l.tx_bytes > LATEST_BACKLOG) {
            r.dropped++;
            p->dropped++;
            continue;
        }
        send_frame(l, make_frame(K_PUB, r.att, p->seq, body, true));
        p->bytes += body.size();
    }
    lock.unlock();
    wake_io();
}

int64_t open_topic(int64_t node, const std::string& topic, bool blocking) {
    if (topic.empty() || topic.find_first_of("*?\"\\") != std::string::npos)
        throw std::runtime_error("TOPIC.OPEN: a topic name has no '*', '?', quote or backslash");
    std::lock_guard<std::mutex> lock(g_mu);
    LocalNode& n = require_node(node, "TOPIC.OPEN");
    for (auto& kv : g_pubs)
        if (kv.second.node == n.handle && kv.second.topic == topic)
            throw std::runtime_error("TOPIC.OPEN: this node already publishes " + topic);
    Publisher p;
    p.node = n.handle;
    p.topic = topic;
    p.blocking = blocking;
    int64_t h = 0;
    auto ch = new_channel(1, ChanPolicy::Block, h);
    p.handle = h;
    p.closed = std::make_shared<std::atomic<bool>>(false);
    auto flag = p.closed;
    ch->forward = [h](const Value& v) { publish(h, v); };
    ch->on_close = [flag] { flag->store(true); wake_io(); };
    g_pubs[h] = std::move(p);
    write_dir_file(n);
    if (n.lan) g_next_beacon = 0;
    return h;
}

Value call_wait(int64_t node, const std::string& addr, const std::string& method, const Value& arg, bool raw,
                int64_t& out_handle, std::shared_ptr<Channel>& out_ch) {
    std::lock_guard<std::mutex> lock(g_mu);
    LocalNode& n = require_node(node, raw ? "NODE.CALL_ASYNC" : "NODE.CALL");
    Link* l = reach(n, addr);
    if (!l) throw std::runtime_error(std::string(raw ? "NODE.CALL_ASYNC: " : "NODE.CALL: ") + g_last_error);
    uint32_t id = g_next_call++;
    PendingCall pc;
    pc.ch = new_channel(1, ChanPolicy::Block, out_handle);
    pc.raw = raw;
    out_ch = pc.ch;
    g_calls[id] = pc;
    Value req = map_of({{"M", Value::make_string(method)}, {"A", arg}});
    send_frame(*l, make_frame(K_CALL, id, 0, body_of(req)));
    wake_io();
    return Value::make_i64(id);
}

Value make_peer_map(const DirEntry& e) {
    Value topics = Value::make_array();
    for (auto& t : e.topics) topics.as_array()->elements.push_back(Value::make_string(t));
    Value m = map_of({{"NAME", Value::make_string(e.display)},
                   {"ADDR", Value::make_string(e.display + "@" + e.host)},
                   {"HOST", Value::make_string(e.host)},
                   {"PID", Value::make_i64(e.pid)},
                   {"SCOPE", Value::make_string(e.lan ? "lan" : "local")},
                   {"TOPICS", topics},
                   {"SINCE", Value::make_date((double)e.started)}});
    if (e.lan) {
        m.as_object()->set("IP", Value::make_string(ip_text(e.ip)));
        auto it = g_offsets.find(e.id);
        if (it != g_offsets.end()) {
            m.as_object()->set("OFFSET", Value::make_i64(it->second.offset));
            m.as_object()->set("ERROR", Value::make_i64(it->second.error));
        }
    }
    return m;
}

int64_t call_token(const Value& msg, const char* fn) {
    if (msg.type == ValueType::OBJECT)
        if (Value* t = msg.as_object()->get("__call__")) return t->to_int();
    throw std::runtime_error(std::string(fn) + ": expects a call taken from the inbox");
}

bool answer(const Value& msg, uint8_t kind, const Value& v, const char* fn) {
    int64_t token = call_token(msg, fn);
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_tokens.find(token);
    if (it == g_tokens.end()) return false;
    auto lit = g_links.find(it->second.link);
    bool ok = lit != g_links.end() && !lit->second.dead;
    if (ok) send_frame(lit->second, make_frame(kind, it->second.call_id, 0, body_of(v)));
    g_tokens.erase(it);
    wake_io();
    return ok;
}

}  // namespace

void register_node_builtins(VM& vm) {
    static std::once_flag exit_hook;
    std::call_once(exit_hook, [] { std::atexit(shutdown_all); });
    VM* vmp = &vm;

    // NODE.JOIN(name$, [scope$]) -> node handle; the first one is the default node.
    vm.register_native("NODE.JOIN", 1, 3, [](const std::vector<Value>& args) -> Value {
        try {
            return Value::make_i64(join(args[0].to_string(), scope_arg(args), key_arg(args)));
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("NODE.JOIN: ") + ex.what());
        }
    });

    // NODE.OPEN(name$, [scope$]) -> a further node of this program.
    vm.register_native("NODE.OPEN", 1, 3, [](const std::vector<Value>& args) -> Value {
        int64_t keep;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            keep = g_default;
        }
        int64_t h;
        try {
            h = join(args[0].to_string(), scope_arg(args), key_arg(args));
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("NODE.OPEN: ") + ex.what());
        }
        std::lock_guard<std::mutex> lock(g_mu);
        if (keep) g_default = keep;
        return Value::make_i64(h);
    });

    vm.register_native("NODE.LEAVE", 0, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        int64_t h = node_arg(args, 0);
        leave_locked(h ? h : g_default);
        return Value::make_none();
    });

    vm.register_native("NODE.NAME$", 0, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        LocalNode& n = require_node(node_arg(args, 0), "NODE.NAME$");
        return Value::make_string(n.display + "@" + host_name());
    });

    vm.register_native("NODE.INBOX", 0, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        return Value::make_i64(require_node(node_arg(args, 0), "NODE.INBOX").inbox_handle);
    });

    // NODE.PEERS([pattern$]) -> array of maps, every other node on this machine.
    vm.register_native("NODE.PEERS", 0, 1, [](const std::vector<Value>& args) -> Value {
        std::string pat = args.empty() || args[0].type == ValueType::NONE ? "*" : lower(args[0].to_string());
        std::lock_guard<std::mutex> lock(g_mu);
        std::string self_id;
        if (LocalNode* n = node_of(g_default)) self_id = n->id;
        Value out = Value::make_array();
        std::vector<DirEntry> dir = peers_for(node_of(g_default));
        std::sort(dir.begin(), dir.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
        for (auto& e : dir)
            if (e.id != self_id && glob_match(pat, e.name)) out.as_array()->elements.push_back(make_peer_map(e));
        return out;
    });

    // NODE.FIND(addr$, [timeout_ms]) -> TRUE once the node is in the directory.
    vm.register_native("NODE.FIND", 1, 2, [](const std::vector<Value>& args) -> Value {
        std::string addr = args[0].to_string();
        int64_t ms = args.size() > 1 ? args[1].to_int() : 5000;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms < 0 ? 0 : ms);
        for (;;) {
            DirEntry e;
            bool found;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                found = find_peer(node_of(g_default), addr, e);
            }
            if (found) return Value::make_bool(true);
            if (ms >= 0 && std::chrono::steady_clock::now() >= deadline) return Value::make_bool(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    vm.register_native("NODE.EVENTS", 0, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        LocalNode& n = require_node(node_arg(args, 0), "NODE.EVENTS");
        if (!n.events) {
            n.events = new_channel(256, ChanPolicy::Ring, n.events_handle);
            for (auto& e : peers_for(&n)) if (e.id != n.id) n.known.insert(e.display);
        }
        return Value::make_i64(n.events_handle);
    });

    // NODE.SEND(addr$, value, [node]) -> TRUE when the value is on its way.
    vm.register_native("NODE.SEND", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::string body = body_of(args[1]);
        std::lock_guard<std::mutex> lock(g_mu);
        LocalNode& n = require_node(node_arg(args, 2), "NODE.SEND");
        Link* l = reach(n, args[0].to_string());
        if (!l) return Value::make_bool(false);
        send_frame(*l, make_frame(K_MSG, 0, 0, body));
        wake_io();
        return Value::make_bool(true);
    });

    vm.register_native("NODE.ERROR$", 0, 0, [](const std::vector<Value>&) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        return Value::make_string(g_last_error);
    });

    // NODE.CALL(addr$, method$, arg, [timeout_ms], [node]) -> the reply.
    vm.register_native("NODE.CALL", 3, 5, [](const std::vector<Value>& args) -> Value {
        int64_t ms = args.size() > 3 && args[3].type != ValueType::NONE ? args[3].to_int() : 5000;
        int64_t h = 0;
        std::shared_ptr<Channel> ch;
        Value idv = call_wait(node_arg(args, 4), args[0].to_string(), args[1].to_string(), args[2], false, h, ch);
        Value got;
        {
            std::unique_lock<std::mutex> lk(ch->mtx);
            bool ready = ch->cv_recv.wait_for(lk, std::chrono::milliseconds(ms < 0 ? INT32_MAX : ms),
                                              [&] { return !ch->buffer.empty(); });
            if (ready) {
                got = ch->buffer.front();
                ch->buffer.pop_front();
            }
        }
        chan_unregister(h);
        if (got.type == ValueType::NONE) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_calls.erase((uint32_t)idv.to_int());
            return chan_make_timeout();
        }
        if (Value* r = got.as_object()->get("__reply__")) return *r;
        Value* f = got.as_object()->get("__fail__");
        throw std::runtime_error("remote " + args[0].to_string() + ": " + (f ? f->to_string() : "failed"));
    });

    // NODE.CALL_ASYNC(addr$, method$, arg, [node]) -> a channel with the reply.
    vm.register_native("NODE.CALL_ASYNC", 3, 4, [](const std::vector<Value>& args) -> Value {
        int64_t h = 0;
        std::shared_ptr<Channel> ch;
        call_wait(node_arg(args, 3), args[0].to_string(), args[1].to_string(), args[2], true, h, ch);
        return Value::make_i64(h);
    });

    vm.register_native("NODE.IS_CALL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::OBJECT && args[0].as_object()->get("__call__") != nullptr);
    });

    vm.register_native("NODE.IS_FAIL", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(args[0].type == ValueType::OBJECT && args[0].as_object()->get("__node_fail__") != nullptr);
    });

    vm.register_native("NODE.REPLY", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(answer(args[0], K_REPLY, args[1], "NODE.REPLY"));
    });

    vm.register_native("NODE.FAIL", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(answer(args[0], K_FAIL, Value::make_string(args[1].to_string()), "NODE.FAIL"));
    });

    // NODE.SERVE method$, func$, [node]: calls of method$ go to func$ in NODE.POLL.
    vm.register_native("NODE.SERVE", 2, 3, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        LocalNode& n = require_node(node_arg(args, 2), "NODE.SERVE");
        std::string fn = args[1].to_string();
        if (fn.empty()) n.serve.erase(lower(args[0].to_string()));
        else n.serve[lower(args[0].to_string())] = fn;
        return Value::make_none();
    });

    // NODE.POLL([timeout_ms], [node]) -> the number of served calls it ran.
    vm.register_native("NODE.POLL", 0, 2, [vmp](const std::vector<Value>& args) -> Value {
        int64_t ms = args.size() > 0 && args[0].type != ValueType::NONE ? args[0].to_int() : 0;
        std::shared_ptr<Channel> served;
        std::map<std::string, std::string> serve;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            LocalNode& n = require_node(node_arg(args, 1), "NODE.POLL");
            served = n.served;
            serve = n.serve;
        }
        int64_t ran = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms < 0 ? 0 : ms);
        for (;;) {
            Value call;
            {
                std::unique_lock<std::mutex> lk(served->mtx);
                if (served->buffer.empty() && ran == 0 && ms != 0) {
                    if (ms < 0) served->cv_recv.wait(lk, [&] { return !served->buffer.empty() || served->closed.load(); });
                    else served->cv_recv.wait_until(lk, deadline, [&] { return !served->buffer.empty() || served->closed.load(); });
                }
                if (served->buffer.empty()) break;
                call = served->buffer.front();
                served->buffer.pop_front();
            }
            auto it = serve.find(lower(field_str(call, "METHOD")));
            try {
                if (it == serve.end()) throw std::runtime_error("no handler for " + field_str(call, "METHOD"));
                Value result = vmp->call_function(it->second, {call});
                answer(call, K_REPLY, result, "NODE.POLL");
            } catch (const std::exception& ex) {
                answer(call, K_FAIL, Value::make_string(ex.what()), "NODE.POLL");
            }
            ran++;
        }
        return Value::make_i64(ran);
    });

    vm.register_native("NODE.TIME", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(now_ns());
    });

    // NODE.TIME_OF(addr$, t) -> a NODE.TIME of that node on this machine's clock.
    vm.register_native("NODE.TIME_OF", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t t = args[1].to_int();
        std::lock_guard<std::mutex> lock(g_mu);
        DirEntry e;
        if (!find_peer(node_of(g_default), args[0].to_string(), e) || !e.lan) return Value::make_i64(t);
        auto it = g_offsets.find(e.id);
        return Value::make_i64(it == g_offsets.end() ? t : t - it->second.offset);
    });

    vm.register_native("NODE.TRACE", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_trace = args[0].to_bool();
        return Value::make_none();
    });

    // TOPIC.OPEN(name$, [mode$], [node]) -> a channel; CHAN.SEND on it publishes.
    vm.register_native("TOPIC.OPEN", 1, 3, [](const std::vector<Value>& args) -> Value {
        std::string mode = args.size() > 1 && args[1].type != ValueType::NONE ? lower(args[1].to_string()) : "drop";
        if (mode != "drop" && mode != "block")
            throw std::runtime_error("TOPIC.OPEN: mode must be \"drop\" or \"block\", not \"" + mode + "\"");
        return Value::make_i64(open_topic(node_arg(args, 2), args[0].to_string(), mode == "block"));
    });

    // TOPIC.SUBSCRIBE(pattern$, [policy$], [envelope], [node]) -> a channel.
    vm.register_native("TOPIC.SUBSCRIBE", 1, 4, [](const std::vector<Value>& args) -> Value {
        std::string policy;
        int64_t cap = 0;
        try {
            parse_policy(args.size() > 1 && args[1].type != ValueType::NONE ? args[1].to_string() : "queue:256", policy, cap);
        } catch (const std::exception& ex) {
            throw std::runtime_error(std::string("TOPIC.SUBSCRIBE: ") + ex.what());
        }
        bool env = args.size() > 2 && args[2].to_bool();
        return Value::make_i64(subscribe(node_arg(args, 3), args[0].to_string(), policy, cap, env));
    });

    // TOPIC.SHARE(name$, [capacity], [node]) -> a channel; each value of the
    // topic reaches one of the programs sharing it.
    vm.register_native("TOPIC.SHARE", 1, 3, [](const std::vector<Value>& args) -> Value {
        int64_t cap = args.size() > 1 && args[1].type != ValueType::NONE ? args[1].to_int() : 64;
        if (cap < 1) throw std::runtime_error("TOPIC.SHARE: capacity must be at least 1");
        std::string name = args[0].to_string();
        if (name.find_first_of("*?") != std::string::npos)
            throw std::runtime_error("TOPIC.SHARE: takes a topic name, not a pattern");
        return Value::make_i64(subscribe(node_arg(args, 2), name, "share", cap, false));
    });

    vm.register_native("TOPIC.UNSUBSCRIBE", 1, 1, [](const std::vector<Value>& args) -> Value {
        auto ch = chan_lookup(args[0].to_int());
        if (ch) chan_close(*ch);
        return Value::make_none();
    });

    vm.register_native("TOPIC.RETAIN", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_pubs.find(args[0].to_int());
        if (it == g_pubs.end()) throw std::runtime_error("TOPIC.RETAIN: expects a channel TOPIC.OPEN returned");
        it->second.retain = args[1].to_bool();
        if (!it->second.retain) {
            it->second.has_retained = false;
            it->second.retained.clear();
        }
        return Value::make_none();
    });

    // TOPIC.LIST([pattern$]) -> {"TOPIC","ADDR"} for every published topic on this machine.
    vm.register_native("TOPIC.LIST", 0, 1, [](const std::vector<Value>& args) -> Value {
        std::string pat = args.empty() || args[0].type == ValueType::NONE ? "*" : args[0].to_string();
        Value out = Value::make_array();
        std::vector<DirEntry> dir;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            dir = peers_for(node_of(g_default));
        }
        std::sort(dir.begin(), dir.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
        for (auto& e : dir)
            for (auto& t : e.topics)
                if (glob_match(pat, t))
                    out.as_array()->elements.push_back(map_of({{"TOPIC", Value::make_string(t)},
                                                               {"ADDR", Value::make_string(e.display + "@" + e.host)}}));
        return out;
    });

    // TOPIC.STATS(ch) -> counters of a publisher or a subscription.
    vm.register_native("TOPIC.STATS", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t h = args[0].to_int();
        std::lock_guard<std::mutex> lock(g_mu);
        auto pit = g_pubs.find(h);
        if (pit != g_pubs.end()) {
            const Publisher& p = pit->second;
            return map_of({{"TOPIC", Value::make_string(p.topic)},
                           {"SENT", Value::make_i64((int64_t)p.sent)},
                           {"DROPPED", Value::make_i64((int64_t)p.dropped)},
                           {"BYTES", Value::make_i64((int64_t)p.bytes)},
                           {"SUBSCRIBERS", Value::make_i64((int64_t)p.subs.size())}});
        }
        for (auto& kv : g_subs) {
            const Subscription& s = kv.second;
            if (s.handle != h) continue;
            int64_t attached = 0;
            for (auto& a : g_atts) if (a.second.sub == s.id) attached++;
            uint64_t dropped;
            {
                std::lock_guard<std::mutex> lk(s.ch->mtx);
                dropped = s.ch->dropped;
            }
            return map_of({{"PATTERN", Value::make_string(s.pattern)},
                           {"RECEIVED", Value::make_i64((int64_t)s.received)},
                           {"DROPPED", Value::make_i64((int64_t)dropped)},
                           {"BYTES", Value::make_i64((int64_t)s.bytes)},
                           {"PUBLISHERS", Value::make_i64(attached)}});
        }
        throw std::runtime_error("TOPIC.STATS: expects a channel from TOPIC.OPEN, SUBSCRIBE or SHARE");
    });
}

std::string node_bus_report() {
    std::vector<DirEntry> dir = read_directory();
    std::sort(dir.begin(), dir.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    std::ostringstream o;
    o << "Node Bus directory: " << node_dir().u8string() << "\n";
    if (dir.empty()) {
        o << "no nodes\n";
        return o.str();
    }
    char line[256];
    std::snprintf(line, sizeof(line), "%-24s %8s  %-6s  %-19s  %s\n", "NAME", "PID", "LINK", "SINCE", "TOPICS");
    o << line;
    for (const DirEntry& e : dir) {
        char since[32] = "";
        std::time_t t = (std::time_t)e.started;
        if (std::tm* tm = std::localtime(&t)) std::strftime(since, sizeof(since), "%Y-%m-%d %H:%M:%S", tm);
        std::string topics;
        for (size_t i = 0; i < e.topics.size(); ++i) topics += (i ? ", " : "") + e.topics[i];
        std::snprintf(line, sizeof(line), "%-24s %8lld  %-6s  %-19s  ", e.display.c_str(), (long long)e.pid,
                      e.sock.empty() ? "tcp" : "unix", since);
        o << line << topics << "\n";
    }
    return o.str();
}
