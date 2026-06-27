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

#include <string>
#include <vector>
#include <atomic>
#include <cstring>

namespace {
ma_device          g_mon_device;
std::atomic<bool>  g_mon_active{false};
std::atomic<float> g_mon_gain{1.0f};

// realtime passthrough: copy input -> output. NO allocation, NO locks, NO VM.
void mon_callback(ma_device* dev, void* pOut, const void* pIn, ma_uint32 frames) {
    float g = g_mon_gain.load(std::memory_order_relaxed);
    size_t n = (size_t)frames * dev->playback.channels;
    const float* in = (const float*)pIn;
    float* out = (float*)pOut;
    if (g == 1.0f) std::memcpy(out, in, n * sizeof(float));
    else for (size_t i = 0; i < n; i++) out[i] = in[i] * g;
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
        cfg.playback.format  = ma_format_f32; cfg.playback.channels = 2;
        cfg.sampleRate        = 48000;
        cfg.periodSizeInFrames = 256;          // low-ish latency
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
        return Value::make_none();
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
}

#else  // !MINIAUDIO

void register_audioio_builtins(VM&) {}

#endif
