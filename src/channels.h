#pragma once
#include "value.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

// jdBasic Channel - bounded MP/MC queue.
//
// Producer and consumer typically live in different ASYNC FUNCs (each ASYNC
// FUNC spawns a fresh std::thread + fresh VM in vm.cpp:1811). Channels live
// in a process-global registry keyed by an i64 handle. The handle survives
// the per-VM globals copy (it's just an integer), so workers can look up
// the same Channel by id regardless of which VM they belong to.
//
// SEND blocks the calling thread when the buffer is full; RECV blocks when
// empty. CLOSE wakes everyone - pending RECVs drain the remaining buffer
// and then start returning the EOF marker (a small `MAP { __chan_eof: TRUE }`
// - chosen so we don't need a new ValueType, see CHAN.IS_EOF).

struct Channel {
    std::mutex                mtx;
    std::condition_variable   cv_recv;     // wakes RECVers when buffer fills
    std::condition_variable   cv_send;     // wakes SENDers when slot frees
    std::deque<Value>         buffer;
    int64_t                   capacity = 0;  // 0 → unbuffered rendezvous
    std::atomic<bool>         closed{false};
    // Counts of threads currently parked in SEND/RECV. Lets CLOSE wake
    // everyone deterministically and lets the destructor refuse to drop a
    // Channel that still has live waiters (we keep it alive via shared_ptr,
    // but the assertion is useful in debug builds).
    int                       waiting_send = 0;
    int                       waiting_recv = 0;
};

// Registry - i64 handle → Channel. Process-global so cross-VM lookup works.
extern std::mutex                                       g_channels_mutex;
extern std::map<int64_t, std::shared_ptr<Channel>>      g_channels;
extern std::atomic<int64_t>                             g_channels_next_id;

// Construct an EOF marker map: { __chan_eof: TRUE }. CHAN.IS_EOF inspects
// the marker key; arbitrary user maps don't accidentally match.
Value chan_make_eof();
bool  chan_value_is_eof(const Value& v);

// Helpers used by the natives (and by the destructor / cleanup paths).
std::shared_ptr<Channel> chan_lookup(int64_t handle);
int64_t                  chan_register(std::shared_ptr<Channel> c);
void                     chan_unregister(int64_t handle);

// Close + wake every waiter. Idempotent.
void chan_close(Channel& ch);

// VM teardown: close every still-registered channel so threads that are
// parked on SEND/RECV come unstuck. Called from VM::~VM (best effort -
// other VMs may still hold references via the registry).
void chan_shutdown_all();
