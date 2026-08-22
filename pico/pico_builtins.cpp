// The board's own builtins: pins and the LED, registered into the VM
// behind the PICO define. GPIO numbers are the RP2350's own; the LED on
// a Pico 2 W hangs off the radio chip, so it gets a word of its own.

#include "../src/vm.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

void register_pico_fs_debug(VM& vm);
void register_pico_alias_probe(VM& vm);
void register_pico_atrans_probe(VM& vm);
void register_pico_nuke_pt(VM& vm);

void register_pico_builtins(VM& vm) {
    vm.register_native("GPIO.MODE", 2, 2, [](const std::vector<Value>& args) -> Value {
        unsigned pin = (unsigned)args[0].to_double();
        gpio_init(pin);
        gpio_set_dir(pin, args[1].to_double() != 0);
        return Value();
    });
    vm.register_native("GPIO.WRITE", 2, 2, [](const std::vector<Value>& args) -> Value {
        gpio_put((unsigned)args[0].to_double(), args[1].to_double() != 0);
        return Value();
    });
    vm.register_native("GPIO.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(gpio_get((unsigned)args[0].to_double()) ? 1 : 0);
    });
    vm.register_native("GPIO.PULLUP", 1, 1, [](const std::vector<Value>& args) -> Value {
        gpio_pull_up((unsigned)args[0].to_double());
        return Value();
    });
    register_pico_fs_debug(vm);
    register_pico_alias_probe(vm);
    register_pico_atrans_probe(vm);
    register_pico_nuke_pt(vm);
    vm.register_native("LED", 1, 1, [](const std::vector<Value>& args) -> Value {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, args[0].to_double() != 0);
        return Value();
    });
}

extern "C" void jdb_pico_sleep_ms(unsigned ms) {
    sleep_ms(ms);
}

extern "C" void jdb_pico_fs_selftest(char* out, int cap);

void register_pico_fs_debug(VM& vm) {
    vm.register_native("FS.TEST", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[192];
        jdb_pico_fs_selftest(buf, sizeof buf);
        return Value::make_string(buf);
    });
}

extern "C" void jdb_pico_alias_probe(char* out, int cap);

void register_pico_alias_probe(VM& vm) {
    vm.register_native("FS.ALIAS", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[96];
        jdb_pico_alias_probe(buf, sizeof buf);
        return Value::make_string(buf);
    });
}

extern "C" void jdb_pico_atrans_probe(char* out, int cap);

void register_pico_atrans_probe(VM& vm) {
    vm.register_native("FS.ATRANS", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[96];
        jdb_pico_atrans_probe(buf, sizeof buf);
        return Value::make_string(buf);
    });
}

extern "C" void jdb_pico_nuke_pt(void);

void register_pico_nuke_pt(VM& vm) {
    vm.register_native("FS.NUKEPT", 0, 0, [](const std::vector<Value>&) -> Value {
        jdb_pico_nuke_pt();
        return Value();
    });
}
