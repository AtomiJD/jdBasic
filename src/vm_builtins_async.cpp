// VM builtins: AWAIT, THREAD.*, CHAN.* and streamed file reading.

#include "vm_internal.h"

void VM::register_async_builtins() {
    // ── Async / Thread ────────────────────────────────────────

    register_native("AWAIT", [](const std::vector<Value>& args) -> Value {
        int task_id = (int)args[0].to_int();
        std::shared_ptr<AsyncTask> task;
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          auto it = g_async_tasks.find(task_id);
          if (it == g_async_tasks.end()) throw std::runtime_error("Invalid async task ID: " + std::to_string(task_id));
          task = it->second; }
        // Busy-wait with small sleep to stay responsive
        while (!task->done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Value result = task->result;
        // Cleanup
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          g_async_tasks.erase(task_id); }
        return result;
    });

#ifndef JDB_LEAN
    register_native("THREAD.ISDONE", [](const std::vector<Value>& args) -> Value {
        int task_id = (int)args[0].to_int();
        std::lock_guard<std::mutex> lock(g_async_mutex);
        auto it = g_async_tasks.find(task_id);
        if (it == g_async_tasks.end()) return Value::make_bool(true);
        return Value::make_bool(it->second->done.load());
    });
#endif

#ifndef JDB_LEAN
    register_native("THREAD.GETRESULT", [](const std::vector<Value>& args) -> Value {
        int task_id = (int)args[0].to_int();
        std::shared_ptr<AsyncTask> task;
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          auto it = g_async_tasks.find(task_id);
          if (it == g_async_tasks.end()) throw std::runtime_error("Invalid task ID");
          task = it->second; }
        while (!task->done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Value result = task->result;
        { std::lock_guard<std::mutex> lock(g_async_mutex);
          g_async_tasks.erase(task_id); }
        return result;
    });
#endif

    // ── Channels ──────────────────────────────────────────────
    //
    // Bounded MP/MC queue between ASYNC tasks (each ASYNC FUNC = its own
    // OS thread + VM, see CALL dispatch above). Channels live in a global
    // registry indexed by an i64 handle; the handle survives the per-VM
    // globals copy that happens when ASYNC spawns, so worker tasks can
    // look up the same Channel as the spawner.

    // CHAN.OPEN(capacity) → handle. Capacity 0 → unbuffered rendezvous.
    register_native("CHAN.OPEN", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t cap = args[0].to_int();
        if (cap < 0) {
            throw std::runtime_error("CHAN.OPEN: capacity must be >= 0");
        }
        auto ch = std::make_shared<Channel>();
        ch->capacity = cap;
        int64_t id = chan_register(ch);
        return Value::make_i64(id);
    });

    // CHAN.SEND(ch, value) - blocks the calling thread when buffer is full.
    // Throws on a closed channel.
    register_native("CHAN.SEND", 2, 2, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) {
            throw std::runtime_error("CHAN.SEND: invalid channel handle " + std::to_string(handle));
        }
        Value v = args[1];

        std::unique_lock<std::mutex> lock(ch->mtx);
        if (ch->capacity == 0) {
            // Unbuffered rendezvous: park until a RECV is waiting, then
            // hand the value through and wake exactly one receiver.
            ++ch->waiting_send;
            ch->buffer.push_back(std::move(v));
            ch->cv_recv.notify_one();
            // Wait for the receiver to drain our slot or the channel to close.
            ch->cv_send.wait(lock, [&]() {
                return ch->buffer.empty() || ch->closed.load();
            });
            --ch->waiting_send;
            if (ch->closed.load() && !ch->buffer.empty()) {
                // RECV never came + we got closed mid-flight → drop the
                // value and report.
                ch->buffer.pop_back();
                throw std::runtime_error("CHAN.SEND: channel closed before delivery");
            }
            return Value::make_none();
        }

        // Bounded buffer: wait until there's room or we're closed.
        ++ch->waiting_send;
        ch->cv_send.wait(lock, [&]() {
            return ch->buffer.size() < (size_t)ch->capacity || ch->closed.load();
        });
        --ch->waiting_send;
        if (ch->closed.load()) {
            throw std::runtime_error("CHAN.SEND: channel is closed");
        }
        ch->buffer.push_back(std::move(v));
        ch->cv_recv.notify_one();
        return Value::make_none();
    });

    // CHAN.RECV(ch, [timeout_ms]) → value. Blocks while empty, at most
    // timeout_ms when given, then returns the timeout marker. Returns the
    // EOF marker when the channel is closed AND the buffer is drained.
    register_native("CHAN.RECV", 1, 2, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) {
            throw std::runtime_error("CHAN.RECV: invalid channel handle " + std::to_string(handle));
        }
        int64_t timeout_ms = args.size() >= 2 ? args[1].to_int() : -1;
        std::unique_lock<std::mutex> lock(ch->mtx);
        ++ch->waiting_recv;
        auto ready = [&]() {
            return !ch->buffer.empty() || ch->closed.load();
        };
        if (timeout_ms < 0) {
            ch->cv_recv.wait(lock, ready);
        } else if (!ch->cv_recv.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready)) {
            --ch->waiting_recv;
            return chan_make_timeout();
        }
        --ch->waiting_recv;
        if (ch->buffer.empty()) {
            // Drained + closed → EOF.
            return chan_make_eof();
        }
        Value out = std::move(ch->buffer.front());
        ch->buffer.pop_front();
        // For unbuffered rendezvous, the SENDer is parked on cv_send
        // waiting for buffer.empty(); wake them now that we've taken
        // their slot. Buffered case wakes a sender slot too.
        if (ch->capacity == 0) ch->cv_send.notify_one();
        else                    ch->cv_send.notify_one();
        return out;
    });

    // CHAN.CLOSE(ch) - wake everyone, idempotent.
    register_native("CHAN.CLOSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) {
            throw std::runtime_error("CHAN.CLOSE: invalid channel handle " + std::to_string(handle));
        }
        chan_close(*ch);
        return Value::make_none();
    });

    // CHAN.IS_EOF(value) → BOOLEAN. Inspects the marker map returned by
    // RECV on a closed+empty channel.
    register_native("CHAN.IS_EOF", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(chan_value_is_eof(args[0]));
    });

    // CHAN.TRY_RECV(ch) → a waiting value, the EOF marker on a closed and
    // drained channel, or the timeout marker. Never blocks.
    register_native("CHAN.TRY_RECV", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) {
            throw std::runtime_error("CHAN.TRY_RECV: invalid channel handle " + std::to_string(handle));
        }
        std::lock_guard<std::mutex> lock(ch->mtx);
        if (ch->buffer.empty()) return ch->closed.load() ? chan_make_eof() : chan_make_timeout();
        Value out = std::move(ch->buffer.front());
        ch->buffer.pop_front();
        ch->cv_send.notify_one();
        return out;
    });

    // CHAN.SELECT(channels, [timeout_ms]) → the index of the first channel
    // with a value waiting or closed, -1 when none is ready in time. A
    // negative or missing timeout waits for as long as it takes.
    register_native("CHAN.SELECT", 1, 2, [](const std::vector<Value>& args) -> Value {
        auto* arr = args[0].as_array();
        if (!arr || arr->elements.empty())
            throw std::runtime_error("CHAN.SELECT: expects a non-empty array of channel handles");
        std::vector<std::shared_ptr<Channel>> chans;
        chans.reserve(arr->elements.size());
        for (auto& e : arr->elements) {
            int64_t handle = e.to_int();
            auto ch = chan_lookup(handle);
            if (!ch) throw std::runtime_error("CHAN.SELECT: invalid channel handle " + std::to_string(handle));
            chans.push_back(ch);
        }
        int64_t timeout_ms = args.size() >= 2 ? args[1].to_int() : -1;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
        for (;;) {
            for (size_t i = 0; i < chans.size(); i++) {
                std::lock_guard<std::mutex> lock(chans[i]->mtx);
                if (!chans[i]->buffer.empty() || chans[i]->closed.load())
                    return Value::make_i64((int64_t)i);
            }
            if (timeout_ms >= 0 && std::chrono::steady_clock::now() >= deadline)
                return Value::make_i64(-1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // CHAN.IS_TIMEOUT(value) → BOOLEAN for the marker a timed RECV,
    // TRY_RECV answer when no value came.
    register_native("CHAN.IS_TIMEOUT", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(chan_value_is_timeout(args[0]));
    });

    // CHAN.IS_CLOSED(ch) → BOOLEAN.
    register_native("CHAN.IS_CLOSED", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) return Value::make_bool(true); // gone = effectively closed
        return Value::make_bool(ch->closed.load());
    });

    // CHAN.LEN(ch) → INTEGER. Current buffer depth (0 for unbuffered).
    register_native("CHAN.LEN", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) return Value::make_i64(0);
        std::lock_guard<std::mutex> lock(ch->mtx);
        return Value::make_i64((int64_t)ch->buffer.size());
    });

    // CHAN.CAP(ch) → INTEGER. Capacity for diagnostics.
    register_native("CHAN.CAP", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto ch = chan_lookup(handle);
        if (!ch) return Value::make_i64(0);
        return Value::make_i64(ch->capacity);
    });

    // ── FILE streaming handles ────────────────────────────────
    //
    // Line-by-line reader plus tail-follow mode. Mirror of the channel
    // registry - process-global, indexed by an i64 handle so producer
    // and consumer ASYNC FUNCs (separate VMs) hit the same FileHandle.

    // FILE.OPEN_LINES(path$) → handle. Reads UTF-8 text, line by line.
    // Throws if the file cannot be opened.
    register_native("FILE.OPEN_LINES", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
        auto fh = std::make_shared<FileHandle>();
        fh->path = path;
        fh->tail_mode = false;
        fh->stream.open(path, std::ios::in | std::ios::binary);
        if (!fh->stream.is_open()) {
            throw std::runtime_error("FILE.OPEN_LINES: cannot open '" + path + "'");
        }
        return Value::make_i64(file_register(fh));
    });

    // FILE.OPEN_TAIL(path$) → handle. Same as OPEN_LINES but READLINE$
    // blocks polling for newly-appended data instead of returning EOF.
    register_native("FILE.OPEN_TAIL", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].as_string()->data;
        auto fh = std::make_shared<FileHandle>();
        fh->path = path;
        fh->tail_mode = true;
        fh->stream.open(path, std::ios::in | std::ios::binary);
        if (!fh->stream.is_open()) {
            throw std::runtime_error("FILE.OPEN_TAIL: cannot open '" + path + "'");
        }
        return Value::make_i64(file_register(fh));
    });

    // FILE.READLINE$(handle) → STRING. Trailing \n / \r\n stripped.
    // Tail-mode: blocks until data arrives or the handle is closed.
    register_native("FILE.READLINE$", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto fh = file_lookup(handle);
        if (!fh) {
            throw std::runtime_error("FILE.READLINE$: invalid file handle " + std::to_string(handle));
        }
        return Value::make_string(file_readline(*fh));
    });

    // FILE.AT_EOF(handle) → BOOLEAN.
    register_native("FILE.AT_EOF", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        auto fh = file_lookup(handle);
        if (!fh) return Value::make_bool(true);
        return Value::make_bool(file_at_eof(*fh));
    });

    // FILE.CLOSE(handle) - idempotent. Wakes any tail reader parked in
    // its poll loop within ~poll_ms (default 50ms).
    register_native("FILE.CLOSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int64_t handle = args[0].to_int();
        file_unregister(handle);
        return Value::make_none();
    });

    // ── FILE → CHAN sugar ─────────────────────────────────────
    //
    // FILE.STREAM_LINES(path$, ch [, capacity]) and FILE.STREAM_TAIL(...)
    // wrap the OPEN_LINES / OPEN_TAIL + read-loop pattern as a single
    // call: spawn a producer thread, push each line into the given
    // channel, close the channel when the file is exhausted (or when
    // the consumer closes it from outside, which cancels the read).
    //
    // The user supplies the channel so they own its lifetime. Pass an
    // already-open channel - capacity decides backpressure.
    auto file_pump_lines = [](int64_t ch_handle, std::string path, bool tail) {
        auto ch = chan_lookup(ch_handle);
        if (!ch) return; // gone already
        auto fh = std::make_shared<FileHandle>();
        fh->path = path;
        fh->tail_mode = tail;
        fh->stream.open(path, std::ios::in | std::ios::binary);
        if (!fh->stream.is_open()) {
            chan_close(*ch);
            return;
        }
        int64_t f_handle = file_register(fh);
        std::thread([ch, fh, f_handle]() {
            // We can't use file_readline() here because its poll loop
            // only watches fh->closed; consumer closes via the *channel*
            // (ch->closed) and would otherwise wait the full poll cycle
            // before noticing. Inline the poll so we can watch BOTH
            // flags every tick.
            const auto poll = std::chrono::milliseconds(50);
            try {
                while (!ch->closed.load() && !fh->closed.load()) {
                    std::string line;
                    bool got = false;
                    {
                        std::lock_guard<std::mutex> lock(fh->mtx);
                        if (!fh->stream.is_open()) break;
                        if (std::getline(fh->stream, line)) {
                            got = true;
                        } else {
                            if (!fh->tail_mode) break;     // EOF in finite mode
                            fh->stream.clear();             // re-arm in tail mode
                        }
                    }
                    if (!got) {
                        std::this_thread::sleep_for(poll);
                        continue;
                    }
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    std::unique_lock<std::mutex> lock(ch->mtx);
                    ch->cv_send.wait(lock, [&]() {
                        return (ch->capacity == 0) ||
                               (ch->buffer.size() < (size_t)ch->capacity) ||
                                ch->closed.load();
                    });
                    if (ch->closed.load()) break;
                    ch->buffer.push_back(Value::make_string(line));
                    ch->cv_recv.notify_one();
                    if (ch->capacity == 0) {
                        ch->cv_send.wait(lock, [&]() {
                            return ch->buffer.empty() || ch->closed.load();
                        });
                        if (ch->closed.load()) break;
                    }
                }
            } catch (...) {
                // swallow - close below
            }
            file_unregister(f_handle);
            chan_close(*ch);
        }).detach();
    };

    register_native("FILE.STREAM_LINES", 2, 3,
        [file_pump_lines](const std::vector<Value>& args) -> Value {
            std::string path = args[0].as_string()->data;
            int64_t ch_handle = args[1].to_int();
            // capacity arg is informational here - the channel was opened
            // by the caller with whatever capacity they chose.
            (void)args; // silence unused-warning
            file_pump_lines(ch_handle, path, /*tail=*/false);
            return Value::make_i64(ch_handle);
        });

    register_native("FILE.STREAM_TAIL", 2, 3,
        [file_pump_lines](const std::vector<Value>& args) -> Value {
            std::string path = args[0].as_string()->data;
            int64_t ch_handle = args[1].to_int();
            (void)args;
            file_pump_lines(ch_handle, path, /*tail=*/true);
            return Value::make_i64(ch_handle);
        });
}
