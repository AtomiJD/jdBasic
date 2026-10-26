// exec_lock.h - which thread may run a VM's code right now.
//
// One thread at a time runs a VM's code and touches its globals. The
// program's own thread holds the lock while it runs, lets it go where it
// waits (SLEEP, AWAIT, a channel, HTTP.SERVER.WAIT) and, every few hundred
// instructions, hands it to threads that asked for it. An HTTP handler on a
// server thread takes the lock before it calls into the VM. So a handler
// never runs beside the program or beside another handler: no global is read
// or written by two threads at once.
#pragma once

#include <atomic>
#include <mutex>
#include <thread>

class ExecLock {
public:
    void acquire() {
        auto me = std::this_thread::get_id();
        if (owner_.load(std::memory_order_acquire) == me) { ++depth_; return; }
        waiting_.fetch_add(1, std::memory_order_relaxed);
        m_.lock();
        waiting_.fetch_sub(1, std::memory_order_relaxed);
        owner_.store(me, std::memory_order_release);
        depth_ = 1;
    }

    void release() {
        if (--depth_ == 0) {
            owner_.store(std::thread::id(), std::memory_order_release);
            m_.unlock();
        }
    }

    bool held_here() const {
        return owner_.load(std::memory_order_acquire) == std::this_thread::get_id();
    }

    // The program's own thread: the only one that lets the lock go for a
    // wait. A handler runs on top of the program's frames, so if it let go
    // the program would carry on beneath it.
    // Called by the thread that starts running the program's code from the
    // outside (not from within a handler, which already holds the lock).
    void make_home() { home_.store(std::this_thread::get_id(), std::memory_order_release); }
    bool at_home() const {
        return home_.load(std::memory_order_acquire) == std::this_thread::get_id();
    }

    // Lets go entirely when this thread holds the lock and is the program's,
    // for a wait; answers the depth yield_end puts back (0: nothing let go).
    int yield_begin() {
        if (!held_here() || !at_home()) return 0;
        int d = depth_;
        depth_ = 0;
        owner_.store(std::thread::id(), std::memory_order_release);
        m_.unlock();
        return d;
    }

    void yield_end(int d) {
        if (!d) return;
        m_.lock();
        owner_.store(std::this_thread::get_id(), std::memory_order_release);
        depth_ = d;
    }

    // Hands the lock to every thread waiting for it, then takes it back once
    // they are done. A plain unlock and lock would usually win the race
    // against the waiters.
    void let_waiters_in() {
        if (waiting_.load(std::memory_order_relaxed) == 0 || !held_here() || !at_home()) return;
        int d = yield_begin();
        while (waiting_.load(std::memory_order_relaxed) > 0) std::this_thread::yield();
        yield_end(d);
    }

private:
    std::mutex m_;
    std::atomic<std::thread::id> owner_{};
    std::atomic<std::thread::id> home_{};
    int depth_ = 0;                  // touched only by the owner
    std::atomic<int> waiting_{0};    // threads blocked in acquire()
};

// Holds the lock for a scope.
struct ExecHold {
    ExecLock& lock;
    explicit ExecHold(ExecLock& l) : lock(l) { lock.acquire(); }
    ~ExecHold() { lock.release(); }
    ExecHold(const ExecHold&) = delete;
    ExecHold& operator=(const ExecHold&) = delete;
};

// Lets the lock go for a wait, and takes it back after.
struct ExecYield {
    ExecLock* lock;
    int depth;
    explicit ExecYield(ExecLock* l) : lock(l), depth(l ? l->yield_begin() : 0) {}
    ~ExecYield() { if (lock) lock->yield_end(depth); }
    ExecYield(const ExecYield&) = delete;
    ExecYield& operator=(const ExecYield&) = delete;
};
