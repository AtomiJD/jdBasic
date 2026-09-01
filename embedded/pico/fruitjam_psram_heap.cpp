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
#include <string.h>
#include <new>

#include "pico/stdlib.h"
#include "hardware/psram.h"

#define PSRAM_BASE 0x11000000u

// Below this a request is better off in SRAM.
#define PSRAM_MIN 512u

namespace {

struct Block {
    size_t  size;       // payload bytes, not counting this header
    Block*  next;
    uint32_t used;
    uint32_t guard;     // marks this as ours when free() sees the pointer
};

const uint32_t GUARD = 0x50535246u;   // "PSRF"

Block* g_first = nullptr;
size_t g_bytes = 0;
size_t g_in_use = 0;

inline size_t align8(size_t n) { return (n + 7u) & ~(size_t)7u; }

void heap_start() {
    if (g_first || !psram_is_available()) return;
    size_t n = psram_get_size();
    if (n < 64 * 1024) return;
    g_bytes = n;
    g_first = (Block*)PSRAM_BASE;
    g_first->size = n - sizeof(Block);
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
        return b + 1;
    }
    return nullptr;
}

bool is_ours(void* p) {
    uintptr_t a = (uintptr_t)p;
    return g_first && a > PSRAM_BASE && a < PSRAM_BASE + g_bytes;
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

static void* jdb_alloc(size_t n) {
    if (n == 0) n = 1;
    if (n >= PSRAM_MIN) {
        void* p = psram_alloc(n);
        if (p) return p;
    }
    void* p = malloc(n);
    if (p) return p;
    // SRAM said no; the big pool is the last chance whatever the size.
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
