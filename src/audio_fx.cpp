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
#include <map>
#include <mutex>
#include <algorithm>
#include <cstdio>

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

// read a WAV file into a mono float buffer (left channel), for cabinet IRs
static bool read_wav_mono(const std::string& path, std::vector<float>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    if (buf.size() < 44 || std::memcmp(&buf[0], "RIFF", 4) != 0 ||
        std::memcmp(&buf[8], "WAVE", 4) != 0) return false;
    uint16_t fmt = 1, ch = 1, bits = 16;
    const unsigned char* data = nullptr;
    uint32_t dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const unsigned char* c = &buf[pos];
        uint32_t sz = r32(c + 4);
        if (std::memcmp(c, "fmt ", 4) == 0 && pos + 24 <= buf.size()) {
            fmt = r16(c + 8); ch = r16(c + 10); bits = r16(c + 22);
        } else if (std::memcmp(c, "data", 4) == 0) {
            data = c + 8; dataLen = sz;
            if (pos + 8 + (size_t)dataLen > buf.size())
                dataLen = (uint32_t)(buf.size() - (pos + 8));
        }
        pos += 8 + sz + (sz & 1);
    }
    if (!data || ch < 1) return false;
    std::vector<float> all;
    if (fmt == 3 && bits == 32) {
        uint32_t n = dataLen / 4; all.resize(n);
        for (uint32_t i = 0; i < n; i++) std::memcpy(&all[i], data + i * 4, 4);
    } else if (fmt == 1 && bits == 16) {
        uint32_t n = dataLen / 2; all.resize(n);
        for (uint32_t i = 0; i < n; i++) all[i] = (int16_t)r16(data + i * 2) / 32768.0f;
    } else if (fmt == 1 && bits == 8) {
        all.resize(dataLen);
        for (uint32_t i = 0; i < dataLen; i++) all[i] = (data[i] - 128) / 128.0f;
    } else return false;
    out.clear();
    for (size_t i = 0; i < all.size(); i += ch) out.push_back(all[i]);  // left channel
    return true;
}

// ---- FX chain: offline effect nodes on a mono float buffer ----

struct FxNode {
    std::string type;
    std::map<std::string, double> p;
    std::vector<float> dl;           // delay line
    std::vector<float> ir;           // cabinet impulse response
    size_t dpos = 0;
    double z1 = 0.0, z2 = 0.0;       // biquad state
    double env = 0.0;                // compressor envelope
    double param(const char* k, double def) const {
        auto it = p.find(k); return it == p.end() ? def : it->second;
    }
};
struct FxChain { std::vector<FxNode> nodes; };

std::map<int, FxChain> g_chains;
int        g_chain_next = 1;
std::mutex g_chain_mtx;

static void biquad_coeffs(const std::string& type, double cutoff, double Q, double rate,
                          double& b0, double& b1, double& b2, double& a1, double& a2) {
    double w0 = 2.0 * 3.14159265358979323846 * cutoff / rate;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * (Q <= 0.0 ? 0.707 : Q));
    if (type == "highpass") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
    else                    { b0 = (1 - cw) / 2; b1 =  (1 - cw); b2 = (1 - cw) / 2; }
    double a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
}

// run one effect node over a block of `cnt` samples (stateful, in place)
static void process_node(FxNode& n, float* buf, unsigned cnt, double rate) {
    const std::string& t = n.type;
    if (t == "gain") {
        double g = n.param("gain", 1.0);
        for (unsigned i = 0; i < cnt; i++) buf[i] = (float)(buf[i] * g);
    } else if (t == "drive") {
        double g = 1.0 + n.param("amount", 5.0), lvl = n.param("level", 0.7);
        for (unsigned i = 0; i < cnt; i++) buf[i] = (float)(std::tanh(buf[i] * g) * lvl);
    } else if (t == "lowpass" || t == "highpass") {
        double b0, b1, b2, a1, a2;
        biquad_coeffs(t, n.param("cutoff", 2000.0), n.param("q", 0.707), rate, b0, b1, b2, a1, a2);
        for (unsigned i = 0; i < cnt; i++) {
            double in = buf[i];
            double out = b0 * in + n.z1;
            n.z1 = b1 * in - a1 * out + n.z2;
            n.z2 = b2 * in - a2 * out;
            buf[i] = (float)out;
        }
    } else if (t == "delay") {
        double ms = n.param("time_ms", 300.0), fb = n.param("feedback", 0.35), mix = n.param("mix", 0.3);
        size_t len = (size_t)std::max(1.0, ms / 1000.0 * rate);
        if (n.dl.size() != len) { n.dl.assign(len, 0.0f); n.dpos = 0; }
        for (unsigned i = 0; i < cnt; i++) {
            double in = buf[i], wet = n.dl[n.dpos];
            n.dl[n.dpos] = (float)(in + wet * fb);
            n.dpos = (n.dpos + 1) % len;
            buf[i] = (float)(in + wet * mix);
        }
    } else if (t == "compressor") {
        double thr = n.param("threshold", 0.5), ratio = n.param("ratio", 4.0), makeup = n.param("makeup", 1.0);
        double att = std::exp(-1.0 / (0.005 * rate)), rel = std::exp(-1.0 / (0.1 * rate));
        for (unsigned i = 0; i < cnt; i++) {
            double a = std::fabs((double)buf[i]);
            n.env = (a > n.env) ? (att * n.env + (1 - att) * a) : (rel * n.env + (1 - rel) * a);
            double red = 1.0;
            if (n.env > thr && n.env > 1e-9) red = (thr + (n.env - thr) / ratio) / n.env;
            buf[i] = (float)(buf[i] * red * makeup);
        }
    } else if (t == "cabinet") {
        if (n.ir.empty()) return;                 // no IR -> pass-through (offline only)
        double level = n.param("level", 0.7), mix = n.param("mix", 1.0);
        size_t M = n.ir.size();
        std::vector<float> out(cnt, 0.0f);
        for (unsigned i = 0; i < cnt; i++) {
            double acc = 0.0;
            size_t kmax = std::min(M, (size_t)i + 1);
            for (size_t k = 0; k < kmax; k++) acc += (double)buf[i - k] * n.ir[k];
            out[i] = (float)(buf[i] * (1.0 - mix) + acc * level * mix);
        }
        for (unsigned i = 0; i < cnt; i++) buf[i] = out[i];
    }
    // unknown type: pass-through
}

// pre-populate a node's full parameter set, so a live FX.SET only ever updates an
// existing value (no map insertion -> safe to tweak while the callback reads it).
static void ensure_defaults(FxNode& n) {
    auto def = [&](const char* k, double v) { if (n.p.find(k) == n.p.end()) n.p[k] = v; };
    const std::string& t = n.type;
    if      (t == "gain")       { def("gain", 1.0); }
    else if (t == "drive")      { def("amount", 5.0); def("level", 0.7); }
    else if (t == "lowpass" || t == "highpass") { def("cutoff", 2000.0); def("q", 0.707); }
    else if (t == "delay")      { def("time_ms", 300.0); def("feedback", 0.35); def("mix", 0.3); }
    else if (t == "compressor") { def("threshold", 0.5); def("ratio", 4.0); def("makeup", 1.0); }
    else if (t == "cabinet")    { def("level", 0.7); def("mix", 1.0); }
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

    // ---- FX chain builtins ----

    // FX.NEW() -> chain handle
    vm.register_native("FX.NEW", 0, 0, [](const std::vector<Value>&) -> Value {
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        int h = g_chain_next++;
        g_chains[h] = FxChain{};
        return Value::make_i64(h);
    });

    // FX.ADD(chain, type$, [params{}]) -> bool
    // types: gain{gain} drive{amount,level} lowpass/highpass{cutoff,q}
    //        delay{time_ms,feedback,mix} compressor{threshold,ratio,makeup}
    vm.register_native("FX.ADD", 2, 3, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        auto it = g_chains.find(h);
        if (it == g_chains.end()) return Value::make_bool(false);
        FxNode node;
        node.type = args[1].to_string();
        if (args.size() >= 3 && args[2].type == ValueType::OBJECT) {
            std::string ir_path;
            for (auto& kv : args[2].as_object()->fields) {
                if (kv.second.type == ValueType::STRING) {
                    if (kv.first == "ir") ir_path = kv.second.to_string();
                } else {
                    node.p[kv.first] = kv.second.to_double();
                }
            }
            if (node.type == "cabinet" && !ir_path.empty()) {
                std::vector<float> ir;
                if (read_wav_mono(ir_path, ir)) {
                    float peak = 0.0f;
                    for (float v : ir) peak = std::max(peak, std::fabs(v));
                    if (peak > 1e-9f) for (float& v : ir) v /= peak;  // unit-peak normalize
                    node.ir = std::move(ir);
                }
            }
        }
        ensure_defaults(node);
        it->second.nodes.push_back(std::move(node));
        return Value::make_bool(true);
    });

    // FX.PROCESS(chain, samples[], [rate=44100]) -> samples[]  (offline, mono)
    vm.register_native("FX.PROCESS", 2, 3, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        std::vector<float> buf;
        if (!collect_floats(args[1], buf)) return Value::make_array();
        double rate = args.size() >= 3 ? args[2].to_double() : 44100.0;
        if (rate <= 0) rate = 44100.0;
        {
            std::lock_guard<std::mutex> lk(g_chain_mtx);
            auto it = g_chains.find(h);
            if (it != g_chains.end())
                for (auto& n : it->second.nodes) process_node(n, buf.data(), (unsigned)buf.size(), rate);
        }
        Value out = Value::make_array();
        out.as_array()->elements.reserve(buf.size());
        for (float f : buf) out.as_array()->elements.push_back(Value::make_f64(f));
        return out;
    });

    // FX.FREE(chain)
    vm.register_native("FX.FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        g_chains.erase((int)args[0].to_int());
        return Value::make_none();
    });

    // FX.SET(chain, nodeIndex, param$, value) -> bool   tweak a node param LIVE.
    // Only updates an existing param (RT-safe: no map insertion while monitoring).
    vm.register_native("FX.SET", 4, 4, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int(), idx = (int)args[1].to_int();
        std::string key = args[2].to_string();
        double val = args[3].to_double();
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        auto it = g_chains.find(h);
        if (it == g_chains.end() || idx < 0 || idx >= (int)it->second.nodes.size())
            return Value::make_bool(false);
        auto& p = it->second.nodes[idx].p;
        auto pit = p.find(key);
        if (pit == p.end()) return Value::make_bool(false);
        pit->second = val;
        return Value::make_bool(true);
    });

    // FX.DUMP$(chain) -> readable listing of the chain's nodes + params (for the REPL)
    vm.register_native("FX.DUMP$", 1, 1, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        auto it = g_chains.find(h);
        if (it == g_chains.end()) return Value::make_string("(no such chain)");
        std::string s;
        char buf[80];
        for (size_t i = 0; i < it->second.nodes.size(); i++) {
            auto& n = it->second.nodes[i];
            snprintf(buf, sizeof(buf), "%u: %-10s", (unsigned)i, n.type.c_str());
            s += buf;
            for (auto& kv : n.p) {
                snprintf(buf, sizeof(buf), " %s=%g", kv.first.c_str(), kv.second);
                s += buf;
            }
            s += "\n";
        }
        return Value::make_string(s);
    });

    // array-taking builtins must not auto-vectorize (would run per element)
    vm.extra_no_vectorize.insert("WAV.WRITE");
    vm.extra_no_vectorize.insert("FX.PROCESS");
}

// realtime block processing (called from the audio callback). No lock: the chain
// must not be mutated while monitoring. Cabinet is skipped (it allocates).
void fx_process_chain_rt(int handle, float* buf, unsigned frames, double rate) {
    auto it = g_chains.find(handle);
    if (it == g_chains.end()) return;
    for (auto& n : it->second.nodes)
        if (n.type != "cabinet") process_node(n, buf, frames, rate);
}

#else  // !FX

void register_audiofx_builtins(VM&) {}
void fx_process_chain_rt(int, float*, unsigned, double) {}

#endif
