// The board's own builtins, registered into the VM behind the ESP32
// define. On this chip the heap can answer directly what the RP2350 had
// to find by binary search, and it answers twice: internal RAM and
// PSRAM are separate pools with very different sizes and speeds.

#include "../../../src/vm.h"
#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAP_INT (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void register_esp32_fs(VM& vm);
void register_esp32_wifi(VM& vm);
void register_jdb_httpd(VM& vm);
void register_esp32_hw(VM& vm);
void register_esp32_events(VM& vm);
void register_es3c28p_gfx(VM& vm);
void register_jdb_play(VM& vm);

// This runs first inside VM::register_builtins, so the heap here is the
// heap after the VM is constructed and before a single native exists.
// SYS.NATIVES reads the difference back out.
static size_t s_before_natives_int = 0;
static size_t s_before_natives_psram = 0;

// Called from app_main the moment the VM is built, so the difference is
// registration and nothing else: no REPL line has been compiled yet.
static size_t s_after_natives_int = 0;
void esp32_note_after_init(void) {
    s_after_natives_int = heap_caps_get_free_size(CAP_INT);
}

void register_esp32_builtins(VM& vm) {
    s_before_natives_int = heap_caps_get_free_size(CAP_INT);
    s_before_natives_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    vm.register_native("SYS.NATIVES", 0, 0, [&vm](const std::vector<Value>&) -> Value {
        auto names = vm.native_names();
        size_t text = 0;
        for (const auto& n : names) text += n.size() + 1;
        char buf[192];
        snprintf(buf, sizeof buf,
                 "%u natives, %u bytes of name, registry %u bytes, cost internal %d psram %d",
                 (unsigned)names.size(), (unsigned)text, (unsigned)vm.native_registry_bytes(),
                 (int)s_before_natives_int - (int)s_after_natives_int,
                 (int)s_before_natives_psram - (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return Value::make_string(buf);
    });

    register_esp32_fs(vm);
    register_esp32_wifi(vm);
    register_jdb_httpd(vm);
    register_esp32_hw(vm);
    register_esp32_events(vm);
    register_es3c28p_gfx(vm);
    register_jdb_play(vm);

    // The console's keys, the way the RP2350 boards hand them out: KEY.GET
    // waits for one, KEY.NOW answers -1 when none has arrived. stdin is
    // non-blocking here, so the wait is a poll.
    vm.register_native("KEY.GET", 0, 0, [](const std::vector<Value>&) -> Value {
        for (;;) {
            int c = getchar();
            if (c != EOF) return Value::make_i64(c);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    });
    vm.register_native("KEY.NOW", 0, 0, [](const std::vector<Value>&) -> Value {
        int c = getchar();
        return Value::make_i64(c == EOF ? -1 : c);
    });

    // [size, deepest use so far] of the main task's stack, in bytes.
    vm.register_native("SYS.STACK", 0, 0, [](const std::vector<Value>&) -> Value {
        Value a = Value::make_array();
        unsigned size = CONFIG_ESP_MAIN_TASK_STACK_SIZE;
        unsigned left = (unsigned)uxTaskGetStackHighWaterMark(NULL);
        a.as_array()->elements.push_back(Value::make_i64(size));
        a.as_array()->elements.push_back(Value::make_i64(size > left ? size - left : 0));
        return a;
    });

    vm.register_native("SYS.FREE", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    });

    // The number SYS.FREE cannot give: the biggest single block the heap
    // will actually hand over. A vector that doubles as it grows asks for
    // one of these, and a heap holding plenty in scattered pieces still
    // says no.
    vm.register_native("SYS.LARGEST", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    });

    vm.register_native("SYS.INTERNAL", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)heap_caps_get_free_size(CAP_INT));
    });

    vm.register_native("SYS.PSRAM", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    });

    // Both pools at once, plus the low-water mark: the smallest the heap
    // has been since boot, which is what a program actually survived
    // rather than what it happens to hold now.
    vm.register_native("SYS.MEM", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[224];
        snprintf(buf, sizeof buf,
                 "internal=%u/%u largest=%u psram=%u/%u largest=%u lowmark=%u",
                 (unsigned)heap_caps_get_free_size(CAP_INT),
                 (unsigned)heap_caps_get_total_size(CAP_INT),
                 (unsigned)heap_caps_get_largest_free_block(CAP_INT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT));
        return Value::make_string(buf);
    });
}
