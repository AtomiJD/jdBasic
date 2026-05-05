#include "channels.h"
#include "value.h"

#include <utility>

std::mutex                                       g_channels_mutex;
std::map<int64_t, std::shared_ptr<Channel>>      g_channels;
std::atomic<int64_t>                             g_channels_next_id{1};

// ── EOF marker ──────────────────────────────────────────────────────
//
// We deliberately don't add a new ValueType for EOF — that would ripple
// through FORMAT$, TYPEOF, the Value comparator, and the codegen tag
// table. Instead we mint a tiny MAP with a sentinel key. Phase 2 can
// promote this to a real type if it becomes a hotspot.
static const char* kChanEofKey = "__chan_eof__";

Value chan_make_eof() {
    Value m = Value::make_object();
    m.as_object()->set(kChanEofKey, Value::make_bool(true));
    return m;
}

bool chan_value_is_eof(const Value& v) {
    if (v.type != ValueType::OBJECT) return false;
    auto* m = v.as_object();
    if (!m) return false;
    Value* slot = m->get(kChanEofKey);
    return slot && slot->to_bool();
}

// ── Registry ────────────────────────────────────────────────────────

std::shared_ptr<Channel> chan_lookup(int64_t handle) {
    std::lock_guard<std::mutex> lock(g_channels_mutex);
    auto it = g_channels.find(handle);
    if (it == g_channels.end()) return nullptr;
    return it->second;
}

int64_t chan_register(std::shared_ptr<Channel> c) {
    int64_t id = g_channels_next_id++;
    std::lock_guard<std::mutex> lock(g_channels_mutex);
    g_channels[id] = std::move(c);
    return id;
}

void chan_unregister(int64_t handle) {
    std::shared_ptr<Channel> ch;
    {
        std::lock_guard<std::mutex> lock(g_channels_mutex);
        auto it = g_channels.find(handle);
        if (it == g_channels.end()) return;
        ch = std::move(it->second);
        g_channels.erase(it);
    }
    // Wake any parked threads so they don't leak forever. The shared_ptr
    // they hold keeps the Channel alive past unregistration.
    if (ch) chan_close(*ch);
}

void chan_close(Channel& ch) {
    {
        std::lock_guard<std::mutex> lock(ch.mtx);
        if (ch.closed.load()) return;
        ch.closed.store(true);
    }
    // Wake everyone — RECVers drain the rest of the buffer then see EOF;
    // SENDers learn the channel is dead and throw.
    ch.cv_recv.notify_all();
    ch.cv_send.notify_all();
}

void chan_shutdown_all() {
    // Close every channel; we keep the registry around so still-running
    // threads holding the shared_ptr don't crash on dereference.
    std::lock_guard<std::mutex> lock(g_channels_mutex);
    for (auto& [id, c] : g_channels) {
        if (c) chan_close(*c);
    }
}
