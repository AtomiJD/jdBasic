// jdb_stack.h - how much call stack the current thread has left.
//
// Recursive walks over user input (the parser, the bytecode compiler, the
// native code generator, the JSON reader, nested calls through builtins)
// ask jdb_stack::low() before going one level deeper and report an error
// instead of running off the end of the stack. The answer comes from the
// thread's real stack bounds, so it holds for the main thread, worker
// threads and a host application's threads alike.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall
GetCurrentThreadStackLimits(unsigned long long* low_limit, unsigned long long* high_limit);
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#endif

namespace jdb_stack {

struct Bounds {
    uintptr_t low = 0;    // lowest usable address (the stack grows down to it)
    size_t margin = 0;    // what is kept free below the last checked frame
};

inline Bounds query_bounds() {
    Bounds b;
    size_t size = 0;
#if defined(_WIN32)
    unsigned long long lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    b.low = (uintptr_t)lo;
    size = (size_t)(hi - lo);
#elif defined(__APPLE__)
    pthread_t self = pthread_self();
    uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(self);
    size = pthread_get_stacksize_np(self);
    b.low = top - size;
#elif defined(__linux__)
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void* addr = nullptr;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0)
            b.low = (uintptr_t)addr;
        pthread_attr_destroy(&attr);
    }
#endif
    // The frames below the last check (library calls, the error path itself)
    // need room too: an eighth of the stack, between 128 KB and 1 MB.
    size_t m = size / 8;
    if (m < 128 * 1024) m = 128 * 1024;
    if (m > 1024 * 1024) m = 1024 * 1024;
    b.margin = m;
    return b;
}

// True when the current frame is within the safety margin of the end of
// this thread's stack. Unknown bounds answer false.
inline bool low() {
    static thread_local Bounds bounds = query_bounds();
    if (bounds.low == 0) return false;
    char here;
    uintptr_t sp = (uintptr_t)&here;
    return sp < bounds.low + bounds.margin;
}

} // namespace jdb_stack
