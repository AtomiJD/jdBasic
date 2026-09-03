// A heap in the 8 MB the Fruit Jam has on QMI chip select 1.
//
// The SDK maps the window and stops there: malloc still hands out SRAM,
// of which this board has about 40 KB free and rarely 20 KB in one
// piece, so loading anything but a toy program fails on a single
// contiguous request. What the interpreter asks for in large pieces is
// asked for through operator new - every vector, string and map it owns
// - so that is the hook, and no wrap around the C allocator is needed.
//
// Small requests stay in SRAM. They are the hot ones, they fit, and
// PSRAM is reached through the XIP cache where a miss costs.
//
// First fit with coalescing, which is enough: the interpreter's large
// blocks are few and long-lived, and the alternative is a size-class
// allocator for a load that does not need one.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <new>

#ifdef JDB_PSRAM_HEAP_TEST
// The allocator is plain arithmetic over a window, so the test gives it
// an ordinary buffer and hammers it on the desktop.
extern bool     psram_is_available(void);
extern size_t   psram_get_size(void);
extern uint8_t* jdb_psram_window(void);
extern unsigned fruitjam_psram_reserved(void);
#define PSRAM_WINDOW ((uintptr_t)jdb_psram_window())
#else
#include "pico/stdlib.h"
#include "hardware/psram.h"
extern "C" unsigned fruitjam_psram_reserved(void);
#define PSRAM_WINDOW 0x11000000u
#endif

// Nothing goes to PSRAM by size. It goes there by *scope*: the caller
// marks the stretch of work whose allocations are transient - lexing and
// parsing, where the tokens and the syntax tree are built, read once by
// the compiler and thrown away - and everything asked for inside it is
// served from the pool. The runtime's own structures, which are asked
// for outside any scope, stay in SRAM where they are fast.
//
// This is the safe half of what was tried by size earlier: the value
// stack, the chunk and the globals never move, so nothing with a
// deadline ever waits on the memory controller.
static int g_scope = 0;

namespace {

struct Block {
    size_t  size;       // payload bytes, not counting this header
    Block*  next;
    uint32_t used;
    uint32_t guard;     // marks this as ours when free() sees the pointer
};

const uint32_t GUARD = 0x50535246u;   // "PSRF"

Block*    g_first = nullptr;
uintptr_t g_base = 0;
size_t    g_bytes = 0;
size_t    g_in_use = 0;
size_t    g_peak = 0;

inline size_t align8(size_t n) { return (n + 7u) & ~(size_t)7u; }

void heap_start() {
    if (g_first || !psram_is_available()) return;
    // The scanout has the bottom of the window; the pool starts above
    // whatever it reserved.
    size_t off = fruitjam_psram_reserved();
    size_t n = psram_get_size();
    if (n < off + 64 * 1024) return;
    g_base = PSRAM_WINDOW + off;
    g_bytes = n - off;
    g_first = (Block*)g_base;
    g_first->size = g_bytes - sizeof(Block);
    g_first->next = nullptr;
    g_first->used = 0;
    g_first->guard = GUARD;
}

void* psram_alloc(size_t want) {
    heap_start();
    if (!g_first) return nullptr;
    want = align8(want);
    for (Block* b = g_first; b; b = b->next) {
        if (b->used || b->size < want) continue;
        // Split when the tail is worth keeping.
        if (b->size >= want + sizeof(Block) + 64) {
            Block* tail = (Block*)((uint8_t*)(b + 1) + want);
            tail->size = b->size - want - sizeof(Block);
            tail->next = b->next;
            tail->used = 0;
            tail->guard = GUARD;
            b->size = want;
            b->next = tail;
        }
        b->used = 1;
        g_in_use += b->size;
        if (g_in_use > g_peak) g_peak = g_in_use;
        return b + 1;
    }
    return nullptr;
}

bool is_ours(void* p) {
    uintptr_t a = (uintptr_t)p;
    return g_first && a > g_base && a < g_base + g_bytes;
}

void psram_free(void* p) {
    Block* b = (Block*)p - 1;
    if (b->guard != GUARD || !b->used) return;
    b->used = 0;
    g_in_use -= b->size;
    // Join free neighbours, walking from the head so a block freed
    // before its predecessor still merges with it. The list holds a
    // handful of large entries, so the walk is cheap.
    for (Block* s = g_first; s; s = s->next)
        while (!s->used && s->next && !s->next->used) {
            s->size += sizeof(Block) + s->next->size;
            s->next = s->next->next;
        }
}

} // namespace

// The desktop test in tests/psram_heap_test.cpp proves the arithmetic.
// This runs the same shape on the board, where the DVI DMA is reading a
// framebuffer and core 1 is fetching USB frames out of flash, because
// that is the part a desktop cannot reproduce.
extern "C" int fruitjam_psram_torture(char* out, int cap, int rounds) {
    heap_start();
    if (!g_first) return snprintf(out, cap, "no psram");
    void* live[24];
    size_t sizes[24];
    unsigned char fills[24];
    for (int i = 0; i < 24; i++) live[i] = nullptr;
    uint32_t seed = 12345;
    int bad = 0, done = 0;
    for (int r = 0; r < rounds && !bad; r++) {
        seed = seed * 1103515245u + 12345u;
        int slot = (seed >> 16) % 24;
        if (live[slot]) {
            unsigned char* p = (unsigned char*)live[slot];
            for (size_t k = 0; k < sizes[slot]; k++)
                if (p[k] != fills[slot]) { bad = 1; break; }
            psram_free(live[slot]);
            live[slot] = nullptr;
        } else {
            size_t n = 64 + ((seed >> 8) % 20000);
            void* p = psram_alloc(n);
            if (!p) continue;
            live[slot] = p;
            sizes[slot] = n;
            fills[slot] = (unsigned char)(seed & 0xFF);
            memset(p, fills[slot], n);
            done++;
        }
    }
    for (int i = 0; i < 24; i++) if (live[i]) psram_free(live[i]);
    return snprintf(out, cap, "%d blocks, %s", done, bad ? "CORRUPT" : "intact");
}

// Nestable, because a module IMPORT lexes and parses inside a parse.
extern "C" void jdb_transient_begin(void) { g_scope++; }
extern "C" void jdb_transient_end(void)   { if (g_scope > 0) g_scope--; }

// Bytes in use in the pool right now, and the most that were in use
// since the last reset of the mark.
extern "C" unsigned fruitjam_psram_in_use(void) { return (unsigned)g_in_use; }
extern "C" unsigned fruitjam_psram_peak(int reset) {
    unsigned p = (unsigned)g_peak;
    if (reset) g_peak = g_in_use;
    return p;
}

extern "C" unsigned fruitjam_psram_heap_free(void) {
    heap_start();
    if (!g_first) return 0;
    size_t free_bytes = 0;
    for (Block* b = g_first; b; b = b->next)
        if (!b->used) free_bytes += b->size;
    return (unsigned)free_bytes;
}

extern "C" unsigned fruitjam_psram_heap_largest(void) {
    heap_start();
    if (!g_first) return 0;
    size_t big = 0;
    for (Block* b = g_first; b; b = b->next)
        if (!b->used && b->size > big) big = b->size;
    return (unsigned)big;
}

#ifdef JDB_PSRAM_HEAP_TEST
// The test drives the pool directly; replacing operator new here would
// take the harness's own allocations with it.
void* jdb_psram_test_alloc(size_t n) { return psram_alloc(n); }
void  jdb_psram_test_free(void* p)   { if (p) psram_free(p); }
#else
static void* jdb_alloc(size_t n) {
    if (n == 0) n = 1;
    if (g_scope > 0) {
        void* p = psram_alloc(n);
        if (p) return p;
    }
    void* p = malloc(n);
    if (p) return p;
    // SRAM said no; the big pool is the last chance either way.
    return psram_alloc(n);
}

static void jdb_release(void* p) {
    if (!p) return;
    if (is_ours(p)) psram_free(p);
    else free(p);
}

void* operator new(size_t n) {
    void* p = jdb_alloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n) {
    void* p = jdb_alloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(size_t n, const std::nothrow_t&) noexcept { return jdb_alloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return jdb_alloc(n); }

void operator delete(void* p) noexcept { jdb_release(p); }
void operator delete[](void* p) noexcept { jdb_release(p); }
void operator delete(void* p, size_t) noexcept { jdb_release(p); }
void operator delete[](void* p, size_t) noexcept { jdb_release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { jdb_release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { jdb_release(p); }
#endif // JDB_PSRAM_HEAP_TEST
