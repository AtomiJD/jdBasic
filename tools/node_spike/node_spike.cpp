// node_spike.cpp - transport measurements for the Node Bus (doc/design/node_bus_plan.md, P0).
//
// Starts itself a second time as a child process and measures, between the two
// processes, for loopback TCP, AF_UNIX and a shared-memory byte ring:
//   - round-trip latency of a 64-byte message (median, p99)
//   - one-way throughput for 1 KB, 16 KB, 256 KB and 8 MB messages
//
//   node_spike [tcp|unix|shm ...]      all three when none is named
//
// Windows: build.bat-style cl call, see build_spike.bat. POSIX: build_spike.sh.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#include <intrin.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#define BAD_SOCK INVALID_SOCKET
static void cpu_relax() { _mm_pause(); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#define BAD_SOCK (-1)
static void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}
#endif

using clk = std::chrono::steady_clock;

static const int LAT_WARMUP = 1000;
static const int LAT_ROUNDS = 20000;
static const size_t LAT_SIZE = 64;
static const size_t SIZES[] = {1024, 16 * 1024, 256 * 1024, 8 * 1024 * 1024};
static const size_t THR_TOTAL = 512ull * 1024 * 1024;
static const size_t RING_CAP = 64ull * 1024 * 1024;

static void die(const char* what) {
    std::fprintf(stderr, "node_spike: %s\n", what);
    std::exit(2);
}

struct Stream {
    virtual ~Stream() = default;
    virtual void write_all(const void* p, size_t n) = 0;
    virtual void read_all(void* p, size_t n) = 0;
};

// ---------------------------------------------------------------- sockets

struct SockStream : Stream {
    sock_t s;
    explicit SockStream(sock_t s_) : s(s_) {}
    ~SockStream() override { CLOSESOCK(s); }
    void write_all(const void* p, size_t n) override {
        const char* c = static_cast<const char*>(p);
        while (n) {
            int chunk = (int)std::min<size_t>(n, 1 << 30);
            int k = send(s, c, chunk, 0);
            if (k <= 0) die("send failed");
            c += k;
            n -= (size_t)k;
        }
    }
    void read_all(void* p, size_t n) override {
        char* c = static_cast<char*>(p);
        while (n) {
            int chunk = (int)std::min<size_t>(n, 1 << 30);
            int k = recv(s, c, chunk, 0);
            if (k <= 0) die("recv failed");
            c += k;
            n -= (size_t)k;
        }
    }
};

static void sock_tune(sock_t s, bool tcp) {
    int one = 1;
    if (tcp) setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    int buf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));
}

static std::string unix_path() {
#ifdef _WIN32
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    return std::string(tmp) + "jdbspike_" + std::to_string(GetCurrentProcessId()) + ".sock";
#else
    const char* t = std::getenv("TMPDIR");
    std::string dir = t && *t ? t : "/tmp";
    if (dir.back() != '/') dir += '/';
    return dir + "jdbspike_" + std::to_string(getpid()) + ".sock";
#endif
}

static void unlink_path(const std::string& p) {
#ifdef _WIN32
    DeleteFileA(p.c_str());
#else
    unlink(p.c_str());
#endif
}

// Listener for the parent; returns the address the child connects to.
static sock_t listen_on(const std::string& kind, std::string& addr) {
    if (kind == "tcp") {
        sock_t l = socket(AF_INET, SOCK_STREAM, 0);
        if (l == BAD_SOCK) die("socket(AF_INET)");
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(l, (sockaddr*)&a, sizeof(a)) != 0) die("bind tcp");
        socklen_t len = sizeof(a);
        getsockname(l, (sockaddr*)&a, &len);
        if (listen(l, 1) != 0) die("listen tcp");
        addr = std::to_string(ntohs(a.sin_port));
        return l;
    }
    sock_t l = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l == BAD_SOCK) die("socket(AF_UNIX)");
    addr = unix_path();
    unlink_path(addr);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, addr.c_str(), sizeof(a.sun_path) - 1);
    if (bind(l, (sockaddr*)&a, sizeof(a)) != 0) die("bind unix");
    if (listen(l, 1) != 0) die("listen unix");
    return l;
}

static sock_t connect_to(const std::string& kind, const std::string& addr) {
    if (kind == "tcp") {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t)std::atoi(addr.c_str()));
        if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) die("connect tcp");
        sock_tune(s, true);
        return s;
    }
    sock_t s = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, addr.c_str(), sizeof(a.sun_path) - 1);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) die("connect unix");
    sock_tune(s, false);
    return s;
}

// ---------------------------------------------------------------- shared memory ring

struct RingHdr {
    alignas(64) std::atomic<uint64_t> head;  // bytes written
    alignas(64) std::atomic<uint64_t> tail;  // bytes read
    alignas(64) uint64_t cap;
};

struct Mapping {
    void* base = nullptr;
    size_t size = 0;
    std::string name;
#ifdef _WIN32
    HANDLE h = nullptr;
#endif
};

static Mapping map_segment(const std::string& name, size_t size, bool create) {
    Mapping m;
    m.size = size;
    m.name = name;
#ifdef _WIN32
    std::string full = "Local\\" + name;
    if (create)
        m.h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                 (DWORD)(size >> 32), (DWORD)(size & 0xffffffff), full.c_str());
    else
        m.h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, full.c_str());
    if (!m.h) die("file mapping");
    m.base = MapViewOfFile(m.h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!m.base) die("MapViewOfFile");
#else
    std::string full = "/" + name;
    int fd = shm_open(full.c_str(), create ? (O_CREAT | O_EXCL | O_RDWR) : O_RDWR, 0600);
    if (fd < 0) die("shm_open");
    if (create && ftruncate(fd, (off_t)size) != 0) die("ftruncate");
    m.base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m.base == MAP_FAILED) die("mmap");
#endif
    return m;
}

static void unmap_segment(Mapping& m, bool owner) {
#ifdef _WIN32
    UnmapViewOfFile(m.base);
    CloseHandle(m.h);
    (void)owner;
#else
    munmap(m.base, m.size);
    if (owner) shm_unlink(("/" + m.name).c_str());
#endif
}

static void wait_spin(uint64_t& spins) {
    if (++spins < 2000) cpu_relax();
    else std::this_thread::yield();
}

struct ShmStream : Stream {
    RingHdr* tx;
    RingHdr* rx;
    char* tx_data;
    char* rx_data;
    ShmStream(void* txb, void* rxb)
        : tx((RingHdr*)txb), rx((RingHdr*)rxb),
          tx_data((char*)txb + sizeof(RingHdr)), rx_data((char*)rxb + sizeof(RingHdr)) {}
    void write_all(const void* p, size_t n) override {
        const char* c = static_cast<const char*>(p);
        uint64_t cap = tx->cap;
        uint64_t head = tx->head.load(std::memory_order_relaxed);
        while (n) {
            uint64_t spins = 0;
            uint64_t free_bytes;
            while ((free_bytes = cap - (head - tx->tail.load(std::memory_order_acquire))) == 0)
                wait_spin(spins);
            size_t k = (size_t)std::min<uint64_t>(free_bytes, n);
            size_t at = (size_t)(head % cap);
            size_t first = std::min(k, (size_t)(cap - at));
            std::memcpy(tx_data + at, c, first);
            if (k > first) std::memcpy(tx_data, c + first, k - first);
            head += k;
            tx->head.store(head, std::memory_order_release);
            c += k;
            n -= k;
        }
    }
    void read_all(void* p, size_t n) override {
        char* c = static_cast<char*>(p);
        uint64_t cap = rx->cap;
        uint64_t tail = rx->tail.load(std::memory_order_relaxed);
        while (n) {
            uint64_t spins = 0;
            uint64_t avail;
            while ((avail = rx->head.load(std::memory_order_acquire) - tail) == 0)
                wait_spin(spins);
            size_t k = (size_t)std::min<uint64_t>(avail, n);
            size_t at = (size_t)(tail % cap);
            size_t first = std::min(k, (size_t)(cap - at));
            std::memcpy(c, rx_data + at, first);
            if (k > first) std::memcpy(c + first, rx_data, k - first);
            tail += k;
            rx->tail.store(tail, std::memory_order_release);
            c += k;
            n -= k;
        }
    }
};

static std::string shm_base_name() {
#ifdef _WIN32
    return "jdbs_" + std::to_string(GetCurrentProcessId());
#else
    return "jdbs_" + std::to_string(getpid());
#endif
}

// ---------------------------------------------------------------- the measurements

struct Result {
    double lat_median_us = 0, lat_p99_us = 0;
    std::vector<double> mb_per_s;
};

static Result run_parent(Stream& s) {
    Result r;
    std::vector<char> buf(SIZES[3]);
    std::memset(buf.data(), 0x5a, buf.size());
    std::vector<double> rt;
    rt.reserve(LAT_ROUNDS);
    for (int i = 0; i < LAT_WARMUP + LAT_ROUNDS; ++i) {
        auto t0 = clk::now();
        s.write_all(buf.data(), LAT_SIZE);
        s.read_all(buf.data(), LAT_SIZE);
        auto t1 = clk::now();
        if (i >= LAT_WARMUP) rt.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::sort(rt.begin(), rt.end());
    r.lat_median_us = rt[rt.size() / 2];
    r.lat_p99_us = rt[rt.size() * 99 / 100];
    for (size_t sz : SIZES) {
        size_t count = THR_TOTAL / sz;
        char ack = 0;
        auto t0 = clk::now();
        for (size_t i = 0; i < count; ++i) s.write_all(buf.data(), sz);
        s.read_all(&ack, 1);
        auto t1 = clk::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        r.mb_per_s.push_back((double)(count * sz) / secs / (1024.0 * 1024.0));
    }
    return r;
}

static void run_child(Stream& s) {
    std::vector<char> buf(SIZES[3]);
    for (int i = 0; i < LAT_WARMUP + LAT_ROUNDS; ++i) {
        s.read_all(buf.data(), LAT_SIZE);
        s.write_all(buf.data(), LAT_SIZE);
    }
    for (size_t sz : SIZES) {
        size_t count = THR_TOTAL / sz;
        for (size_t i = 0; i < count; ++i) s.read_all(buf.data(), sz);
        char ack = 1;
        s.write_all(&ack, 1);
    }
}

// ---------------------------------------------------------------- processes

struct Child {
#ifdef _WIN32
    PROCESS_INFORMATION pi{};
#else
    pid_t pid = -1;
#endif
};

static Child spawn_child(const std::string& self, const std::string& kind, const std::string& addr) {
    Child c;
#ifdef _WIN32
    std::string cmd = "\"" + self + "\" child " + kind + " \"" + addr + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    std::vector<char> line(cmd.begin(), cmd.end());
    line.push_back(0);
    if (!CreateProcessA(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &c.pi))
        die("CreateProcess");
#else
    c.pid = fork();
    if (c.pid < 0) die("fork");
    if (c.pid == 0) {
        execl(self.c_str(), self.c_str(), "child", kind.c_str(), addr.c_str(), (char*)nullptr);
        _exit(127);
    }
#endif
    return c;
}

static int wait_child(Child& c) {
#ifdef _WIN32
    WaitForSingleObject(c.pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(c.pi.hProcess, &code);
    CloseHandle(c.pi.hProcess);
    CloseHandle(c.pi.hThread);
    return (int)code;
#else
    int st = 0;
    waitpid(c.pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
#endif
}

static std::string self_path(const char* argv0) {
#ifdef _WIN32
    (void)argv0;
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    return p;
#else
    char p[4096];
    ssize_t n = readlink("/proc/self/exe", p, sizeof(p) - 1);
    if (n > 0) { p[n] = 0; return p; }
    return argv0;
#endif
}

static Result measure(const std::string& self, const std::string& kind) {
    if (kind == "shm") {
        std::string base = shm_base_name();
        size_t seg = sizeof(RingHdr) + RING_CAP;
        Mapping a = map_segment(base + "_a", seg, true);
        Mapping b = map_segment(base + "_b", seg, true);
        for (Mapping* m : {&a, &b}) {
            RingHdr* h = new (m->base) RingHdr();
            h->head.store(0);
            h->tail.store(0);
            h->cap = RING_CAP;
        }
        Child c = spawn_child(self, kind, base);
        Result r;
        {
            ShmStream s(a.base, b.base);
            r = run_parent(s);
        }
        if (wait_child(c) != 0) die("child failed");
        unmap_segment(a, true);
        unmap_segment(b, true);
        return r;
    }
    std::string addr;
    sock_t l = listen_on(kind, addr);
    Child c = spawn_child(self, kind, addr);
    sock_t s = accept(l, nullptr, nullptr);
    if (s == BAD_SOCK) die("accept");
    sock_tune(s, kind == "tcp");
    CLOSESOCK(l);
    Result r;
    {
        SockStream st(s);
        r = run_parent(st);
    }
    if (wait_child(c) != 0) die("child failed");
    if (kind == "unix") unlink_path(addr);
    return r;
}

static int child_main(const std::string& kind, const std::string& addr) {
    if (kind == "shm") {
        size_t seg = sizeof(RingHdr) + RING_CAP;
        Mapping a = map_segment(addr + "_a", seg, false);
        Mapping b = map_segment(addr + "_b", seg, false);
        {
            ShmStream s(b.base, a.base);
            run_child(s);
        }
        unmap_segment(a, false);
        unmap_segment(b, false);
        return 0;
    }
    SockStream s(connect_to(kind, addr));
    run_child(s);
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    if (argc >= 4 && std::string(argv[1]) == "child") return child_main(argv[2], argv[3]);

    std::vector<std::string> kinds;
    for (int i = 1; i < argc; ++i) kinds.push_back(argv[i]);
    if (kinds.empty()) kinds = {"tcp", "unix", "shm"};

    std::string self = self_path(argv[0]);
    std::printf("%-5s %10s %10s %10s %10s %10s %10s\n", "", "rtt med", "rtt p99", "1K", "16K", "256K", "8M");
    std::printf("%-5s %10s %10s %10s %10s %10s %10s\n", "", "us", "us", "MB/s", "MB/s", "MB/s", "MB/s");
    for (const std::string& k : kinds) {
        Result r = measure(self, k);
        std::printf("%-5s %10.2f %10.2f", k.c_str(), r.lat_median_us, r.lat_p99_us);
        for (double v : r.mb_per_s) std::printf(" %10.0f", v);
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}
