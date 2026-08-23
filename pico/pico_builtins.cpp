// The board's own builtins: pins and the LED, registered into the VM
// behind the PICO define. GPIO numbers are the RP2350's own; the LED on
// a Pico 2 W hangs off the radio chip, so it gets a word of its own.

#include "../src/vm.h"
#include "pico/stdlib.h"
#ifdef JDB_HAS_CYW43
#include "pico/cyw43_arch.h"
#endif

void register_pico_fs_debug(VM& vm);
void register_pico_alias_probe(VM& vm);
void register_pico_atrans_probe(VM& vm);
void register_pico_nuke_pt(VM& vm);
void register_pico_keyget(VM& vm);
void register_pico_hw(VM& vm);
void register_pico_events(VM& vm);
#ifdef JDB_HAS_CYW43
void register_pico_wifi(VM& vm);
#endif
#ifdef PICOCALC
void register_pico_diag(VM& vm);
void register_pico_lcdstat(VM& vm);
void register_pico_tap(VM& vm);
void register_pico_taparm(VM& vm);
void register_pico_gfx(VM& vm);
void register_pico_snd(VM& vm);
void register_pico_sdtest(VM& vm);
void register_pico_sdbb(VM& vm);
#endif

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
    register_pico_keyget(vm);
    register_pico_hw(vm);
    register_pico_events(vm);
#ifdef JDB_HAS_CYW43
    register_pico_wifi(vm);
#endif
#ifdef PICOCALC
    register_pico_diag(vm);
    register_pico_lcdstat(vm);
    register_pico_tap(vm);
    register_pico_taparm(vm);
    register_pico_gfx(vm);
    register_pico_snd(vm);
    register_pico_sdtest(vm);
    register_pico_sdbb(vm);
#endif
    vm.register_native("LED", 1, 1, [](const std::vector<Value>& args) -> Value {
#ifdef JDB_HAS_CYW43
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, args[0].to_double() != 0);
#else
        gpio_init(PICO_DEFAULT_LED_PIN);
        gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
        gpio_put(PICO_DEFAULT_LED_PIN, args[0].to_double() != 0);
#endif
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

#ifdef PICOCALC
extern "C" int picocalc_kbd_rawget(uint16_t* out, int cap);
extern "C" void picocalc_lcd_row(int row, char* out, int cap);

void register_pico_diag(VM& vm) {
    vm.register_native("KBD.RAW$", 0, 0, [](const std::vector<Value>&) -> Value {
        uint16_t w[16];
        int n = picocalc_kbd_rawget(w, 16);
        char buf[128];
        int at = 0;
        for (int i = 0; i < n && at < 110; i++)
            at += snprintf(buf + at, sizeof buf - at, "%04x ", w[i]);
        buf[at] = 0;
        return Value::make_string(buf);
    });
    vm.register_native("LCD.ROW$", 1, 1, [](const std::vector<Value>& args) -> Value {
        char buf[64];
        picocalc_lcd_row((int)args[0].to_double(), buf, sizeof buf);
        return Value::make_string(buf);
    });
}
#endif // PICOCALC

void register_pico_keyget(VM& vm) {
    vm.register_native("KEY.GET", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(getchar());
    });
}

#ifdef PICOCALC
extern "C" void picocalc_lcd_stat(int* scroll, int* cx, int* cy);

void register_pico_lcdstat(VM& vm) {
    vm.register_native("LCD.STAT$", 0, 0, [](const std::vector<Value>&) -> Value {
        int s, x, y;
        picocalc_lcd_stat(&s, &x, &y);
        char buf[64];
        snprintf(buf, sizeof buf, "scroll=%d cx=%d cy=%d", s, x, y);
        return Value::make_string(buf);
    });
}

extern "C" int picocalc_lcd_tap_get(uint8_t* out, int cap);

void register_pico_tap(VM& vm) {
    vm.register_native("LCD.TAP$", 0, 0, [](const std::vector<Value>&) -> Value {
        uint8_t w[160];
        int n = picocalc_lcd_tap_get(w, 160);
        char buf[512];
        int at = 0;
        for (int i = 0; i < n && at < 500; i++)
            at += snprintf(buf + at, sizeof buf - at, "%02x", w[i]);
        buf[at] = 0;
        return Value::make_string(buf);
    });
}

extern "C" void picocalc_lcd_tap_arm(void);

void register_pico_taparm(VM& vm) {
    vm.register_native("LCD.TAPARM", 0, 0, [](const std::vector<Value>&) -> Value {
        picocalc_lcd_tap_arm();
        return Value();
    });
}

// The drawing family, scalar forms with the desktop's argument order.
extern "C" void picocalc_gfx_color(int r, int g, int b);
extern "C" void picocalc_gfx_pset(int x, int y);
extern "C" void picocalc_gfx_line(int x1, int y1, int x2, int y2);
extern "C" void picocalc_gfx_rect(int x, int y, int w, int h, int fill);
extern "C" void picocalc_gfx_circle(int cx, int cy, int rad, int fill);

static void gfx_maybe_color(const std::vector<Value>& args, size_t at) {
    if (args.size() >= at + 3)
        picocalc_gfx_color((int)args[at].to_double(),
                           (int)args[at + 1].to_double(),
                           (int)args[at + 2].to_double());
}

void register_pico_gfx(VM& vm) {
    vm.register_native("DRAWCOLOR", 3, 4, [](const std::vector<Value>& args) -> Value {
        picocalc_gfx_color((int)args[0].to_double(), (int)args[1].to_double(),
                           (int)args[2].to_double());
        return Value();
    });
    vm.register_native("PSET", 2, 5, [](const std::vector<Value>& args) -> Value {
        gfx_maybe_color(args, 2);
        picocalc_gfx_pset((int)args[0].to_double(), (int)args[1].to_double());
        return Value();
    });
    vm.register_native("LINE", 4, 7, [](const std::vector<Value>& args) -> Value {
        gfx_maybe_color(args, 4);
        picocalc_gfx_line((int)args[0].to_double(), (int)args[1].to_double(),
                          (int)args[2].to_double(), (int)args[3].to_double());
        return Value();
    });
    vm.register_native("RECT", 4, 8, [](const std::vector<Value>& args) -> Value {
        int fill = args.size() >= 5 ? (args[4].to_double() != 0) : 0;
        gfx_maybe_color(args, 5);
        picocalc_gfx_rect((int)args[0].to_double(), (int)args[1].to_double(),
                          (int)args[2].to_double(), (int)args[3].to_double(), fill);
        return Value();
    });
    vm.register_native("CIRCLE", 3, 7, [](const std::vector<Value>& args) -> Value {
        int fill = args.size() >= 4 ? (args[3].to_double() != 0) : 0;
        gfx_maybe_color(args, 4);
        picocalc_gfx_circle((int)args[0].to_double(), (int)args[1].to_double(),
                            (int)args[2].to_double(), fill);
        return Value();
    });
}

extern "C" void picocalc_snd_beep(int freq, int ms);

void register_pico_snd(VM& vm) {
    vm.register_native("BEEP", 0, 2, [](const std::vector<Value>& args) -> Value {
        int freq = args.size() >= 1 ? (int)args[0].to_double() : 880;
        int ms   = args.size() >= 2 ? (int)args[1].to_double() : 200;
        picocalc_snd_beep(freq, ms);
        return Value();
    });
}

extern "C" void sd_selftest(char* out, int cap);

void register_pico_sdtest(VM& vm) {
    vm.register_native("SD.TEST", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[160];
        sd_selftest(buf, sizeof buf);
        return Value::make_string(buf);
    });
}

extern "C" void sd_bitbang_test(char* out, int cap);

void register_pico_sdbb(VM& vm) {
    vm.register_native("SD.BB", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[64];
        sd_bitbang_test(buf, sizeof buf);
        return Value::make_string(buf);
    });
}
#endif // PICOCALC
