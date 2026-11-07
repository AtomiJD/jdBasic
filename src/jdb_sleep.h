// jdb_sleep.h - SLEEP to the millisecond, shared by the VM and the native
// runtime.
//
// Windows wakes a plain Sleep() on its system tick, about 15.6 ms unless some
// program raised the timer resolution, so SLEEP 1 waited 15 ms and SLEEP 16
// about 27. A high-resolution waitable timer (Windows 10 1803 and later) wakes
// within about half a millisecond without changing the tick for the whole
// system the way timeBeginPeriod does. Each thread keeps one timer. Older
// Windows falls back to Sleep(). POSIX sleeps are already that fine.
#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif

inline void jdb_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    struct TimerHolder {
        HANDLE h = nullptr;
        bool tried = false;
        ~TimerHolder() { if (h) CloseHandle(h); }
    };
    static thread_local TimerHolder timer;
    if (!timer.tried) {
        timer.tried = true;
        timer.h = CreateWaitableTimerExW(nullptr, nullptr,
                                         0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                         TIMER_ALL_ACCESS);
    }
    if (timer.h) {
        LARGE_INTEGER due;
        due.QuadPart = -ms * 10000;  // relative, in 100 ns units
        if (SetWaitableTimer(timer.h, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer.h, INFINITE);
            return;
        }
    }
    Sleep((DWORD)ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}
