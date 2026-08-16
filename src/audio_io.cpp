#include "audio_io.h"
#include "vm.h"
#include "value.h"

#ifdef MINIAUDIO

// Single-header build. We only need device enumeration + raw PCM I/O, so trim
// the decoders/encoders/generators to keep the compile lean.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

#include "audio_fx.h"
#include <string>
#include <vector>
#include <atomic>
#include <cstring>
#include <cctype>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <thread>
#include <chrono>

namespace {
ma_device          g_mon_device;
std::atomic<bool>  g_mon_active{false};
std::atomic<float> g_mon_gain{1.0f};
std::atomic<int>   g_rt_chain{0};      // active realtime FX chain (0 = passthrough)
std::atomic<float> g_in_peak{0.0f};    // decaying input / output peak for metering
std::atomic<float> g_out_peak{0.0f};

const unsigned     SCOPE_N = 4096;     // power of two: sample rings for scope + tuner
float              g_scope[SCOPE_N] = {0};      // post-FX output (oscilloscope)
float              g_scope_in[SCOPE_N] = {0};   // dry input (pitch detection)
std::atomic<unsigned> g_scope_pos{0};
std::atomic<unsigned> g_scope_in_pos{0};

const unsigned     OUT_CH  = 2;               // the monitor plays back in stereo
const unsigned     REC_CAP = 48000u * 180u * OUT_CH;   // 3 min stereo record buffer
std::vector<float> g_rec;                      // pre-sized once; callback never reallocs
std::atomic<unsigned> g_rec_pos{0};
std::atomic<bool>  g_recording{false};

float              g_dry[FX_RT_MAX_BLOCK];    // mono input block handed to the chain

// realtime: input -> gain -> FX chain -> output. NO allocation, NO locks, NO VM.
void mon_callback(ma_device* dev, void* pOut, const void* pIn, ma_uint32 frames) {
    unsigned outCh = dev->playback.channels;
    const float* in = (const float*)pIn;
    float* out = (float*)pOut;
    if (frames > FX_RT_MAX_BLOCK) frames = FX_RT_MAX_BLOCK;
    unsigned n = frames * outCh;
    float g = g_mon_gain.load(std::memory_order_relaxed);
    // Sum the capture channels down to mono so the guitar is heard on either
    // physical input (e.g. XLR on ch1 or the instrument jack on ch2), instead
    // of only picking channel 0.
    unsigned inCh = dev->capture.channels;
    for (unsigned i = 0; i < frames; i++) {
        float smp = 0.0f;
        for (unsigned c = 0; c < inCh; c++) smp += in[i * inCh + c];
        g_dry[i] = smp * g;
    }
    int chain = g_rt_chain.load(std::memory_order_relaxed);
    if (chain > 0) {
        fx_process_chain_rt(chain, g_dry, out, frames, outCh, 48000.0);
    } else {
        for (unsigned i = 0; i < frames; i++)
            for (unsigned c = 0; c < outCh; c++) out[i * outCh + c] = g_dry[i];
    }
    float ip = 0.0f, op = 0.0f;
    for (unsigned i = 0; i < frames; i++) { float a = g_dry[i]; if (a < 0) a = -a; if (a > ip) ip = a; }
    for (unsigned i = 0; i < n; i++)      { float b = out[i]; if (b < 0) b = -b; if (b > op) op = b; }
    float pin = g_in_peak.load(std::memory_order_relaxed) * 0.92f;  if (ip > pin) pin = ip;
    float pout = g_out_peak.load(std::memory_order_relaxed) * 0.92f; if (op > pout) pout = op;
    g_in_peak.store(pin, std::memory_order_relaxed);
    g_out_peak.store(pout, std::memory_order_relaxed);
    // the scope shows what you actually hear, so a split chain is summed down
    unsigned sp = g_scope_pos.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < frames; i++) {
        float s = 0.0f;
        for (unsigned c = 0; c < outCh; c++) s += out[i * outCh + c];
        g_scope[(sp + i) & (SCOPE_N - 1)] = s / (float)outCh;
    }
    g_scope_pos.store(sp + frames, std::memory_order_relaxed);
    unsigned sip = g_scope_in_pos.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < frames; i++) g_scope_in[(sip + i) & (SCOPE_N - 1)] = g_dry[i];
    g_scope_in_pos.store(sip + frames, std::memory_order_relaxed);
    if (g_recording.load(std::memory_order_relaxed)) {
        unsigned rp = g_rec_pos.load(std::memory_order_relaxed);
        unsigned cap = (unsigned)g_rec.size();
        for (unsigned i = 0; i < frames && rp + OUT_CH <= cap; i++) {
            g_rec[rp++] = out[i * outCh];
            g_rec[rp++] = out[i * outCh + (outCh > 1 ? 1 : 0)];
        }
        g_rec_pos.store(rp, std::memory_order_relaxed);
    }
}

// ---- WAV.RECORD: raw capture on its own device, independent of the monitor ----

ma_device           g_cap_device;
std::atomic<bool>   g_cap_active{false};
std::vector<float>  g_cap_buf;                 // interleaved, sized before start
std::atomic<size_t> g_cap_pos{0};
unsigned            g_cap_ch   = 1;
unsigned            g_cap_rate = 48000;
std::vector<float>  g_play_buf;                // played while capturing (duplex)
std::atomic<size_t> g_play_pos{0};

// realtime: raw input into the buffer, optional pre-rendered output. Stops
// filling at the end of the buffer instead of wrapping - a measurement wants
// the first N seconds, not the last.
void cap_callback(ma_device* dev, void* pOut, const void* pIn, ma_uint32 frames) {
    if (pIn) {
        const float* in = (const float*)pIn;
        size_t n = (size_t)frames * dev->capture.channels;
        size_t p = g_cap_pos.load(std::memory_order_relaxed);
        size_t cap = g_cap_buf.size();
        if (p < cap) {
            if (n > cap - p) n = cap - p;
            std::memcpy(g_cap_buf.data() + p, in, n * sizeof(float));
            g_cap_pos.store(p + n, std::memory_order_relaxed);
        }
    }
    if (pOut) {
        float* out = (float*)pOut;
        size_t n = (size_t)frames * dev->playback.channels;
        size_t p = g_play_pos.load(std::memory_order_relaxed);
        size_t have = (p < g_play_buf.size()) ? g_play_buf.size() - p : 0;
        if (have > n) have = n;
        if (have) std::memcpy(out, g_play_buf.data() + p, have * sizeof(float));
        if (have < n) std::memset(out + have, 0, (n - have) * sizeof(float));
        g_play_pos.store(p + n, std::memory_order_relaxed);
    }
}

struct RecOpts {
    unsigned channels = 1;
    unsigned rate     = 48000;
    double   seconds  = 60.0;
    int      device      = -1;
    int      playdevice  = -1;
    unsigned playchannels = 1;
    std::vector<float> play;
};

const Value* opt_get(const Value& m, const char* key) {
    if (m.type != ValueType::OBJECT) return nullptr;
    size_t klen = std::strlen(key);
    for (auto& kv : m.as_object()->fields) {
        if (kv.first.size() != klen) continue;
        size_t i = 0;
        while (i < klen && std::tolower((unsigned char)kv.first[i]) ==
                           std::tolower((unsigned char)key[i])) i++;
        if (i == klen) return &kv.second;
    }
    return nullptr;
}

RecOpts parse_opts(const std::vector<Value>& args, size_t idx) {
    RecOpts o;
    if (args.size() <= idx || args[idx].type != ValueType::OBJECT) return o;
    const Value& m = args[idx];
    if (const Value* v = opt_get(m, "channels"))     o.channels = (unsigned)v->to_int();
    if (const Value* v = opt_get(m, "rate"))         o.rate = (unsigned)v->to_int();
    if (const Value* v = opt_get(m, "seconds"))      o.seconds = v->to_double();
    if (const Value* v = opt_get(m, "device"))       o.device = (int)v->to_int();
    if (const Value* v = opt_get(m, "playdevice"))   o.playdevice = (int)v->to_int();
    if (const Value* v = opt_get(m, "playchannels")) o.playchannels = (unsigned)v->to_int();
    if (const Value* v = opt_get(m, "play")) {
        if (v->type == ValueType::ARRAY)
            for (auto& e : v->as_array()->elements) o.play.push_back((float)e.to_double());
    }
    if (o.channels < 1) o.channels = 1;
    if (o.playchannels < 1) o.playchannels = 1;
    if (o.rate < 1000) o.rate = 48000;
    return o;
}

bool find_device_id(bool capture, int idx, ma_device_id& out) {
    ma_context ctx;
    bool found = false;
    if (ma_context_init(NULL, 0, NULL, &ctx) == MA_SUCCESS) {
        ma_device_info* p = nullptr; ma_uint32 pc = 0;
        ma_device_info* c = nullptr; ma_uint32 cc = 0;
        if (ma_context_get_devices(&ctx, &p, &pc, &c, &cc) == MA_SUCCESS) {
            ma_device_info* list = capture ? c : p;
            ma_uint32 n = capture ? cc : pc;
            if (idx >= 0 && (ma_uint32)idx < n) { out = list[idx].id; found = true; }
        }
        ma_context_uninit(&ctx);
    }
    return found;
}

bool rec_start(const RecOpts& o) {
    if (g_cap_active.load()) return false;
    bool duplex = !o.play.empty();
    ma_device_config cfg = ma_device_config_init(
        duplex ? ma_device_type_duplex : ma_device_type_capture);
    cfg.capture.format   = ma_format_f32;
    cfg.capture.channels  = o.channels;
    cfg.sampleRate        = o.rate;
    cfg.dataCallback      = cap_callback;
    ma_device_id inId, outId;
    if (o.device >= 0 && find_device_id(true, o.device, inId)) cfg.capture.pDeviceID = &inId;
    if (duplex) {
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = o.playchannels;
        if (o.playdevice >= 0 && find_device_id(false, o.playdevice, outId))
            cfg.playback.pDeviceID = &outId;
    }
    if (ma_device_init(NULL, &cfg, &g_cap_device) != MA_SUCCESS) return false;
    // the device may not grant what was asked for; the buffer follows what it gave
    g_cap_ch   = g_cap_device.capture.channels;
    g_cap_rate = g_cap_device.sampleRate;
    double secs = o.seconds > 0.0 ? o.seconds : 60.0;
    size_t cap = (size_t)(secs * g_cap_rate) * g_cap_ch;
    if (cap == 0) { ma_device_uninit(&g_cap_device); return false; }
    g_cap_buf.assign(cap, 0.0f);
    g_cap_pos.store(0, std::memory_order_relaxed);
    g_play_buf = o.play;
    g_play_pos.store(0, std::memory_order_relaxed);
    if (ma_device_start(&g_cap_device) != MA_SUCCESS) {
        ma_device_uninit(&g_cap_device);
        return false;
    }
    g_cap_active.store(true);
    return true;
}

void rec_stop() {
    if (g_cap_active.exchange(false)) ma_device_uninit(&g_cap_device);
    g_play_buf.clear();
}

// the same map shape WAV.READ returns, so both feed WAV.WRITE and FFT alike
Value rec_result() {
    size_t n = g_cap_pos.load(std::memory_order_relaxed);
    if (n > g_cap_buf.size()) n = g_cap_buf.size();
    Value arr = Value::make_array();
    arr.as_array()->elements.reserve(n);
    for (size_t i = 0; i < n; i++) arr.as_array()->elements.push_back(Value::make_f64(g_cap_buf[i]));
    Value m = Value::make_object();
    m.as_object()->set("samples", std::move(arr));
    m.as_object()->set("rate", Value::make_i64(g_cap_rate));
    m.as_object()->set("channels", Value::make_i64(g_cap_ch));
    m.as_object()->set("frames", Value::make_i64((int64_t)(n / g_cap_ch)));
    return m;
}
} // namespace

void register_audioio_builtins(VM& vm) {

    // MON.DEVICES() -> { playback:[name,...], capture:[name,...] }
    vm.register_native("MON.DEVICES", 0, 0, [](const std::vector<Value>&) -> Value {
        Value m = Value::make_object();
        Value pb = Value::make_array(), cap = Value::make_array();
        ma_context ctx;
        if (ma_context_init(NULL, 0, NULL, &ctx) == MA_SUCCESS) {
            ma_device_info* pInfos = nullptr; ma_uint32 pCount = 0;
            ma_device_info* cInfos = nullptr; ma_uint32 cCount = 0;
            if (ma_context_get_devices(&ctx, &pInfos, &pCount, &cInfos, &cCount) == MA_SUCCESS) {
                for (ma_uint32 i = 0; i < pCount; i++)
                    pb.as_array()->elements.push_back(Value::make_string(pInfos[i].name));
                for (ma_uint32 i = 0; i < cCount; i++)
                    cap.as_array()->elements.push_back(Value::make_string(cInfos[i].name));
            }
            ma_context_uninit(&ctx);
        }
        m.as_object()->set("playback", std::move(pb));
        m.as_object()->set("capture", std::move(cap));
        return m;
    });

    // MON.BACKEND$() -> the audio backend miniaudio chose (diagnostics)
    vm.register_native("MON.BACKEND$", 0, 0, [](const std::vector<Value>&) -> Value {
        ma_context ctx;
        std::string name = "none";
        if (ma_context_init(NULL, 0, NULL, &ctx) == MA_SUCCESS) {
            name = ma_get_backend_name(ctx.backend);
            ma_context_uninit(&ctx);
        }
        return Value::make_string(name);
    });

    // MON.START([captureIdx], [playbackIdx]) -> bool   live input -> output passthrough
    // omit indices for system default devices. WARNING: mic -> speakers will howl;
    // use a line/instrument input -> headphones.
    vm.register_native("MON.START", 0, 2, [](const std::vector<Value>& args) -> Value {
        if (g_mon_active.load()) return Value::make_bool(true);
        ma_device_id inId, outId; bool haveIn = false, haveOut = false;
        if (!args.empty()) {
            ma_context ctx;
            if (ma_context_init(NULL, 0, NULL, &ctx) == MA_SUCCESS) {
                ma_device_info* p = nullptr; ma_uint32 pc = 0;
                ma_device_info* c = nullptr; ma_uint32 cc = 0;
                if (ma_context_get_devices(&ctx, &p, &pc, &c, &cc) == MA_SUCCESS) {
                    int ii = (int)args[0].to_int();
                    if (ii >= 0 && (ma_uint32)ii < cc) { inId = c[ii].id; haveIn = true; }
                    if (args.size() >= 2) {
                        int oi = (int)args[1].to_int();
                        if (oi >= 0 && (ma_uint32)oi < pc) { outId = p[oi].id; haveOut = true; }
                    }
                }
                ma_context_uninit(&ctx);
            }
        }
        ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
        cfg.capture.format   = ma_format_f32; cfg.capture.channels  = 2;
        cfg.playback.format  = ma_format_f32; cfg.playback.channels = OUT_CH;
        cfg.sampleRate        = 48000;
        // 256 frames x 3 periods is the stable sweet spot: small enough to feel
        // responsive, deep enough that a cross-device (interface in, HDMI out)
        // PulseAudio duplex does not under-run and swallow pick transients.
        cfg.periodSizeInFrames = 256;
        cfg.periods            = 3;
        cfg.performanceProfile = ma_performance_profile_low_latency;
        cfg.dataCallback      = mon_callback;
        if (haveIn)  cfg.capture.pDeviceID  = &inId;
        if (haveOut) cfg.playback.pDeviceID = &outId;
        if (ma_device_init(NULL, &cfg, &g_mon_device) != MA_SUCCESS) return Value::make_bool(false);
        if (ma_device_start(&g_mon_device) != MA_SUCCESS) { ma_device_uninit(&g_mon_device); return Value::make_bool(false); }
        g_mon_active.store(true);
        return Value::make_bool(true);
    });

    // MON.STOP()
    vm.register_native("MON.STOP", 0, 0, [](const std::vector<Value>&) -> Value {
        if (g_mon_active.exchange(false)) ma_device_uninit(&g_mon_device);
        g_in_peak.store(0.0f, std::memory_order_relaxed);
        g_out_peak.store(0.0f, std::memory_order_relaxed);
        return Value::make_none();
    });

    // MON.LEVEL() -> { in, out }  decaying peak levels (0..1+) for VU metering
    vm.register_native("MON.LEVEL", 0, 0, [](const std::vector<Value>&) -> Value {
        Value m = Value::make_object();
        m.as_object()->set("in",  Value::make_f64(g_in_peak.load(std::memory_order_relaxed)));
        m.as_object()->set("out", Value::make_f64(g_out_peak.load(std::memory_order_relaxed)));
        return m;
    });

    // MON.SCOPE([count=1024]) -> the last `count` output samples (oldest..newest)
    // for an oscilloscope / FFT spectrum. Reads without a lock: minor tearing is fine.
    vm.register_native("MON.SCOPE", 0, 1, [](const std::vector<Value>& args) -> Value {
        int cnt = args.empty() ? 1024 : (int)args[0].to_int();
        if (cnt < 1) cnt = 1;
        if (cnt > (int)SCOPE_N) cnt = (int)SCOPE_N;
        unsigned sp = g_scope_pos.load(std::memory_order_relaxed);
        Value arr = Value::make_array();
        arr.as_array()->elements.reserve(cnt);
        for (int i = cnt; i > 0; i--)
            arr.as_array()->elements.push_back(Value::make_f64(g_scope[(sp - (unsigned)i) & (SCOPE_N - 1)]));
        return arr;
    });

    // MON.PITCH() -> detected fundamental of the DRY input in Hz, or 0 if no clear
    // note / too quiet. Autocorrelation with sub-octave guard + parabolic refine.
    vm.register_native("MON.PITCH", 0, 0, [](const std::vector<Value>&) -> Value {
        const int N = 2048;
        static float buf[N];
        unsigned sp = g_scope_in_pos.load(std::memory_order_relaxed);
        for (int i = 0; i < N; i++)
            buf[i] = g_scope_in[(sp - (unsigned)(N - i)) & (SCOPE_N - 1)];
        double mean = 0.0;
        for (int i = 0; i < N; i++) mean += buf[i];
        mean /= N;
        double msq = 0.0;
        for (int i = 0; i < N; i++) { buf[i] = (float)(buf[i] - mean); msq += (double)buf[i] * buf[i]; }
        msq /= N;
        if (msq < 0.000001) return Value::make_f64(0.0);        // below noise floor; the thin high e is very quiet (nconf below guards noise)
        const double rate = 48000.0;
        int minTau = (int)(rate / 1000.0);                      // up to 1000 Hz
        int maxTau = (int)(rate / 70.0);                        // down to 70 Hz
        int M = N - maxTau;                                     // common window, comparable taus
        static double rbuf[1100];
        double r0 = 0.0;
        for (int i = 0; i < M; i++) r0 += (double)buf[i] * buf[i];
        if (r0 <= 0.0) return Value::make_f64(0.0);
        double bestR = 0.0;
        for (int tau = minTau; tau <= maxTau; tau++) {
            double r = 0.0;
            for (int i = 0; i < M; i++) r += (double)buf[i] * buf[i + tau];
            rbuf[tau] = r;
            if (r > bestR) bestR = r;
        }
        if (bestR <= 0.0) return Value::make_f64(0.0);
        double thr = 0.93 * bestR;                              // earliest strong peak = fundamental
        int tsel = -1;
        for (int tau = minTau; tau <= maxTau; tau++)
            if (rbuf[tau] >= thr) { tsel = tau; break; }
        if (tsel < 0) return Value::make_f64(0.0);
        // normalized cross-correlation at the chosen lag: robust to fast string
        // decay (the high e drops in amplitude across the window) - the raw
        // bestR/r0 ratio would wrongly reject it.
        double e2 = 0.0;
        for (int i = 0; i < M; i++) e2 += (double)buf[i + tsel] * buf[i + tsel];
        double nconf = (e2 > 0.0) ? rbuf[tsel] / std::sqrt(r0 * e2) : 0.0;
        if (nconf < 0.5) return Value::make_f64(0.0);
        double tau = tsel;
        if (tsel > minTau && tsel < maxTau) {
            double rm = rbuf[tsel - 1], rc = rbuf[tsel], rp = rbuf[tsel + 1];
            double denom = rm - 2 * rc + rp;
            if (denom != 0.0) tau = tsel + 0.5 * (rm - rp) / denom;
        }
        return Value::make_f64(rate / tau);
    });

    // MON.RECSTART() -> bool   start tapping the wet output into the record buffer
    vm.register_native("MON.RECSTART", 0, 0, [](const std::vector<Value>&) -> Value {
        if (g_rec.size() < REC_CAP) g_rec.assign(REC_CAP, 0.0f);
        g_rec_pos.store(0, std::memory_order_relaxed);
        g_recording.store(true, std::memory_order_relaxed);
        return Value::make_bool(true);
    });

    // MON.RECLEN() -> seconds captured so far (for the GUI)
    vm.register_native("MON.RECLEN", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_f64(g_rec_pos.load(std::memory_order_relaxed) / (48000.0 * OUT_CH));
    });

    // MON.RECSTOP(path$) -> seconds written   stop and save a 16-bit stereo 48k WAV
    vm.register_native("MON.RECSTOP", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_recording.store(false, std::memory_order_relaxed);
        unsigned cnt = g_rec_pos.load(std::memory_order_relaxed);
        if (cnt > (unsigned)g_rec.size()) cnt = (unsigned)g_rec.size();
        std::ofstream f(args[0].to_string(), std::ios::binary);
        if (!f) return Value::make_f64(0.0);
        uint16_t blockAlign = (uint16_t)(OUT_CH * 2);
        uint32_t rate = 48000, dataBytes = cnt * 2, byteRate = rate * blockAlign;
        auto w32 = [&](uint32_t v) { f.put((char)(v & 0xff)); f.put((char)((v >> 8) & 0xff)); f.put((char)((v >> 16) & 0xff)); f.put((char)((v >> 24) & 0xff)); };
        auto w16 = [&](uint16_t v) { f.put((char)(v & 0xff)); f.put((char)((v >> 8) & 0xff)); };
        f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVE", 4);
        f.write("fmt ", 4); w32(16); w16(1); w16((uint16_t)OUT_CH); w32(rate); w32(byteRate); w16(blockAlign); w16(16);
        f.write("data", 4); w32(dataBytes);
        for (unsigned i = 0; i < cnt; i++) {
            float s = g_rec[i];
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            w16((uint16_t)(int16_t)(s * 32767.0f));
        }
        return Value::make_f64(cnt / (48000.0 * OUT_CH));
    });

    // MON.GAIN(g)  monitor level, applied lock-free in the audio callback
    vm.register_native("MON.GAIN", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_mon_gain.store((float)args[0].to_double(), std::memory_order_relaxed);
        return Value::make_none();
    });

    // MON.RUNNING() -> bool
    vm.register_native("MON.RUNNING", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(g_mon_active.load());
    });

    // MON.FX(chainHandle)  route the live monitor through an FX chain (0 = bypass).
    // Can be called before or during monitoring; swaps are picked up next block.
    // Do not FX.ADD to a chain while it is the active monitor chain - build a new
    // one and MON.FX to it. Cabinet nodes are skipped on the live path.
    vm.register_native("MON.FX", 1, 1, [](const std::vector<Value>& args) -> Value {
        int chain = (int)args[0].to_int();
        if (chain > 0) {
            float dry[256] = { 0.0f };           // pre-size delay lines off the audio thread
            float warm[256 * OUT_CH] = { 0.0f };
            fx_process_chain_rt(chain, dry, warm, 256, OUT_CH, 48000.0);
        }
        g_rt_chain.store(chain, std::memory_order_relaxed);
        return Value::make_none();
    });

    // WAV.RECORD(seconds, [opts{}]) -> { samples, rate, channels, frames } or NONE
    // Blocks until the buffer is full. opts: channels, rate, device (index into
    // MON.DEVICES().capture), play (samples output while capturing - one duplex
    // device, so the two stay in step), playchannels, playdevice.
    vm.register_native("WAV.RECORD", 1, 2, [](const std::vector<Value>& args) -> Value {
        RecOpts o = parse_opts(args, 1);
        o.seconds = args[0].to_double();
        if (o.seconds <= 0.0 || o.seconds > 3600.0) return Value::make_none();
        if (!rec_start(o)) return Value::make_none();
        size_t want = g_cap_buf.size();
        // wall clock, not sample count: a device that stalls must not hang the VM
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds((long long)(o.seconds * 1000.0) + 2000);
        while (g_cap_pos.load(std::memory_order_relaxed) < want &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Value m = rec_result();
        rec_stop();
        return m;
    });

    // WAV.RECSTART([opts{}]) -> bool   same options, plus seconds as the buffer cap
    vm.register_native("WAV.RECSTART", 0, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(rec_start(parse_opts(args, 0)));
    });

    // WAV.RECLEN() -> seconds captured so far
    vm.register_native("WAV.RECLEN", 0, 0, [](const std::vector<Value>&) -> Value {
        size_t n = g_cap_pos.load(std::memory_order_relaxed);
        return Value::make_f64((double)n / ((double)g_cap_rate * g_cap_ch));
    });

    // WAV.RECSTOP() -> { samples, rate, channels, frames } or NONE
    vm.register_native("WAV.RECSTOP", 0, 0, [](const std::vector<Value>&) -> Value {
        if (!g_cap_active.load() && g_cap_buf.empty()) return Value::make_none();
        Value m = rec_result();
        rec_stop();
        return m;
    });
}

#else  // !MINIAUDIO

void register_audioio_builtins(VM&) {}

#endif
