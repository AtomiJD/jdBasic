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
}

#else  // !MINIAUDIO

void register_audioio_builtins(VM&) {}

#endif
