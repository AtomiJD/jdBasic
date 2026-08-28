// The board's own builtins, registered into the VM behind the ESP32
// define. On this chip the heap can answer directly what the RP2350 had
// to find by binary search, and it answers twice: internal RAM and
// PSRAM are separate pools with very different sizes and speeds.

#include "../../src/vm.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#define CAP_INT (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void register_esp32_builtins(VM& vm) {
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
