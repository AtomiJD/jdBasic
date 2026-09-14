// net.cpp - NET.* builtins: TCP clients and servers, UDP sockets.
//
// A socket is a small integer handle. Sockets are non-blocking; every call
// that waits takes a timeout in milliseconds, and -1 waits until something
// happens. Strings carry bytes, so binary data passes unchanged.

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "net.h"
#include "vm.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

#if defined(_WIN32)
using sock_t = SOCKET;
using addr_len_t = int;
const sock_t BAD_SOCK = INVALID_SOCKET;
#else
using sock_t = int;
using addr_len_t = socklen_t;
const sock_t BAD_SOCK = -1;
#endif

enum class Kind { Stream, Listener, Datagram };

struct Sock {
    sock_t fd = BAD_SOCK;
    Kind kind = Kind::Stream;
    std::string rx;           // bytes received but not handed out yet
    bool peer_closed = false;
};

std::mutex g_socks_mu;
std::unordered_map<int64_t, Sock> g_socks;
int64_t g_next_handle = 1;

std::mutex g_error_mu;
std::string g_last_error;

void set_error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_error_mu);
    g_last_error = msg;
}

int last_code() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

std::string code_text(int code) {
#if defined(_WIN32)
    char buf[256] = {};
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, (DWORD)code, 0, buf, sizeof(buf), nullptr);
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '.'))
        s.pop_back();
    return s.empty() ? "socket error " + std::to_string(code) : s;
#else
    return std::strerror(code);
#endif
}

bool would_block(int code) {
#if defined(_WIN32)
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
#else
    return code == EWOULDBLOCK || code == EAGAIN || code == EINPROGRESS;
#endif
}

bool ensure_started() {
#if defined(_WIN32)
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ok;
#else
    return true;
#endif
}

void close_fd(sock_t fd) {
#if defined(_WIN32)
    closesocket(fd);
#else
    ::close(fd);
#endif
}

void set_nonblocking(sock_t fd) {
#if defined(_WIN32)
    u_long on = 1;
    ioctlsocket(fd, FIONBIO, &on);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

void no_sigpipe(sock_t fd) {
#if defined(SO_NOSIGPIPE)
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&on, sizeof(on));
#else
    (void)fd;
#endif
}

void no_delay(sock_t fd) {
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
}

#if defined(MSG_NOSIGNAL)
const int SEND_FLAGS = MSG_NOSIGNAL;
#else
const int SEND_FLAGS = 0;
#endif

timeval* to_timeval(int64_t timeout_ms, timeval& tv) {
    if (timeout_ms < 0) return nullptr;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    return &tv;
}

// 1 when the socket is readable (or writable), 0 on timeout, -1 on error.
int wait_fd(sock_t fd, bool for_write, int64_t timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv;
    int r = select((int)fd + 1, for_write ? nullptr : &set, for_write ? &set : nullptr,
                   nullptr, to_timeval(timeout_ms, tv));
    return r > 0 ? 1 : (r == 0 ? 0 : -1);
}

// A connect that is still under way is done when the socket turns writable;
// Windows reports a failed one through the exception set instead.
int wait_connect(sock_t fd, int64_t timeout_ms) {
    fd_set wset, eset;
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    FD_SET(fd, &wset);
    FD_SET(fd, &eset);
    timeval tv;
    int r = select((int)fd + 1, nullptr, &wset, &eset, to_timeval(timeout_ms, tv));
    return r > 0 ? 1 : (r == 0 ? 0 : -1);
}

using steady = std::chrono::steady_clock;

int64_t remaining_ms(int64_t timeout_ms, steady::time_point start) {
    if (timeout_ms < 0) return -1;
    int64_t used = std::chrono::duration_cast<std::chrono::milliseconds>(steady::now() - start).count();
    return std::max<int64_t>(0, timeout_ms - used);
}

int64_t add_sock(Sock s) {
    std::lock_guard<std::mutex> lock(g_socks_mu);
    int64_t id = g_next_handle++;
    g_socks.emplace(id, std::move(s));
    return id;
}

Sock* get_sock(int64_t h, const char* fn) {
    std::lock_guard<std::mutex> lock(g_socks_mu);
    auto it = g_socks.find(h);
    if (it == g_socks.end())
        throw std::runtime_error(std::string(fn) + ": no open socket " + std::to_string(h));
    return &it->second;
}

void need_args(const std::vector<Value>& args, size_t n, const char* fn) {
    if (args.size() < n)
        throw std::runtime_error(std::string(fn) + " expects at least " + std::to_string(n) + " arguments");
}

std::string text_of(const Value& v) {
    return v.type == ValueType::STRING ? v.as_string()->data : v.to_string();
}

addrinfo* resolve(const std::string& host, int64_t port, int socktype, bool passive) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    if (passive) hints.ai_flags = AI_PASSIVE;
    addrinfo* res = nullptr;
    std::string port_text = std::to_string(port);
    int rc = getaddrinfo(host.empty() ? nullptr : host.c_str(), port_text.c_str(), &hints, &res);
    if (rc != 0) {
        set_error("cannot resolve " + host + ": " + std::string(gai_strerror(rc)));
        return nullptr;
    }
    return res;
}

std::string addr_host(const sockaddr_storage& ss) {
    char buf[INET6_ADDRSTRLEN] = {};
    if (ss.ss_family == AF_INET) {
        const auto* a = (const sockaddr_in*)&ss;
        inet_ntop(AF_INET, (void*)&a->sin_addr, buf, sizeof(buf));
    } else if (ss.ss_family == AF_INET6) {
        const auto* a = (const sockaddr_in6*)&ss;
        inet_ntop(AF_INET6, (void*)&a->sin6_addr, buf, sizeof(buf));
    }
    return buf;
}

int64_t addr_port(const sockaddr_storage& ss) {
    if (ss.ss_family == AF_INET) return ntohs(((const sockaddr_in*)&ss)->sin_port);
    if (ss.ss_family == AF_INET6) return ntohs(((const sockaddr_in6*)&ss)->sin6_port);
    return 0;
}

// Waits up to timeout_ms for bytes and appends what arrives to s->rx.
// Answers false on timeout, error, or when the peer has closed.
bool pull(Sock* s, int64_t timeout_ms) {
    if (s->peer_closed) return false;
    int w = wait_fd(s->fd, false, timeout_ms);
    if (w <= 0) return false;
    char buf[8192];
    int n = (int)::recv(s->fd, buf, sizeof(buf), 0);
    if (n > 0) {
        s->rx.append(buf, (size_t)n);
        return true;
    }
    if (n == 0) {
        s->peer_closed = true;
        return false;
    }
    int code = last_code();
    if (would_block(code)) return true;
    s->peer_closed = true;
    set_error(code_text(code));
    return false;
}

Sock* stream_sock(const std::vector<Value>& args, const char* fn) {
    Sock* s = get_sock(args[0].to_int(), fn);
    if (s->kind != Kind::Stream)
        throw std::runtime_error(std::string(fn) + ": socket " + std::to_string(args[0].to_int()) +
                                 " is not a TCP connection");
    return s;
}

// Binds a listening or UDP socket to host:port; 0 on failure with the error set.
sock_t bound_socket(const std::string& host, int64_t port, int socktype, const char* fn) {
    addrinfo* res = resolve(host, port, socktype, true);
    if (!res) return BAD_SOCK;
    std::string err = "no address for " + host;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        sock_t fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == BAD_SOCK) {
            err = code_text(last_code());
            continue;
        }
#if !defined(_WIN32)
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
#endif
        if (bind(fd, ai->ai_addr, (addr_len_t)ai->ai_addrlen) != 0 ||
            (socktype == SOCK_STREAM && listen(fd, SOMAXCONN) != 0)) {
            err = code_text(last_code());
            close_fd(fd);
            continue;
        }
        set_nonblocking(fd);
        freeaddrinfo(res);
        return fd;
    }
    freeaddrinfo(res);
    set_error(std::string(fn) + " " + host + ":" + std::to_string(port) + ": " + err);
    return BAD_SOCK;
}

void close_all() {
    std::lock_guard<std::mutex> lock(g_socks_mu);
    for (auto& kv : g_socks) close_fd(kv.second.fd);
    g_socks.clear();
}

} // namespace

void register_net_builtins(VM& vm) {
    static std::once_flag exit_hook;
    std::call_once(exit_hook, [] { std::atexit(close_all); });

    // NET.CONNECT(host$, port, [timeout_ms]) -> handle, 0 when it fails.
    vm.register_native("NET.CONNECT", [](const std::vector<Value>& args) -> Value {
        need_args(args, 2, "NET.CONNECT");
        std::string host = text_of(args[0]);
        int64_t port = args[1].to_int();
        int64_t timeout = args.size() > 2 ? args[2].to_int() : 5000;
        if (!ensure_started()) {
            set_error("NET.CONNECT: the socket layer did not start");
            return Value::make_i64(0);
        }
        addrinfo* res = resolve(host, port, SOCK_STREAM, false);
        if (!res) return Value::make_i64(0);
        std::string err = "no address for " + host;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            sock_t fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd == BAD_SOCK) {
                err = code_text(last_code());
                continue;
            }
            set_nonblocking(fd);
            no_sigpipe(fd);
            if (connect(fd, ai->ai_addr, (addr_len_t)ai->ai_addrlen) != 0) {
                int code = last_code();
                if (!would_block(code)) {
                    err = code_text(code);
                    close_fd(fd);
                    continue;
                }
                int w = wait_connect(fd, timeout);
                if (w != 1) {
                    err = (w == 0) ? "connection timed out" : code_text(last_code());
                    close_fd(fd);
                    continue;
                }
                int so_error = 0;
                addr_len_t len = sizeof(so_error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len);
                if (so_error != 0) {
                    err = code_text(so_error);
                    close_fd(fd);
                    continue;
                }
            }
            no_delay(fd);
            freeaddrinfo(res);
            Sock s;
            s.fd = fd;
            s.kind = Kind::Stream;
            return Value::make_i64(add_sock(std::move(s)));
        }
        freeaddrinfo(res);
        set_error("NET.CONNECT " + host + ":" + std::to_string(port) + ": " + err);
        return Value::make_i64(0);
    });

    // NET.LISTEN(port, [bind$]) -> handle of a listening socket, 0 when it fails.
    vm.register_native("NET.LISTEN", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.LISTEN");
        int64_t port = args[0].to_int();
        std::string host = args.size() > 1 ? text_of(args[1]) : "0.0.0.0";
        if (!ensure_started()) {
            set_error("NET.LISTEN: the socket layer did not start");
            return Value::make_i64(0);
        }
        sock_t fd = bound_socket(host, port, SOCK_STREAM, "NET.LISTEN");
        if (fd == BAD_SOCK) return Value::make_i64(0);
        Sock s;
        s.fd = fd;
        s.kind = Kind::Listener;
        return Value::make_i64(add_sock(std::move(s)));
    });

    // NET.ACCEPT(listener, [timeout_ms]) -> handle of the new connection, 0 on timeout.
    vm.register_native("NET.ACCEPT", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.ACCEPT");
        Sock* l = get_sock(args[0].to_int(), "NET.ACCEPT");
        if (l->kind != Kind::Listener)
            throw std::runtime_error("NET.ACCEPT: socket " + std::to_string(args[0].to_int()) +
                                     " is not listening");
        int64_t timeout = args.size() > 1 ? args[1].to_int() : -1;
        int w = wait_fd(l->fd, false, timeout);
        if (w <= 0) {
            if (w < 0) set_error("NET.ACCEPT: " + code_text(last_code()));
            return Value::make_i64(0);
        }
        sockaddr_storage ss{};
        addr_len_t len = sizeof(ss);
        sock_t fd = accept(l->fd, (sockaddr*)&ss, &len);
        if (fd == BAD_SOCK) {
            set_error("NET.ACCEPT: " + code_text(last_code()));
            return Value::make_i64(0);
        }
        set_nonblocking(fd);
        no_sigpipe(fd);
        no_delay(fd);
        Sock s;
        s.fd = fd;
        s.kind = Kind::Stream;
        return Value::make_i64(add_sock(std::move(s)));
    });

    // NET.SEND(handle, data$) -> bytes sent, -1 when nothing could be sent.
    vm.register_native("NET.SEND", [](const std::vector<Value>& args) -> Value {
        need_args(args, 2, "NET.SEND");
        Sock* s = stream_sock(args, "NET.SEND");
        std::string data = text_of(args[1]);
        size_t sent = 0;
        while (sent < data.size()) {
            int n = (int)::send(s->fd, data.data() + sent, (int)(data.size() - sent), SEND_FLAGS);
            if (n > 0) {
                sent += (size_t)n;
                continue;
            }
            int code = last_code();
            if (n < 0 && would_block(code)) {
                if (wait_fd(s->fd, true, 30000) == 1) continue;
                set_error("NET.SEND: timed out");
                break;
            }
            set_error("NET.SEND: " + code_text(code));
            s->peer_closed = true;
            break;
        }
        if (sent == 0 && !data.empty()) return Value::make_i64(-1);
        return Value::make_i64((int64_t)sent);
    });

    // NET.RECV$(handle, [max_bytes], [timeout_ms]) -> what arrived, "" on timeout or close.
    vm.register_native("NET.RECV$", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.RECV$");
        Sock* s = stream_sock(args, "NET.RECV$");
        int64_t max_bytes = args.size() > 1 ? args[1].to_int() : 65536;
        if (max_bytes <= 0) max_bytes = 65536;
        int64_t timeout = args.size() > 2 ? args[2].to_int() : -1;
        auto start = steady::now();
        while (s->rx.empty()) {
            bool got = pull(s, remaining_ms(timeout, start));
            if (!got) break;
            if (timeout >= 0 && remaining_ms(timeout, start) == 0 && s->rx.empty()) break;
        }
        size_t take = std::min(s->rx.size(), (size_t)max_bytes);
        std::string out = s->rx.substr(0, take);
        s->rx.erase(0, take);
        return Value::make_string(out);
    });

    // NET.RECVLINE$(handle, [timeout_ms]) -> the next line without its line end,
    // "" on timeout; after a close, the rest of an unfinished line.
    vm.register_native("NET.RECVLINE$", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.RECVLINE$");
        Sock* s = stream_sock(args, "NET.RECVLINE$");
        int64_t timeout = args.size() > 1 ? args[1].to_int() : -1;
        auto start = steady::now();
        for (;;) {
            size_t nl = s->rx.find('\n');
            if (nl != std::string::npos) {
                std::string line = s->rx.substr(0, nl);
                s->rx.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return Value::make_string(line);
            }
            int64_t rem = remaining_ms(timeout, start);
            if (timeout >= 0 && rem == 0) return Value::make_string("");
            if (!pull(s, rem)) {
                if (s->peer_closed && !s->rx.empty()) {
                    std::string line = s->rx;
                    s->rx.clear();
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return Value::make_string(line);
                }
                if (s->peer_closed || timeout >= 0) return Value::make_string("");
            }
        }
    });

    // NET.UDP([port], [bind$]) -> handle of a UDP socket, 0 when it fails.
    vm.register_native("NET.UDP", [](const std::vector<Value>& args) -> Value {
        int64_t port = args.size() > 0 ? args[0].to_int() : 0;
        std::string host = args.size() > 1 ? text_of(args[1]) : "0.0.0.0";
        if (!ensure_started()) {
            set_error("NET.UDP: the socket layer did not start");
            return Value::make_i64(0);
        }
        sock_t fd = bound_socket(host, port, SOCK_DGRAM, "NET.UDP");
        if (fd == BAD_SOCK) return Value::make_i64(0);
#if defined(_WIN32)
        // Without this a datagram to a closed port makes the next receive fail.
        BOOL report = FALSE;
        DWORD ret = 0;
        WSAIoctl(fd, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &ret, nullptr, nullptr);
#endif
        Sock s;
        s.fd = fd;
        s.kind = Kind::Datagram;
        return Value::make_i64(add_sock(std::move(s)));
    });

    // NET.SENDTO(handle, host$, port, data$) -> bytes sent, -1 on error.
    vm.register_native("NET.SENDTO", [](const std::vector<Value>& args) -> Value {
        need_args(args, 4, "NET.SENDTO");
        Sock* s = get_sock(args[0].to_int(), "NET.SENDTO");
        if (s->kind != Kind::Datagram)
            throw std::runtime_error("NET.SENDTO: socket " + std::to_string(args[0].to_int()) +
                                     " is not a UDP socket");
        std::string host = text_of(args[1]);
        int64_t port = args[2].to_int();
        std::string data = text_of(args[3]);
        sockaddr_storage local{};
        addr_len_t local_len = sizeof(local);
        getsockname(s->fd, (sockaddr*)&local, &local_len);
        addrinfo* res = resolve(host, port, SOCK_DGRAM, false);
        if (!res) return Value::make_i64(-1);
        int64_t result = -1;
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
            if (ai->ai_family != local.ss_family) continue;
            int n = (int)::sendto(s->fd, data.data(), (int)data.size(), 0, ai->ai_addr, (addr_len_t)ai->ai_addrlen);
            if (n >= 0) {
                result = n;
                break;
            }
            set_error("NET.SENDTO: " + code_text(last_code()));
        }
        if (result < 0 && res) set_error("NET.SENDTO " + host + ":" + std::to_string(port) + ": no usable address");
        freeaddrinfo(res);
        return Value::make_i64(result);
    });

    // NET.RECVFROM(handle, [timeout_ms]) -> {data, host, port}, NONE on timeout.
    vm.register_native("NET.RECVFROM", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.RECVFROM");
        Sock* s = get_sock(args[0].to_int(), "NET.RECVFROM");
        if (s->kind != Kind::Datagram)
            throw std::runtime_error("NET.RECVFROM: socket " + std::to_string(args[0].to_int()) +
                                     " is not a UDP socket");
        int64_t timeout = args.size() > 1 ? args[1].to_int() : -1;
        if (wait_fd(s->fd, false, timeout) <= 0) return Value::make_none();
        std::vector<char> buf(65536);
        sockaddr_storage from{};
        addr_len_t len = sizeof(from);
        int n = (int)::recvfrom(s->fd, buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &len);
        if (n < 0) {
            int code = last_code();
            if (!would_block(code)) set_error("NET.RECVFROM: " + code_text(code));
            return Value::make_none();
        }
        Value result = Value::make_object();
        result.as_object()->set("data", Value::make_string(std::string(buf.data(), (size_t)n)));
        result.as_object()->set("host", Value::make_string(addr_host(from)));
        result.as_object()->set("port", Value::make_i64(addr_port(from)));
        return result;
    });

    // NET.CLOSE(handle) -> TRUE when a socket was closed.
    vm.register_native("NET.CLOSE", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.CLOSE");
        std::lock_guard<std::mutex> lock(g_socks_mu);
        auto it = g_socks.find(args[0].to_int());
        if (it == g_socks.end()) return Value::make_bool(false);
        close_fd(it->second.fd);
        g_socks.erase(it);
        return Value::make_bool(true);
    });

    // NET.ALIVE(handle) -> TRUE while the socket is open and the peer has not closed.
    vm.register_native("NET.ALIVE", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.ALIVE");
        Sock* s = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_socks_mu);
            auto it = g_socks.find(args[0].to_int());
            if (it == g_socks.end()) return Value::make_bool(false);
            s = &it->second;
        }
        if (s->kind != Kind::Stream) return Value::make_bool(true);
        if (s->peer_closed) return Value::make_bool(!s->rx.empty());
        if (wait_fd(s->fd, false, 0) == 1) {
            char probe;
            int n = (int)::recv(s->fd, &probe, 1, MSG_PEEK);
            if (n == 0) {
                s->peer_closed = true;
                return Value::make_bool(!s->rx.empty());
            }
            if (n < 0 && !would_block(last_code())) {
                s->peer_closed = true;
                return Value::make_bool(!s->rx.empty());
            }
        }
        return Value::make_bool(true);
    });

    // NET.PEER$(handle) -> "host:port" of the other end of a TCP connection.
    vm.register_native("NET.PEER$", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.PEER$");
        Sock* s = get_sock(args[0].to_int(), "NET.PEER$");
        if (s->kind != Kind::Stream) return Value::make_string("");
        sockaddr_storage ss{};
        addr_len_t len = sizeof(ss);
        if (getpeername(s->fd, (sockaddr*)&ss, &len) != 0) return Value::make_string("");
        std::string host = addr_host(ss);
        if (ss.ss_family == AF_INET6) host = "[" + host + "]";
        return Value::make_string(host + ":" + std::to_string(addr_port(ss)));
    });

    // NET.PORT(handle) -> the local port, which answers what NET.LISTEN(0) picked.
    vm.register_native("NET.PORT", [](const std::vector<Value>& args) -> Value {
        need_args(args, 1, "NET.PORT");
        Sock* s = get_sock(args[0].to_int(), "NET.PORT");
        sockaddr_storage ss{};
        addr_len_t len = sizeof(ss);
        if (getsockname(s->fd, (sockaddr*)&ss, &len) != 0) return Value::make_i64(0);
        return Value::make_i64(addr_port(ss));
    });

    // NET.ERROR$() -> the message of the last failure.
    vm.register_native("NET.ERROR$", [](const std::vector<Value>& args) -> Value {
        (void)args;
        std::lock_guard<std::mutex> lock(g_error_mu);
        return Value::make_string(g_last_error);
    });
}
