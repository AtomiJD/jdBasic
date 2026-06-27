#include "midi.h"
#include "vm.h"
#include "value.h"

#ifdef MIDI

#include "RtMidi.h"
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace {

struct MidiPort {
    std::unique_ptr<RtMidiOut> out;
    std::unique_ptr<RtMidiIn>  in;
};

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
            g_ports[h].in = std::move(in);
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
        std::vector<unsigned char> msg;
        while (true) {
            msg.clear();
            try { it->second.in->getMessage(&msg); } catch (...) { break; }
            if (msg.empty()) break;
            Value ev = Value::make_array();
            for (unsigned char b : msg) ev.as_array()->elements.push_back(Value::make_i64(b));
            arr.as_array()->elements.push_back(std::move(ev));
        }
        return arr;
    });

    // MIDI.CLOSE(handle)
    vm.register_native("MIDI.CLOSE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_ports.erase((int)args[0].to_int());
        return Value::make_none();
    });
}

#else  // !MIDI

void register_midi_builtins(VM&) {}

#endif
