#include "midi.h"
#include "vm.h"
#include "value.h"
#include "errors.h"

#ifdef MIDI

#include "RtMidi.h"
#include "sound.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cmath>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {

// An input port collects its messages for MIDI.POLL from RtMidi's callback
// thread; a routed port also hands each one to the synth right away.
struct MidiPort {
    std::mutex qmtx;
    std::mutex omtx;                  // one sender at a time on `out`
    std::deque<std::vector<unsigned char>> queue;
    std::atomic<int> routed{0};
    std::atomic<int> to_channel{0};   // 1-16: routed messages move to this channel
    std::unique_ptr<RtMidiOut> out;
    std::unique_ptr<RtMidiIn>  in;

    // clock out: a thread sends 24 ticks per beat in step with the sequencer
    std::thread clock_thr;
    std::atomic<bool> clock_run{false};
    std::atomic<bool> clock_start{false};   // Start goes out on the next bar line

    // clock in, measured in the input callback (under qmtx)
    int64_t clk_last_ns = 0;
    double  clk_avg_ns = 0.0;
    int64_t clk_ticks = 0;
    bool    clk_running = false;
    int64_t clk_seen_ns = 0;

    ~MidiPort() {
        clock_run.store(false);
        if (clock_thr.joinable()) clock_thr.join();
    }
};

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void send_bytes(MidiPort& p, std::initializer_list<unsigned char> bytes) {
    std::vector<unsigned char> msg(bytes);
    std::lock_guard<std::mutex> lk(p.omtx);
    try { p.out->sendMessage(&msg); } catch (...) {}
}

// Waits until steady-clock time `until_ns`. On Windows a high-resolution
// waitable timer (about 0.5 ms) when the system has one, else a 1 ms tick.
struct TickWaiter {
#ifdef _WIN32
    HANDLE timer = nullptr;
    bool coarse = false;
    TickWaiter() {
        timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (!timer) { coarse = true; timeBeginPeriod(1); }
    }
    ~TickWaiter() {
        if (timer) CloseHandle(timer);
        if (coarse) timeEndPeriod(1);
    }
#endif
    void wait_until(int64_t until_ns) {
        int64_t d = until_ns - now_ns();
        if (d <= 0) return;
#ifdef _WIN32
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(d / 100);
            if (due.QuadPart < 0 && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_for(std::chrono::nanoseconds(d));
    }
};

// Tick n of the song is cycle n / 96 (4 beats of 24); each tick leaves when
// that cycle is heard. A jump of the song clock resyncs without a burst. The
// thread sleeps to the next tick, at most 5 ms, so a new tempo applies soon.
void clock_loop(MidiPort* p) {
    TickWaiter waiter;
    int64_t sent = INT64_MIN;
    while (p->clock_run.load()) {
        double cycle = 0.0, cps = 0.5;
        int64_t t = 0;
        if (!sound_clock_anchor(&cycle, &t, &cps)) {
            waiter.wait_until(now_ns() + 2000000);
            continue;
        }
        int64_t now = now_ns();
        double c = cycle + (double)(now - t) / 1e9 * cps;
        int64_t target = (int64_t)std::floor(c * 96.0);
        if (sent == INT64_MIN || target < sent - 1 || target > sent + 96) sent = target - 1;
        while (sent < target) {
            sent++;
            if (p->clock_start.load() && ((sent % 96) + 96) % 96 == 0) {
                p->clock_start.store(false);
                send_bytes(*p, {0xFA});
            }
            send_bytes(*p, {0xF8});
        }
        int64_t next = t + (int64_t)(((double)(sent + 1) / 96.0 - cycle) / std::max(cps, 1e-3) * 1e9);
        waiter.wait_until(std::min(next, now + 5000000));
    }
}

void stop_clock(MidiPort& p) {
    p.clock_run.store(false);
    if (p.clock_thr.joinable()) p.clock_thr.join();
}

constexpr size_t MAX_QUEUED = 4096;
enum { ROUTE_OFF = 0, ROUTE_ALL = 1, ROUTE_CC = 2 };

void on_midi_in(double, std::vector<unsigned char>* msg, void* user) {
    auto* p = static_cast<MidiPort*>(user);
    if (!msg || msg->empty()) return;
    unsigned char s0 = (*msg)[0];
    if (s0 == 0xF8 || s0 == 0xFA || s0 == 0xFB || s0 == 0xFC) {
        std::lock_guard<std::mutex> lk(p->qmtx);
        int64_t t = now_ns();
        if (s0 == 0xF8) {
            if (p->clk_last_ns != 0) {
                double d = (double)(t - p->clk_last_ns);
                if (d < 1e9) p->clk_avg_ns = p->clk_avg_ns <= 0 ? d : p->clk_avg_ns + (d - p->clk_avg_ns) / 24.0;
            }
            p->clk_last_ns = t;
            p->clk_seen_ns = t;
            if (p->clk_running) p->clk_ticks++;
        } else if (s0 == 0xFA) {
            p->clk_running = true;
            p->clk_ticks = 0;
        } else if (s0 == 0xFB) {
            p->clk_running = true;
        } else {
            p->clk_running = false;
        }
    }
    if (s0 == 0xF8) return;   // ticks are counted above, not queued
    int mode = p->routed.load(std::memory_order_relaxed);
    if (mode == ROUTE_ALL || (mode == ROUTE_CC && ((*msg)[0] & 0xF0) == 0xB0)) {
        int ch = p->to_channel.load(std::memory_order_relaxed);
        unsigned char st = (*msg)[0];
        if (ch > 0 && st >= 0x80 && st < 0xF0) {
            unsigned char m[3] = {(unsigned char)((st & 0xF0) | (ch - 1)), 0, 0};
            size_t n = std::min<size_t>(msg->size(), 3);
            for (size_t i = 1; i < n; i++) m[i] = (*msg)[i];
            sound_midi_push(m, n);
        } else {
            sound_midi_push(msg->data(), msg->size());
        }
    }
    std::lock_guard<std::mutex> lk(p->qmtx);
    if (p->queue.size() >= MAX_QUEUED) p->queue.pop_front();
    p->queue.push_back(*msg);
}

void close_port(MidiPort& p) {
    stop_clock(p);
    if (p.in) {
        try { p.in->cancelCallback(); p.in->closePort(); } catch (...) {}
    }
}

std::map<int, MidiPort> g_ports;
std::mutex              g_mtx;
int                     g_next = 1;

// shared helper: send a 3-byte channel message on an open output handle
static Value send3(int h, int b0, int b1, int b2) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_ports.find(h);
    if (it == g_ports.end() || !it->second.out) return Value::make_bool(false);
    std::vector<unsigned char> msg = {
        (unsigned char)b0, (unsigned char)(b1 & 0x7F), (unsigned char)(b2 & 0x7F) };
    std::lock_guard<std::mutex> olk(it->second.omtx);
    try { it->second.out->sendMessage(&msg); return Value::make_bool(true); }
    catch (...) { return Value::make_bool(false); }
}

} // namespace

void register_midi_builtins(VM& vm) {
    // Clock threads end before the statics they read are torn down
    static bool exit_hook = false;
    if (!exit_hook) {
        exit_hook = true;
        std::atexit([] {
            std::lock_guard<std::mutex> lk(g_mtx);
            for (auto& kv : g_ports) stop_clock(kv.second);
        });
    }

    // MIDI.PORTS() -> { in:[name,...], out:[name,...] }
    vm.register_native("MIDI.PORTS", 0, 0, [](const std::vector<Value>&) -> Value {
        Value m = Value::make_object();
        Value inArr = Value::make_array(), outArr = Value::make_array();
        try {
            RtMidiIn probe;
            for (unsigned int i = 0; i < probe.getPortCount(); i++)
                inArr.as_array()->elements.push_back(Value::make_string(probe.getPortName(i)));
        } catch (...) {}
        try {
            RtMidiOut probe;
            for (unsigned int i = 0; i < probe.getPortCount(); i++)
                outArr.as_array()->elements.push_back(Value::make_string(probe.getPortName(i)));
        } catch (...) {}
        m.as_object()->set("in", std::move(inArr));
        m.as_object()->set("out", std::move(outArr));
        return m;
    });

    // MIDI.OPEN_OUT(portIndex) -> handle (>=1) or -1
    vm.register_native("MIDI.OPEN_OUT", 1, 1, [](const std::vector<Value>& args) -> Value {
        int idx = (int)args[0].to_int();
        try {
            auto out = std::make_unique<RtMidiOut>();
            if (idx < 0 || (unsigned)idx >= out->getPortCount()) return Value::make_i64(-1);
            out->openPort(idx);
            std::lock_guard<std::mutex> lk(g_mtx);
            int h = g_next++;
            g_ports[h].out = std::move(out);
            return Value::make_i64(h);
        } catch (...) { return Value::make_i64(-1); }
    });

    // MIDI.OPEN_IN(portIndex) -> handle (>=1) or -1
    vm.register_native("MIDI.OPEN_IN", 1, 1, [](const std::vector<Value>& args) -> Value {
        int idx = (int)args[0].to_int();
        try {
            auto in = std::make_unique<RtMidiIn>();
            if (idx < 0 || (unsigned)idx >= in->getPortCount()) return Value::make_i64(-1);
            in->openPort(idx);
            std::lock_guard<std::mutex> lk(g_mtx);
            int h = g_next++;
            MidiPort& p = g_ports[h];
            p.in = std::move(in);
            p.in->setCallback(&on_midi_in, &p);
            return Value::make_i64(h);
        } catch (...) { return Value::make_i64(-1); }
    });

    // MIDI.SEND(handle, b0 [, b1 [, b2]]) -> bool   raw message bytes
    vm.register_native("MIDI.SEND", 2, 4, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_ports.find(h);
        if (it == g_ports.end() || !it->second.out) return Value::make_bool(false);
        std::vector<unsigned char> msg;
        for (size_t i = 1; i < args.size(); i++)
            msg.push_back((unsigned char)(int)args[i].to_int());
        std::lock_guard<std::mutex> olk(it->second.omtx);
        try { it->second.out->sendMessage(&msg); return Value::make_bool(true); }
        catch (...) { return Value::make_bool(false); }
    });

    // MIDI.NOTEON(handle, channel, note, velocity) -> bool
    vm.register_native("MIDI.NOTEON", 4, 4, [](const std::vector<Value>& args) -> Value {
        return send3((int)args[0].to_int(), 0x90 | ((int)args[1].to_int() & 0x0F),
                     (int)args[2].to_int(), (int)args[3].to_int());
    });

    // MIDI.NOTEOFF(handle, channel, note) -> bool
    vm.register_native("MIDI.NOTEOFF", 3, 3, [](const std::vector<Value>& args) -> Value {
        return send3((int)args[0].to_int(), 0x80 | ((int)args[1].to_int() & 0x0F),
                     (int)args[2].to_int(), 0);
    });

    // MIDI.CC(handle, channel, cc, value) -> bool
    vm.register_native("MIDI.CC", 4, 4, [](const std::vector<Value>& args) -> Value {
        return send3((int)args[0].to_int(), 0xB0 | ((int)args[1].to_int() & 0x0F),
                     (int)args[2].to_int(), (int)args[3].to_int());
    });

    // MIDI.POLL(handle) -> [ [b0,b1,b2], ... ]  drain pending input messages
    vm.register_native("MIDI.POLL", 1, 1, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        Value arr = Value::make_array();
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_ports.find(h);
        if (it == g_ports.end() || !it->second.in) return arr;
        std::deque<std::vector<unsigned char>> got;
        {
            std::lock_guard<std::mutex> qlk(it->second.qmtx);
            got.swap(it->second.queue);
        }
        for (const auto& msg : got) {
            Value ev = Value::make_array();
            for (unsigned char b : msg) ev.as_array()->elements.push_back(Value::make_i64(b));
            arr.as_array()->elements.push_back(std::move(ev));
        }
        return arr;
    });

    // MIDI.ROUTE(handle [, on [, channel]]) -> bool: an input port plays the synth
    // directly, inside the audio thread (zones: SOUND.ZONE). on is TRUE,
    // FALSE, or "CC" to pass only controllers (SOUND.CCMAP) and no notes.
    // channel 1-16 moves every routed message to that channel (0 keeps it).
    vm.register_native("MIDI.ROUTE", 1, 3, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        int mode = ROUTE_ALL;
        if (args.size() >= 2) {
            if (args[1].type == ValueType::STRING) {
                std::string s = args[1].to_string();
                for (auto& c : s) c = (char)toupper((unsigned char)c);
                if (s == "CC") mode = ROUTE_CC;
                else if (s == "ALL") mode = ROUTE_ALL;
                else if (s == "OFF") mode = ROUTE_OFF;
                else throw jdError(ErrCode::RUNTIME_ERROR, "MIDI.ROUTE: mode is TRUE, FALSE, \"ALL\", \"CC\" or \"OFF\", not " + s);
            } else {
                mode = args[1].to_bool() ? ROUTE_ALL : ROUTE_OFF;
            }
        }
        int ch = args.size() >= 3 ? (int)args[2].to_int() : 0;
        if (ch < 0 || ch > 16)
            throw jdError(ErrCode::RUNTIME_ERROR, "MIDI.ROUTE: channel is 1-16, or 0 to keep it");
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_ports.find(h);
        if (it == g_ports.end() || !it->second.in) return Value::make_bool(false);
        it->second.routed.store(mode);
        it->second.to_channel.store(ch);
        return Value::make_bool(true);
    });

    // MIDI.CLOCK(handle, mode) -> bool: MIDI clock out, 24 ticks per beat in
    // step with what the sequencer plays. TRUE / "ON" ticks only, "START"
    // ticks and sends Start on the next bar line, "STOP" sends Stop and keeps
    // ticking, FALSE / "OFF" sends Stop and ends the ticks.
    vm.register_native("MIDI.CLOCK", 2, 2, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        std::string mode = "ON";
        if (args[1].type == ValueType::STRING) {
            mode = args[1].to_string();
            for (auto& c : mode) c = (char)toupper((unsigned char)c);
        } else if (!args[1].to_bool()) {
            mode = "OFF";
        }
        if (mode != "ON" && mode != "OFF" && mode != "START" && mode != "STOP")
            throw jdError(ErrCode::RUNTIME_ERROR, "MIDI.CLOCK: mode is TRUE, FALSE, \"ON\", \"OFF\", \"START\" or \"STOP\", not " + mode);
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_ports.find(h);
        if (it == g_ports.end() || !it->second.out) return Value::make_bool(false);
        MidiPort& p = it->second;
        if (mode == "OFF" || mode == "STOP") {
            p.clock_start.store(false);
            send_bytes(p, {0xFC});
            if (mode == "OFF") stop_clock(p);
            return Value::make_bool(true);
        }
        if (mode == "START") p.clock_start.store(true);
        if (!p.clock_run.load()) {
            if (p.clock_thr.joinable()) p.clock_thr.join();
            p.clock_run.store(true);
            p.clock_thr = std::thread(clock_loop, &p);
        }
        return Value::make_bool(true);
    });

    // MIDI.CLOCKIN(handle) -> {bpm, running, ticks}: the clock an input port
    // receives. bpm is 0 when no tick came for a second; ticks counts from
    // the last Start, 24 per beat.
    vm.register_native("MIDI.CLOCKIN", 1, 1, [](const std::vector<Value>& args) -> Value {
        double bpm = 0.0;
        bool running = false;
        int64_t ticks = 0;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            auto it = g_ports.find((int)args[0].to_int());
            if (it != g_ports.end() && it->second.in) {
                MidiPort& p = it->second;
                std::lock_guard<std::mutex> qlk(p.qmtx);
                if (p.clk_avg_ns > 0 && now_ns() - p.clk_seen_ns < 1000000000LL)
                    bpm = 60e9 / (p.clk_avg_ns * 24.0);
                running = p.clk_running;
                ticks = p.clk_ticks;
            }
        }
        Value r = Value::make_object();
        r.as_object()->lower_keys = true;
        r.as_object()->set("bpm", Value::make_f64(bpm));
        r.as_object()->set("running", Value::make_bool(running));
        r.as_object()->set("ticks", Value::make_i64(ticks));
        return r;
    });

    // MIDI.CLOSE(handle)
    vm.register_native("MIDI.CLOSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_ports.find((int)args[0].to_int());
        if (it == g_ports.end()) return Value::make_none();
        close_port(it->second);
        g_ports.erase(it);
        return Value::make_none();
    });
}

#else  // !MIDI

void register_midi_builtins(VM&) {}

#endif
