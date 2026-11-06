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

namespace {

// An input port collects its messages for MIDI.POLL from RtMidi's callback
// thread; a routed port also hands each one to the synth right away.
struct MidiPort {
    std::mutex qmtx;
    std::deque<std::vector<unsigned char>> queue;
    std::atomic<int> routed{0};
    std::atomic<int> to_channel{0};   // 1-16: routed messages move to this channel
    std::unique_ptr<RtMidiOut> out;
    std::unique_ptr<RtMidiIn>  in;
};

constexpr size_t MAX_QUEUED = 4096;
enum { ROUTE_OFF = 0, ROUTE_ALL = 1, ROUTE_CC = 2 };

void on_midi_in(double, std::vector<unsigned char>* msg, void* user) {
    auto* p = static_cast<MidiPort*>(user);
    if (!msg || msg->empty()) return;
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
    try { it->second.out->sendMessage(&msg); return Value::make_bool(true); }
    catch (...) { return Value::make_bool(false); }
}

} // namespace

void register_midi_builtins(VM& vm) {

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
