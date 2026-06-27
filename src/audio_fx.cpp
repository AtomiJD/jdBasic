#include "audio_fx.h"
#include "vm.h"
#include "value.h"

#ifdef FX

#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <iterator>

namespace {

// little-endian writers
static void w16(std::ofstream& f, uint16_t v) {
    f.put((char)(v & 0xFF)); f.put((char)((v >> 8) & 0xFF));
}
static void w32(std::ofstream& f, uint32_t v) {
    f.put((char)(v & 0xFF)); f.put((char)((v >> 8) & 0xFF));
    f.put((char)((v >> 16) & 0xFF)); f.put((char)((v >> 24) & 0xFF));
}
static uint16_t r16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t r32(const unsigned char* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

// flatten an ARRAY Value of numbers into interleaved float samples
static bool collect_floats(const Value& v, std::vector<float>& out) {
    if (v.type != ValueType::ARRAY) return false;
    auto* a = v.as_array();
    out.reserve(a->elements.size());
    for (auto& e : a->elements) out.push_back((float)e.to_double());
    return true;
}

} // namespace

void register_audiofx_builtins(VM& vm) {

    // WAV.WRITE(path$, samples[], [rate=44100], [channels=1]) -> bool
    // samples are interleaved floats in [-1, 1]; written as 16-bit PCM.
    vm.register_native("WAV.WRITE", 2, 4, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].to_string();
        std::vector<float> samples;
        if (path.empty() || !collect_floats(args[1], samples)) return Value::make_bool(false);
        int rate = args.size() >= 3 ? (int)args[2].to_int() : 44100;
        int ch   = args.size() >= 4 ? (int)args[3].to_int() : 1;
        if (rate <= 0) rate = 44100;
        if (ch < 1) ch = 1;

        std::ofstream f(path, std::ios::binary);
        if (!f) return Value::make_bool(false);

        const uint16_t bits = 16;
        uint32_t nsamp = (uint32_t)samples.size();
        uint32_t dataBytes = nsamp * (bits / 8);
        uint16_t blockAlign = (uint16_t)(ch * (bits / 8));
        uint32_t byteRate = (uint32_t)rate * blockAlign;

        f.write("RIFF", 4); w32(f, 36 + dataBytes); f.write("WAVE", 4);
        f.write("fmt ", 4); w32(f, 16); w16(f, 1); w16(f, (uint16_t)ch);
        w32(f, (uint32_t)rate); w32(f, byteRate); w16(f, blockAlign); w16(f, bits);
        f.write("data", 4); w32(f, dataBytes);
        for (uint32_t i = 0; i < nsamp; i++) {
            float s = samples[i];
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            int16_t iv = (int16_t)lrintf(s * 32767.0f);
            w16(f, (uint16_t)iv);
        }
        return Value::make_bool((bool)f);
    });

    // WAV.READ(path$) -> { samples:[float -1..1], rate, channels, frames } or NONE
    vm.register_native("WAV.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].to_string();
        std::ifstream f(path, std::ios::binary);
        if (!f) return Value::make_none();
        std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
        if (buf.size() < 44 || std::memcmp(&buf[0], "RIFF", 4) != 0 ||
            std::memcmp(&buf[8], "WAVE", 4) != 0)
            return Value::make_none();

        uint16_t fmt = 1, ch = 1, bits = 16;
        uint32_t rate = 44100;
        const unsigned char* data = nullptr;
        uint32_t dataLen = 0;
        size_t pos = 12;
        while (pos + 8 <= buf.size()) {
            const unsigned char* c = &buf[pos];
            uint32_t sz = r32(c + 4);
            if (std::memcmp(c, "fmt ", 4) == 0 && pos + 24 <= buf.size()) {
                fmt = r16(c + 8); ch = r16(c + 10); rate = r32(c + 12); bits = r16(c + 22);
            } else if (std::memcmp(c, "data", 4) == 0) {
                data = c + 8; dataLen = sz;
                if (pos + 8 + (size_t)dataLen > buf.size())
                    dataLen = (uint32_t)(buf.size() - (pos + 8));
            }
            pos += 8 + sz + (sz & 1);
        }
        if (!data || ch < 1) return Value::make_none();

        Value arr = Value::make_array();
        auto* el = arr.as_array();
        if (fmt == 3 && bits == 32) {
            uint32_t n = dataLen / 4;
            el->elements.reserve(n);
            for (uint32_t i = 0; i < n; i++) {
                float s; std::memcpy(&s, data + i * 4, 4);
                el->elements.push_back(Value::make_f64(s));
            }
        } else if (fmt == 1 && bits == 16) {
            uint32_t n = dataLen / 2;
            el->elements.reserve(n);
            for (uint32_t i = 0; i < n; i++)
                el->elements.push_back(Value::make_f64((int16_t)r16(data + i * 2) / 32768.0));
        } else if (fmt == 1 && bits == 8) {
            el->elements.reserve(dataLen);
            for (uint32_t i = 0; i < dataLen; i++)
                el->elements.push_back(Value::make_f64((data[i] - 128) / 128.0));
        } else {
            return Value::make_none();
        }

        Value m = Value::make_object();
        uint32_t frames = (uint32_t)el->elements.size() / ch;
        m.as_object()->set("samples", std::move(arr));
        m.as_object()->set("rate", Value::make_i64(rate));
        m.as_object()->set("channels", Value::make_i64(ch));
        m.as_object()->set("frames", Value::make_i64(frames));
        return m;
    });

    // WAV.INFO(path$) -> { rate, channels, bits } (canonical header only) or NONE
    vm.register_native("WAV.INFO", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].to_string();
        std::ifstream f(path, std::ios::binary);
        if (!f) return Value::make_none();
        unsigned char h[64];
        f.read((char*)h, 64);
        if (f.gcount() < 36 || std::memcmp(h, "RIFF", 4) != 0 ||
            std::memcmp(h + 8, "WAVE", 4) != 0 || std::memcmp(h + 12, "fmt ", 4) != 0)
            return Value::make_none();
        Value m = Value::make_object();
        m.as_object()->set("channels", Value::make_i64(r16(h + 22)));
        m.as_object()->set("rate", Value::make_i64(r32(h + 24)));
        m.as_object()->set("bits", Value::make_i64(r16(h + 34)));
        return m;
    });

    // WAV.WRITE takes a whole sample array as an argument; without this it would
    // auto-vectorize (one call per sample). (Native -c path uses the codegen
    // no_vectorize list separately - to wire when WAV.* is needed under -c.)
    vm.extra_no_vectorize.insert("WAV.WRITE");
}

#else  // !FX

void register_audiofx_builtins(VM&) {}

#endif
