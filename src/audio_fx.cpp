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

// ---- WAV parsing, shared by WAV.READ, WAV.INFO and the cabinet IR loader ----

struct WavFmt {
    uint16_t tag      = 1;       // 1 = PCM, 3 = IEEE float (EXTENSIBLE resolved)
    uint16_t channels = 1;
    uint16_t bits     = 16;
    uint32_t rate     = 44100;
    size_t   data_off = 0;       // file offset of the data payload
    uint32_t data_len = 0;       // length the header declares
    bool     ok       = false;   // fmt chunk understood
};

// Walk the RIFF chunk list. buf may hold only the head of the file; data_off
// and data_len then still describe the payload and the caller clamps them.
static WavFmt scan_wav(const unsigned char* buf, size_t n) {
    WavFmt w;
    if (n < 12 || std::memcmp(buf, "RIFF", 4) != 0 || std::memcmp(buf + 8, "WAVE", 4) != 0)
        return w;
    bool have_fmt = false;
    size_t pos = 12;
    while (pos + 8 <= n) {
        const unsigned char* c = buf + pos;
        uint32_t sz = r32(c + 4);
        if (std::memcmp(c, "fmt ", 4) == 0 && pos + 24 <= n) {
            w.tag = r16(c + 8); w.channels = r16(c + 10);
            w.rate = r32(c + 12); w.bits = r16(c + 22);
            // WAVE_FORMAT_EXTENSIBLE keeps the real tag in the first two bytes
            // of the SubFormat GUID.
            if (w.tag == 0xFFFE && sz >= 40 && pos + 34 <= n) w.tag = r16(c + 32);
            have_fmt = true;
        } else if (std::memcmp(c, "data", 4) == 0) {
            w.data_off = pos + 8;
            w.data_len = sz;
        }
        pos += 8 + (size_t)sz + (sz & 1);
    }
    w.ok = have_fmt && w.channels >= 1 && w.bits >= 8 && (w.bits % 8) == 0;
    return w;
}

// decode the data payload into interleaved floats in [-1, 1]
static bool wav_samples(const unsigned char* data, size_t len, const WavFmt& w,
                        std::vector<float>& out) {
    size_t bytes = (size_t)w.bits / 8;
    size_t n = len / bytes;
    out.clear();
    out.reserve(n);
    if (w.tag == 1 && w.bits == 8) {
        for (size_t i = 0; i < n; i++) out.push_back((data[i] - 128) / 128.0f);
    } else if (w.tag == 1 && w.bits == 16) {
        for (size_t i = 0; i < n; i++)
            out.push_back((int16_t)r16(data + i * 2) / 32768.0f);
    } else if (w.tag == 1 && w.bits == 24) {
        // assembled into the top three bytes of an int32, which sign-extends it
        for (size_t i = 0; i < n; i++) {
            const unsigned char* p = data + i * 3;
            int32_t v = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) |
                                  ((uint32_t)p[2] << 24));
            out.push_back((float)(v / 2147483648.0));
        }
    } else if (w.tag == 1 && w.bits == 32) {
        for (size_t i = 0; i < n; i++)
            out.push_back((float)((int32_t)r32(data + i * 4) / 2147483648.0));
    } else if (w.tag == 3 && w.bits == 32) {
        for (size_t i = 0; i < n; i++) {
            float s; std::memcpy(&s, data + i * 4, 4); out.push_back(s);
        }
    } else if (w.tag == 3 && w.bits == 64) {
        for (size_t i = 0; i < n; i++) {
            double s; std::memcpy(&s, data + i * 8, 8); out.push_back((float)s);
        }
    } else {
        return false;
    }
    return true;
}

// whole file -> interleaved floats
static bool load_wav(const std::string& path, WavFmt& w, std::vector<float>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    w = scan_wav(buf.data(), buf.size());
    if (!w.ok || w.data_off == 0 || w.data_off >= buf.size()) return false;
    size_t len = w.data_len;
    if (w.data_off + len > buf.size()) len = buf.size() - w.data_off;
    return wav_samples(buf.data() + w.data_off, len, w, out);
}

// left channel of a WAV, for cabinet IRs
static bool read_wav_mono(const std::string& path, std::vector<float>& out) {
    WavFmt w;
    std::vector<float> all;
    if (!load_wav(path, w, all)) return false;
    out.clear();
    out.reserve(all.size() / w.channels + 1);
    for (size_t i = 0; i < all.size(); i += w.channels) out.push_back(all[i]);
    return true;
}

// ---- FX chain: offline effect nodes on a mono float buffer ----

struct FxNode {
    std::string type;
    std::map<std::string, double> p;
    std::vector<float> dl;           // delay line
    std::vector<float> ir;           // cabinet impulse response
    size_t dpos = 0;
    double z1 = 0.0, z2 = 0.0;       // biquad state (also phaser feedback in z1)
    double env = 0.0;                // compressor / wah / gate envelope
    double lfo = 0.0;                // modulation LFO phase
    double ap[8] = {0,0,0,0,0,0,0,0};// phaser allpass stages (x,y per stage)
    double hold = 0.0;               // bitcrush sample-and-hold value
    int    crush = 0;                // bitcrush downsample counter
    double gr = 0.0;                 // noisegate gain smoothing
    std::vector<std::vector<float>> cb;  // reverb comb buffers
    std::vector<std::vector<float>> apb; // reverb allpass buffers
    std::vector<float>  clp;         // reverb per-comb damping state
    std::vector<size_t> cpos, appos; // reverb buffer positions
    double param(const char* k, double def) const {
        auto it = p.find(k); return it == p.end() ? def : it->second;
    }
};
// One leg of a split chain: its own effects, plus where it lands in the stereo
// image. pan -1 is hard left, +1 hard right.
struct FxBranch {
    std::vector<FxNode> nodes;
    double level = 1.0;
    double pan   = 0.0;
};

// nodes is the section every signal passes; with split set, its output feeds
// branch a and b, which are mixed back together at the output.
struct FxChain {
    std::vector<FxNode> nodes;
    bool     split = false;
    FxBranch a, b;
    std::vector<float> sa, sb;      // realtime scratch, never sized on the audio thread
};

std::map<int, FxChain> g_chains;
int        g_chain_next = 1;
std::mutex g_chain_mtx;

static const double FX_PI    = 3.14159265358979323846;
static const double FX_2PI   = 6.28318530717958647692;

static void biquad_coeffs(const std::string& type, double cutoff, double Q, double rate,
                          double& b0, double& b1, double& b2, double& a1, double& a2,
                          double gainDb = 0.0) {
    double w0 = FX_2PI * cutoff / rate;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * (Q <= 0.0 ? 0.707 : Q));
    double a0;
    if (type == "bandpass") {            // 0 dB peak bandpass (BPF)
        b0 = alpha; b1 = 0; b2 = -alpha;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    } else if (type == "peak") {         // peaking EQ, gainDb cut/boost
        double A = std::pow(10.0, gainDb / 40.0);
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    } else {
        if (type == "highpass") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
        else                    { b0 = (1 - cw) / 2; b1 =  (1 - cw); b2 = (1 - cw) / 2; }
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    }
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
        // A valve stage clips its two halves differently and shifts its
        // operating point while it is being hit, coming back slowly. Both
        // ride into the same nonlinearity as an offset, so with asym and
        // bloom at zero this is exactly the plain symmetric shaper.
        double asym = n.param("asym", 0.0);
        double bloom = n.param("bloom", 0.0);
        if (asym == 0.0 && bloom == 0.0) {
            for (unsigned i = 0; i < cnt; i++) buf[i] = (float)(std::tanh(buf[i] * g) * lvl);
        } else {
            double rec = n.param("recover", 0.25);          // seconds back to rest
            double a = std::exp(-1.0 / (std::max(0.01, rec) * rate));
            // the coupling capacitor after a valve stage: an offset shifts
            // the waveform while it is driven, but none of it reaches the
            // next stage
            const double R = 1.0 - 12.0 / rate;
            for (unsigned i = 0; i < cnt; i++) {
                double x = buf[i];
                n.lfo = a * n.lfo + (1.0 - a) * std::fabs(x);   // envelope of how hard it is driven
                double b = asym + bloom * n.lfo;
                double y = (std::tanh(x * g + b) - std::tanh(b)) * lvl;
                double out = y - n.z1 + R * n.z2;
                n.z1 = y;
                n.z2 = out;
                buf[i] = (float)out;
            }
        }
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
        // dry 0 leaves the repeats alone, which is what an echo-only amp wants
        double dry = n.param("dry", 1.0);
        size_t len = (size_t)std::max(1.0, ms / 1000.0 * rate);
        if (n.dl.size() != len) { n.dl.assign(len, 0.0f); n.dpos = 0; }
        for (unsigned i = 0; i < cnt; i++) {
            double in = buf[i], wet = n.dl[n.dpos];
            n.dl[n.dpos] = (float)(in + wet * fb);
            n.dpos = (n.dpos + 1) % len;
            buf[i] = (float)(in * dry + wet * mix);
        }
    } else if (t == "compressor") {
        double thr = n.param("threshold", 0.5), ratio = n.param("ratio", 4.0), makeup = n.param("makeup", 1.0);
        double att_s = std::max(0.0001, n.param("attack", 0.005));
        double rel_s = std::max(0.001, n.param("release", 0.1));
        // sag stretches the recovery with how hard the stage was hit, which
        // is what a power supply sinking under a chord does. 0 = plain.
        double sag = n.param("sag", 0.0);
        double att = std::exp(-1.0 / (att_s * rate));
        double rel = std::exp(-1.0 / (rel_s * rate));
        for (unsigned i = 0; i < cnt; i++) {
            double a = std::fabs((double)buf[i]);
            if (a > n.env) {
                n.env = att * n.env + (1 - att) * a;
            } else {
                double r = rel;
                if (sag > 0.0) r = std::exp(-1.0 / (rel_s * (1.0 + sag * 6.0 * n.env) * rate));
                n.env = r * n.env + (1 - r) * a;
            }
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
    } else if (t == "chorus" || t == "flanger" || t == "vibrato") {
        bool fl = (t == "flanger"), vb = (t == "vibrato");
        double rt  = n.param("rate",  fl ? 0.3 : (vb ? 5.0 : 0.8));
        double dep = n.param("depth", fl ? 2.0 : (vb ? 2.0 : 3.0));
        double base = n.param("delay", fl ? 1.0 : 14.0);
        double mix = vb ? 1.0 : n.param("mix", 0.5);
        double fb  = fl ? n.param("feedback", 0.5) : 0.0;
        size_t len = (size_t)((base + dep + 5.0) / 1000.0 * rate) + 4;
        if (n.dl.size() != len) { n.dl.assign(len, 0.0f); n.dpos = 0; }
        double inc = FX_2PI * rt / rate;
        for (unsigned i = 0; i < cnt; i++) {
            double m = 0.5 * (1.0 + std::sin(n.lfo));
            n.lfo += inc; if (n.lfo > FX_2PI) n.lfo -= FX_2PI;
            double dsamp = (base + dep * m) / 1000.0 * rate;
            double rp = (double)n.dpos - dsamp;
            while (rp < 0) rp += len;
            size_t i0 = (size_t)rp; double fr = rp - i0;
            double wet = n.dl[i0] * (1.0 - fr) + n.dl[(i0 + 1) % len] * fr;
            double in = buf[i];
            n.dl[n.dpos] = (float)(in + wet * fb);
            n.dpos = (n.dpos + 1) % len;
            // a negative mix subtracts the delayed copy instead of adding
            // it, which is the comb an out-of-phase pickup pair makes. The
            // dry side follows the amount, so both polarities stay level.
            buf[i] = (float)(in * (1.0 - std::fabs(mix)) + wet * mix);
        }
    } else if (t == "phaser") {
        double rt = n.param("rate", 0.5), oct = n.param("depth", 2.0);
        double mix = n.param("mix", 0.5), fb = n.param("feedback", 0.3);
        double base = n.param("base", 500.0);
        double inc = FX_2PI * rt / rate;
        for (unsigned i = 0; i < cnt; i++) {
            double m = 0.5 * (1.0 + std::sin(n.lfo));
            n.lfo += inc; if (n.lfo > FX_2PI) n.lfo -= FX_2PI;
            double fc = base * std::pow(2.0, oct * m);
            if (fc > rate * 0.45) fc = rate * 0.45;
            double tn = std::tan(FX_PI * fc / rate);
            double a = (1.0 - tn) / (1.0 + tn);
            double in = buf[i] + fb * n.z1;
            for (int s = 0; s < 4; s++) {
                double xp = n.ap[2 * s], yp = n.ap[2 * s + 1];
                double y = a * in + xp - a * yp;
                n.ap[2 * s] = in; n.ap[2 * s + 1] = y;
                in = y;
            }
            n.z1 = in;
            buf[i] = (float)(buf[i] * (1.0 - mix) + in * mix);
        }
    } else if (t == "tremolo") {
        double rt = n.param("rate", 5.0), dep = n.param("depth", 0.7);
        double inc = FX_2PI * rt / rate;
        for (unsigned i = 0; i < cnt; i++) {
            double m = 0.5 * (1.0 + std::sin(n.lfo));
            n.lfo += inc; if (n.lfo > FX_2PI) n.lfo -= FX_2PI;
            buf[i] = (float)(buf[i] * (1.0 - dep * m));
        }
    } else if (t == "fuzz") {
        double g = 1.0 + n.param("amount", 10.0), lvl = n.param("level", 0.6);
        for (unsigned i = 0; i < cnt; i++) {
            double v = buf[i] * g;
            if (v > 1.0) v = 1.0; else if (v < -0.8) v = -0.8;   // asymmetric hard clip
            buf[i] = (float)(v * lvl);
        }
    } else if (t == "bitcrush") {
        double levels = std::pow(2.0, n.param("bits", 8.0));
        int dv = (int)std::max(1.0, n.param("downsample", 4.0));
        double mix = n.param("mix", 1.0);
        for (unsigned i = 0; i < cnt; i++) {
            if (n.crush <= 0) { n.hold = std::round(buf[i] * levels) / levels; n.crush = dv; }
            n.crush--;
            buf[i] = (float)(buf[i] * (1.0 - mix) + n.hold * mix);
        }
    } else if (t == "octave") {
        double g = 1.0 + n.param("amount", 8.0), lvl = n.param("level", 0.5), mix = n.param("mix", 0.7);
        for (unsigned i = 0; i < cnt; i++) {
            double rec = std::tanh(std::fabs((double)buf[i]) * g);   // full-wave rectify -> octave up
            double y = rec - n.z1 + 0.995 * n.z2;                    // DC blocker
            n.z1 = rec; n.z2 = y;
            buf[i] = (float)(buf[i] * (1.0 - mix) + y * lvl * mix);
        }
    } else if (t == "autowah") {
        double base = n.param("base", 300.0), range = n.param("range", 2200.0);
        double sens = n.param("sensitivity", 8.0), Q = n.param("q", 4.0), mix = n.param("mix", 1.0);
        double att = std::exp(-1.0 / (0.005 * rate)), rel = std::exp(-1.0 / (0.05 * rate));
        for (unsigned i = 0; i < cnt; i++) {
            double av = std::fabs((double)buf[i]);
            n.env = (av > n.env) ? (att * n.env + (1 - att) * av) : (rel * n.env + (1 - rel) * av);
            double ef = n.env * sens; if (ef > 1.0) ef = 1.0;
            double fc = base + range * ef;
            double b0, b1, b2, a1, a2;
            biquad_coeffs("bandpass", fc, Q, rate, b0, b1, b2, a1, a2);
            double in = buf[i];
            double out = b0 * in + n.z1;
            n.z1 = b1 * in - a1 * out + n.z2;
            n.z2 = b2 * in - a2 * out;
            buf[i] = (float)(in * (1.0 - mix) + out * mix);
        }
    } else if (t == "noisegate") {
        double thr = n.param("threshold", 0.02);
        double att = std::exp(-1.0 / (0.001 * rate)), rel = std::exp(-1.0 / (0.08 * rate));
        for (unsigned i = 0; i < cnt; i++) {
            double av = std::fabs((double)buf[i]);
            n.env = (av > n.env) ? (att * n.env + (1 - att) * av) : (rel * n.env + (1 - rel) * av);
            double target = (n.env > thr) ? 1.0 : 0.0;
            double c = (target > n.gr) ? att : rel;            // open fast, close slow
            n.gr = c * n.gr + (1.0 - c) * target;
            buf[i] = (float)(buf[i] * n.gr);
        }
    } else if (t == "eq") {
        double b0, b1, b2, a1, a2;
        biquad_coeffs("peak", n.param("freq", 800.0), n.param("q", 1.0), rate,
                      b0, b1, b2, a1, a2, n.param("gain", 6.0));
        for (unsigned i = 0; i < cnt; i++) {
            double in = buf[i];
            double out = b0 * in + n.z1;
            n.z1 = b1 * in - a1 * out + n.z2;
            n.z2 = b2 * in - a2 * out;
            buf[i] = (float)out;
        }
    } else if (t == "reverb") {
        static const int CT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int AT[4] = {556, 441, 341, 225};
        if (n.cb.empty()) {                                    // allocate once (Freeverb-style)
            double sr = rate / 44100.0;
            for (int k = 0; k < 8; k++) n.cb.push_back(std::vector<float>((size_t)std::max(1.0, CT[k] * sr), 0.0f));
            for (int k = 0; k < 4; k++) n.apb.push_back(std::vector<float>((size_t)std::max(1.0, AT[k] * sr), 0.0f));
            n.clp.assign(8, 0.0f); n.cpos.assign(8, 0); n.appos.assign(4, 0);
        }
        double room = n.param("roomsize", 0.7), damp = n.param("damp", 0.5), mix = n.param("mix", 0.3);
        double fb = room * 0.28 + 0.7, damp1 = damp * 0.4, gain = 0.015;
        for (unsigned i = 0; i < cnt; i++) {
            double in = buf[i] * gain, acc = 0.0;
            for (int k = 0; k < 8; k++) {
                std::vector<float>& b = n.cb[k]; size_t& cp = n.cpos[k];
                double out = b[cp];
                n.clp[k] = (float)(out * (1.0 - damp1) + n.clp[k] * damp1);
                b[cp] = (float)(in + n.clp[k] * fb);
                cp = (cp + 1) % b.size();
                acc += out;
            }
            for (int k = 0; k < 4; k++) {
                std::vector<float>& b = n.apb[k]; size_t& ap = n.appos[k];
                double bo = b[ap];
                double out = -acc + bo;
                b[ap] = (float)(acc + bo * 0.5);
                ap = (ap + 1) % b.size();
                acc = out;
            }
            buf[i] = (float)(buf[i] * (1.0 - mix) + acc * mix);
        }
    }
    // unknown type: pass-through
}

// pre-populate a node's full parameter set, so a live FX.SET only ever updates an
// existing value (no map insertion -> safe to tweak while the callback reads it).
static void ensure_defaults(FxNode& n) {
    auto def = [&](const char* k, double v) { if (n.p.find(k) == n.p.end()) n.p[k] = v; };
    const std::string& t = n.type;
    if      (t == "gain")       { def("gain", 1.0); }
    else if (t == "drive")      { def("amount", 5.0); def("level", 0.7); def("asym", 0.0); def("bloom", 0.0); def("recover", 0.25); }
    else if (t == "lowpass" || t == "highpass") { def("cutoff", 2000.0); def("q", 0.707); }
    else if (t == "delay")      { def("time_ms", 300.0); def("feedback", 0.35); def("mix", 0.3); def("dry", 1.0); }
    else if (t == "compressor") { def("threshold", 0.5); def("ratio", 4.0); def("makeup", 1.0); def("attack", 0.005); def("release", 0.1); def("sag", 0.0); }
    else if (t == "cabinet")    { def("level", 0.7); def("mix", 1.0); }
    else if (t == "chorus")     { def("rate", 0.8); def("depth", 3.0); def("delay", 14.0); def("mix", 0.5); }
    else if (t == "flanger")    { def("rate", 0.3); def("depth", 2.0); def("delay", 1.0); def("mix", 0.5); def("feedback", 0.5); }
    else if (t == "vibrato")    { def("rate", 5.0); def("depth", 2.0); def("delay", 14.0); }
    else if (t == "phaser")     { def("rate", 0.5); def("depth", 2.0); def("base", 500.0); def("mix", 0.5); def("feedback", 0.3); }
    else if (t == "tremolo")    { def("rate", 5.0); def("depth", 0.7); }
    else if (t == "fuzz")       { def("amount", 10.0); def("level", 0.6); }
    else if (t == "bitcrush")   { def("bits", 8.0); def("downsample", 4.0); def("mix", 1.0); }
    else if (t == "octave")     { def("amount", 8.0); def("level", 0.5); def("mix", 0.7); }
    else if (t == "autowah")    { def("base", 300.0); def("range", 2200.0); def("sensitivity", 8.0); def("q", 4.0); def("mix", 1.0); }
    else if (t == "noisegate")  { def("threshold", 0.02); }
    else if (t == "eq")         { def("freq", 800.0); def("q", 1.0); def("gain", 6.0); }
    else if (t == "reverb")     { def("roomsize", 0.7); def("damp", 0.5); def("mix", 0.3); }
}

// bus 1 and 2 are the split legs, everything else is the common section
static std::vector<FxNode>* bus_nodes(FxChain& c, int bus) {
    if (bus == 1) return &c.a.nodes;
    if (bus == 2) return &c.b.nodes;
    return &c.nodes;
}

static FxBranch* bus_branch(FxChain& c, int bus) {
    if (bus == 1) return &c.a;
    if (bus == 2) return &c.b;
    return nullptr;
}

// constant-power pan: a centred branch keeps its loudness when panned out
static void pan_gains(double pan, double& gl, double& gr) {
    if (pan < -1.0) pan = -1.0; else if (pan > 1.0) pan = 1.0;
    double a = (pan + 1.0) * (FX_PI / 4.0);
    gl = std::cos(a);
    gr = std::sin(a);
}

// run one section over a block, skipping the nodes a caller cannot afford
static void run_section(std::vector<FxNode>& nodes, float* buf, unsigned cnt,
                        double rate, bool skip_cabinet) {
    for (auto& n : nodes)
        if (!skip_cabinet || n.type != "cabinet") process_node(n, buf, cnt, rate);
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
    // samples are interleaved: frame f channel c is samples[f * channels + c].
    vm.register_native("WAV.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        WavFmt w;
        std::vector<float> samples;
        if (!load_wav(args[0].to_string(), w, samples)) return Value::make_none();
        Value arr = Value::make_array();
        auto* el = arr.as_array();
        el->elements.reserve(samples.size());
        for (float s : samples) el->elements.push_back(Value::make_f64(s));
        Value m = Value::make_object();
        m.as_object()->set("samples", std::move(arr));
        m.as_object()->set("rate", Value::make_i64(w.rate));
        m.as_object()->set("channels", Value::make_i64(w.channels));
        m.as_object()->set("frames", Value::make_i64((int64_t)(samples.size() / w.channels)));
        return m;
    });

    // WAV.INFO(path$) -> { rate, channels, bits, frames } or NONE
    // Header only, so it stays cheap on long files.
    vm.register_native("WAV.INFO", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::ifstream f(args[0].to_string(), std::ios::binary);
        if (!f) return Value::make_none();
        f.seekg(0, std::ios::end);
        std::streamoff total = f.tellg();
        f.seekg(0, std::ios::beg);
        unsigned char head[4096];
        f.read((char*)head, sizeof(head));
        WavFmt w = scan_wav(head, (size_t)f.gcount());
        if (!w.ok) return Value::make_none();
        size_t len = 0;
        if (w.data_off > 0 && total > 0 && (std::streamoff)w.data_off < total) {
            len = w.data_len;
            if (w.data_off + len > (size_t)total) len = (size_t)total - w.data_off;
        }
        Value m = Value::make_object();
        m.as_object()->set("rate", Value::make_i64(w.rate));
        m.as_object()->set("channels", Value::make_i64(w.channels));
        m.as_object()->set("bits", Value::make_i64(w.bits));
        m.as_object()->set("frames",
            Value::make_i64((int64_t)(len / ((size_t)w.bits / 8 * w.channels))));
        return m;
    });

    // ---- FX chain builtins ----

    // FX.NEW() -> chain handle
    vm.register_native("FX.NEW", 0, 0, [](const std::vector<Value>&) -> Value {
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        int h = g_chain_next++;
        FxChain c;
        c.sa.assign(FX_RT_MAX_BLOCK, 0.0f);
        c.sb.assign(FX_RT_MAX_BLOCK, 0.0f);
        g_chains[h] = std::move(c);
        return Value::make_i64(h);
    });

    // FX.ADD(chain, type$, [params{}], [bus=0]) -> bool
    // types: gain{gain} drive{amount,level} lowpass/highpass{cutoff,q}
    //        delay{time_ms,feedback,mix} compressor{threshold,ratio,makeup}
    // bus 0 is the common section, 1 and 2 are the split legs.
    vm.register_native("FX.ADD", 2, 4, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        int bus = args.size() >= 4 ? (int)args[3].to_int() : 0;
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
        bus_nodes(it->second, bus)->push_back(std::move(node));
        return Value::make_bool(true);
    });

    // FX.SPLIT(chain, on) -> bool   feed the two branches instead of one signal
    vm.register_native("FX.SPLIT", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        auto it = g_chains.find((int)args[0].to_int());
        if (it == g_chains.end()) return Value::make_bool(false);
        it->second.split = args[1].to_bool();
        return Value::make_bool(true);
    });

    // FX.MIX(chain, bus, level, pan) -> bool   where a branch lands in the mix.
    // pan -1 is hard left, +1 hard right, so two amps are pan -1 and +1.
    vm.register_native("FX.MIX", 4, 4, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        auto it = g_chains.find((int)args[0].to_int());
        if (it == g_chains.end()) return Value::make_bool(false);
        FxBranch* br = bus_branch(it->second, (int)args[1].to_int());
        if (!br) return Value::make_bool(false);
        br->level = args[2].to_double();
        br->pan   = args[3].to_double();
        return Value::make_bool(true);
    });

    // FX.PROCESS(chain, samples[], [rate=44100], [channels=1]) -> samples[]
    // Offline. With channels 2 the result is interleaved stereo, and a chain
    // with the split section on renders its two branches into it.
    vm.register_native("FX.PROCESS", 2, 4, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int();
        std::vector<float> buf;
        if (!collect_floats(args[1], buf)) return Value::make_array();
        double rate = args.size() >= 3 ? args[2].to_double() : 44100.0;
        if (rate <= 0) rate = 44100.0;
        int ch = args.size() >= 4 ? (int)args[3].to_int() : 1;
        if (ch < 1) ch = 1;

        std::vector<float> outbuf;
        {
            std::lock_guard<std::mutex> lk(g_chain_mtx);
            auto it = g_chains.find(h);
            unsigned cnt = (unsigned)buf.size();
            if (it == g_chains.end()) {
                outbuf.assign((size_t)cnt * ch, 0.0f);
                for (unsigned i = 0; i < cnt; i++)
                    for (int c = 0; c < ch; c++) outbuf[(size_t)i * ch + c] = buf[i];
            } else {
                FxChain& fc = it->second;
                run_section(fc.nodes, buf.data(), cnt, rate, false);
                outbuf.assign((size_t)cnt * ch, 0.0f);
                if (!fc.split) {
                    for (unsigned i = 0; i < cnt; i++)
                        for (int c = 0; c < ch; c++) outbuf[(size_t)i * ch + c] = buf[i];
                } else {
                    std::vector<float> bbuf(buf);
                    run_section(fc.a.nodes, buf.data(), cnt, rate, false);
                    run_section(fc.b.nodes, bbuf.data(), cnt, rate, false);
                    double alv = fc.a.level, blv = fc.b.level;
                    if (ch < 2) {
                        for (unsigned i = 0; i < cnt; i++)
                            outbuf[i] = (float)(buf[i] * alv + bbuf[i] * blv);
                    } else {
                        double al, ar, bl, br;
                        pan_gains(fc.a.pan, al, ar);
                        pan_gains(fc.b.pan, bl, br);
                        for (unsigned i = 0; i < cnt; i++) {
                            float l = (float)(buf[i] * alv * al + bbuf[i] * blv * bl);
                            float r = (float)(buf[i] * alv * ar + bbuf[i] * blv * br);
                            outbuf[(size_t)i * ch]     = l;
                            outbuf[(size_t)i * ch + 1] = r;
                            for (int c = 2; c < ch; c++) outbuf[(size_t)i * ch + c] = 0.5f * (l + r);
                        }
                    }
                }
            }
        }
        Value out = Value::make_array();
        out.as_array()->elements.reserve(outbuf.size());
        for (float f : outbuf) out.as_array()->elements.push_back(Value::make_f64(f));
        return out;
    });

    // FX.FREE(chain)
    vm.register_native("FX.FREE", 1, 1, [](const std::vector<Value>& args) -> Value {
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        g_chains.erase((int)args[0].to_int());
        return Value::make_none();
    });

    // FX.SET(chain, nodeIndex, param$, value, [bus=0]) -> bool   tweak LIVE.
    // Only updates an existing param (RT-safe: no map insertion while monitoring).
    // nodeIndex counts within its own bus.
    vm.register_native("FX.SET", 4, 5, [](const std::vector<Value>& args) -> Value {
        int h = (int)args[0].to_int(), idx = (int)args[1].to_int();
        int bus = args.size() >= 5 ? (int)args[4].to_int() : 0;
        std::string key = args[2].to_string();
        double val = args[3].to_double();
        std::lock_guard<std::mutex> lk(g_chain_mtx);
        auto it = g_chains.find(h);
        if (it == g_chains.end()) return Value::make_bool(false);
        auto* nodes = bus_nodes(it->second, bus);
        if (idx < 0 || idx >= (int)nodes->size()) return Value::make_bool(false);
        auto& p = (*nodes)[idx].p;
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
        FxChain& fc = it->second;
        std::string s;
        char buf[80];
        auto dump = [&](const char* title, std::vector<FxNode>& nodes) {
            s += title;
            s += "\n";
            for (size_t i = 0; i < nodes.size(); i++) {
                snprintf(buf, sizeof(buf), "  %u: %-10s", (unsigned)i, nodes[i].type.c_str());
                s += buf;
                for (auto& kv : nodes[i].p) {
                    snprintf(buf, sizeof(buf), " %s=%g", kv.first.c_str(), kv.second);
                    s += buf;
                }
                s += "\n";
            }
        };
        dump(fc.split ? "main (before the split)" : "main", fc.nodes);
        if (fc.split) {
            snprintf(buf, sizeof(buf), "branch A  level=%g pan=%g", fc.a.level, fc.a.pan);
            dump(buf, fc.a.nodes);
            snprintf(buf, sizeof(buf), "branch B  level=%g pan=%g", fc.b.level, fc.b.pan);
            dump(buf, fc.b.nodes);
        }
        return Value::make_string(s);
    });

    // array-taking builtins must not auto-vectorize (would run per element)
    vm.extra_no_vectorize.insert("WAV.WRITE");
    vm.extra_no_vectorize.insert("FX.PROCESS");
}

// realtime block processing (called from the audio callback). No lock: the chain
// must not be mutated while monitoring. Cabinet is skipped (it allocates), and
// so is any block larger than the scratch buffers, which are sized on FX.NEW.
void fx_process_chain_rt(int handle, const float* in, float* out,
                         unsigned frames, unsigned outCh, double rate) {
    auto spread_dry = [&]() {
        for (unsigned i = 0; i < frames; i++)
            for (unsigned c = 0; c < outCh; c++) out[(size_t)i * outCh + c] = in[i];
    };
    auto it = g_chains.find(handle);
    if (it == g_chains.end()) { spread_dry(); return; }
    FxChain& fc = it->second;
    if (frames > fc.sa.size() || frames > fc.sb.size()) { spread_dry(); return; }

    float* am = fc.sa.data();
    for (unsigned i = 0; i < frames; i++) am[i] = in[i];
    run_section(fc.nodes, am, frames, rate, true);

    if (!fc.split) {
        for (unsigned i = 0; i < frames; i++)
            for (unsigned c = 0; c < outCh; c++) out[(size_t)i * outCh + c] = am[i];
        return;
    }

    float* bm = fc.sb.data();
    for (unsigned i = 0; i < frames; i++) bm[i] = am[i];
    run_section(fc.a.nodes, am, frames, rate, true);
    run_section(fc.b.nodes, bm, frames, rate, true);

    double alv = fc.a.level, blv = fc.b.level;
    // a single output channel still hears both branches, just without the image
    if (outCh < 2) {
        for (unsigned i = 0; i < frames; i++)
            out[i] = (float)(am[i] * alv + bm[i] * blv);
        return;
    }
    double al, ar, bl, br;
    pan_gains(fc.a.pan, al, ar);
    pan_gains(fc.b.pan, bl, br);
    for (unsigned i = 0; i < frames; i++) {
        float l = (float)(am[i] * alv * al + bm[i] * blv * bl);
        float r = (float)(am[i] * alv * ar + bm[i] * blv * br);
        float* o = out + (size_t)i * outCh;
        o[0] = l;
        o[1] = r;
        for (unsigned c = 2; c < outCh; c++) o[c] = 0.5f * (l + r);
    }
}

#else  // !FX

void register_audiofx_builtins(VM&) {}
void fx_process_chain_rt(int, const float* in, float* out,
                         unsigned frames, unsigned outCh, double) {
    for (unsigned i = 0; i < frames; i++)
        for (unsigned c = 0; c < outCh; c++) out[(size_t)i * outCh + c] = in ? in[i] : 0.0f;
}

#endif
