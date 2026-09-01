// Torture the Fruit Jam's PSRAM allocator on the desktop, where a wrong
// answer prints instead of wedging a board.
//
//   g++ -std=c++17 -O1 -fsanitize=address -DJDB_PSRAM_HEAP_TEST \
//       tests/psram_heap_test.cpp embedded/pico/fruitjam_psram_heap.cpp -o psram_test
//
// Every live block is filled with a byte derived from its index, so an
// allocator that hands the same bytes out twice shows up as a pattern
// that changed under a block that nobody wrote to.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <random>

#define WINDOW_BYTES (1u << 20)     // a megabyte is plenty to find overlap

static uint8_t* g_window = nullptr;

bool     psram_is_available(void) { return true; }
size_t   psram_get_size(void)     { return WINDOW_BYTES; }
uint8_t* jdb_psram_window(void)   { return g_window; }
unsigned fruitjam_psram_reserved(void) { return 0; }   // no scanout here

void* jdb_psram_test_alloc(size_t n);
void  jdb_psram_test_free(void* p);
extern "C" unsigned fruitjam_psram_heap_free(void);
extern "C" unsigned fruitjam_psram_heap_largest(void);

struct Live {
    uint8_t* p;
    size_t   n;
    uint8_t  fill;
};

static int g_fail = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        g_fail++;
    }
}

// Does this block overlap any other live one?
static bool overlaps(const std::vector<Live>& live, size_t self) {
    const Live& a = live[self];
    for (size_t i = 0; i < live.size(); i++) {
        if (i == self) continue;
        const Live& b = live[i];
        if (a.p < b.p + b.n && b.p < a.p + a.n) return true;
    }
    return false;
}

int main() {
    g_window = (uint8_t*)malloc(WINDOW_BYTES);
    if (!g_window) { printf("FAIL: no window\n"); return 1; }
    memset(g_window, 0xCC, WINDOW_BYTES);

    printf("free at start: %u of %u\n", fruitjam_psram_heap_free(), WINDOW_BYTES);
    check(fruitjam_psram_heap_free() > WINDOW_BYTES - 64, "the whole window is free at the start");

    std::mt19937 rng(1234);
    std::vector<Live> live;
    size_t handed_out = 0, refused = 0;

    for (int round = 0; round < 200000; round++) {
        bool grow = live.empty() || (rng() % 100) < 55;
        if (grow) {
            // Sizes across the range the interpreter actually asks for.
            size_t n;
            switch (rng() % 4) {
                case 0:  n = 8 + rng() % 500; break;
                case 1:  n = 512 + rng() % 2048; break;
                case 2:  n = 4096 + rng() % 16384; break;
                default: n = 1 + rng() % 64; break;
            }
            uint8_t* p = (uint8_t*)jdb_psram_test_alloc(n);
            if (!p) { refused++; continue; }
            handed_out++;
            Live l{ p, n, (uint8_t)(rng() & 0xFF) };
            memset(p, l.fill, n);
            live.push_back(l);
            check(!overlaps(live, live.size() - 1), "a fresh block overlaps a live one");
            if (g_fail) break;
        } else {
            size_t idx = rng() % live.size();
            // What was written must still be there.
            for (size_t k = 0; k < live[idx].n; k++) {
                if (live[idx].p[k] != live[idx].fill) {
                    printf("FAIL: block of %zu bytes changed at offset %zu\n", live[idx].n, k);
                    g_fail++;
                    break;
                }
            }
            if (g_fail) break;
            jdb_psram_test_free(live[idx].p);
            live[idx] = live.back();
            live.pop_back();
        }
    }

    printf("handed out %zu, refused %zu, still live %zu\n", handed_out, refused, live.size());

    // Everything back, and the pool whole again.
    for (auto& l : live) jdb_psram_test_free(l.p);
    live.clear();
    unsigned back = fruitjam_psram_heap_free();
    unsigned biggest = fruitjam_psram_heap_largest();
    printf("free at end: %u, largest %u\n", back, biggest);
    check(back > WINDOW_BYTES - 64, "everything came back");
    check(biggest > WINDOW_BYTES - 64, "the pool coalesced back into one block");

    printf(g_fail ? "RESULTS: %d failed\n" : "RESULTS: all passed (%d failed)\n", g_fail);
    return g_fail ? 1 : 0;
}
