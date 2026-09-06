// The board's own builtins: pins and the LED, registered into the VM
// behind the PICO define. GPIO numbers are the RP2350's own; the LED on
// a Pico 2 W hangs off the radio chip, so it gets a word of its own.

#include "../../src/vm.h"
#include <set>
#include "pico/stdlib.h"

extern "C" int jdb_stdin_getc(int timeout_us);
extern "C" int jdb_stdin_getc(int timeout_us);
extern "C" unsigned jdb_stack_size(void);
extern "C" unsigned jdb_stack_high_water(void);
#ifdef JDB_HAS_CYW43
#include "pico/cyw43_arch.h"
#endif

void register_pico_fs_debug(VM& vm);
void register_pico_alias_probe(VM& vm);
void register_pico_atrans_probe(VM& vm);
void register_pico_nuke_pt(VM& vm);
void register_pico_keyget(VM& vm);
void register_pico_hw(VM& vm);
void register_pico_mem(VM& vm);
void register_pico_psram(VM& vm);
void register_sprite_builtins(VM& vm);
void register_pico_events(VM& vm);
#ifdef FRUITJAM
void register_fruitjam_gfx(VM& vm);
void register_fruitjam_wifi(VM& vm);
#endif
#ifdef JDB_HAS_CYW43
void register_pico_wifi(VM& vm);
void register_jdb_httpd(VM& vm);
#endif
#if defined(PICOCALC) || defined(FRUITJAM)
// The melody engine is board independent; only its output stage is not.
void register_jdb_play(VM& vm);
// So is the card reader, once it is told which pins it sits on.
void register_pico_sdtest(VM& vm);
void register_pico_sdbb(VM& vm);
#endif
#ifdef PICOCALC
void register_pico_diag(VM& vm);
void register_pico_lcdstat(VM& vm);
void register_pico_tap(VM& vm);
void register_pico_taparm(VM& vm);
void register_pico_gfx(VM& vm);

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
    register_pico_mem(vm);
#ifdef PICOCALC
    // The bit-banged probe drives GP2 to GP5, GP20 and GP21, which is
    // where the PicoCalc's chip sits and where other boards have other
    // things. The Fruit Jam's PSRAM is on the QMI bus and answers under
    // SYS.PSRAM instead.
    register_pico_psram(vm);
#endif
    register_pico_events(vm);
#ifdef FRUITJAM
    register_fruitjam_gfx(vm);
    register_fruitjam_wifi(vm);
#endif
#if defined(PICOCALC) || defined(FRUITJAM)
    register_jdb_play(vm);
    register_pico_sdtest(vm);
    register_pico_sdbb(vm);
#endif
#ifdef JDB_HAS_CYW43
    register_pico_wifi(vm);
    register_jdb_httpd(vm);
#endif
#ifdef PICOCALC
    register_pico_diag(vm);
    register_pico_lcdstat(vm);
    register_pico_tap(vm);
    register_pico_taparm(vm);
    register_pico_gfx(vm);
    register_sprite_builtins(vm);
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
        return Value::make_i64(jdb_stdin_getc(-1));
    });
    // The one a game loop needs: -1 rather than a wait when nothing has
    // been pressed. Reads whatever console the board has, so the same
    // loop works on the board's own keyboard and over the serial line.
    vm.register_native("KEY.NOW", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(jdb_stdin_getc(0));
    });
    // [size, deepest use so far], both in bytes.
    vm.register_native("SYS.STACK", 0, 0, [](const std::vector<Value>&) -> Value {
        Value a = Value::make_array();
        a.as_array()->elements.push_back(Value::make_i64(jdb_stack_size()));
        a.as_array()->elements.push_back(Value::make_i64(jdb_stack_high_water()));
        return a;
    });
}

#ifdef PICOCALC
extern "C" void picocalc_lcd_stat(int* scroll, int* cx, int* cy);
extern "C" void picocalc_lcd_console(int on);

void register_pico_lcdstat(VM& vm) {
    // The same switch the other boards have: 0 gives the panel to a
    // program that draws, 1 brings the text console back on a clear page.
    vm.register_native("GFX.CONSOLE", 0, 1, [](const std::vector<Value>& args) -> Value {
        picocalc_lcd_console(args.size() >= 1 ? (int)args[0].to_double() : 1);
        return Value();
    });
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
extern "C" void picocalc_gfx_text(int x, int y, const char* s, int scale);
extern "C" int  picocalc_gfx_buffer(int x, int y, int w, int h);
extern "C" int  picocalc_gfx_buffered(void);
extern "C" void picocalc_gfx_flip(void);
extern "C" void picocalc_gfx_clear(int index);
extern "C" void picocalc_gfx_palette(int i, int r, int g, int b);
extern "C" void jdb_con_size(int* cols, int* rows);

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
    // x, y, text [, r, g, b] like the desktop, with scale as an extra.
    vm.register_native("TEXT", 3, 7, [](const std::vector<Value>& args) -> Value {
        gfx_maybe_color(args, 3);
        int scale = args.size() >= 7 ? (int)args[6].to_double() : 1;
        picocalc_gfx_text((int)args[0].to_double(), (int)args[1].to_double(),
                          args[2].to_string().c_str(), scale);
        return Value();
    });
    // GFX.BUFFER(x, y, w, h) buffers that rectangle and answers with the
    // bytes it took, -1 if there was not enough room. GFX.BUFFER(0)
    // gives it back.
    vm.register_native("GFX.BUFFER", 1, 4, [](const std::vector<Value>& args) -> Value {
        if (args.size() < 4)
            return Value::make_i64(picocalc_gfx_buffer(0, 0, 0, 0));
        return Value::make_i64(picocalc_gfx_buffer(
            (int)args[0].to_double(), (int)args[1].to_double(),
            (int)args[2].to_double(), (int)args[3].to_double()));
    });
    // The desktop's way in, so a program that opens a screen and draws
    // sprites reads the same on both. Here it buffers the panel, which
    // now fits: a whole 320x320 at four bits is 51 KB.
    vm.register_native("SCREEN", 0, 4, [](const std::vector<Value>& args) -> Value {
        int w = args.size() >= 1 ? (int)args[0].to_double() : 320;
        int h = args.size() >= 2 ? (int)args[1].to_double() : 320;
        if (w > 320) w = 320;
        if (h > 320) h = 320;
        return Value::make_i64(picocalc_gfx_buffer(0, 0, w, h));
    });
    vm.register_native("GFX.BUFFERED", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(picocalc_gfx_buffered() != 0);
    });
    vm.register_native("SCREENFLIP", 0, 0, [](const std::vector<Value>&) -> Value {
        picocalc_gfx_flip();
        return Value();
    });
    vm.register_native("GFX.CLEAR", 0, 1, [](const std::vector<Value>& args) -> Value {
        picocalc_gfx_clear(args.size() >= 1 ? (int)args[0].to_double() : 0);
        return Value();
    });
    vm.register_native("GFX.PALETTE", 4, 4, [](const std::vector<Value>& args) -> Value {
        picocalc_gfx_palette((int)args[0].to_double(), (int)args[1].to_double(),
                             (int)args[2].to_double(), (int)args[3].to_double());
        return Value();
    });
    vm.register_native("GFX.WIDTH", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(320);
    });
    vm.register_native("GFX.HEIGHT", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(320);
    });
    vm.register_native("GFX.CONSIZE", 0, 0, [](const std::vector<Value>&) -> Value {
        int cols = 0, rows = 0;
        jdb_con_size(&cols, &rows);
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(cols));
        arr.as_array()->elements.push_back(Value::make_i64(rows));
        return arr;
    });
}


#endif // PICOCALC
// What a program can still get: the ground the heap has not claimed
// yet, plus what it claimed and handed back. Counting only the first
// reads as zero while there is plenty on the free list.
#include <malloc.h>
extern "C" char __StackLimit;
extern "C" void* sbrk(int);

extern "C" int jdb_pico_fs_free(unsigned* freebytes, unsigned* total);

extern "C" unsigned jdb_pico_heap_free(void) {
    struct mallinfo mi = mallinfo();
    return (unsigned)((&__StackLimit - (char*)sbrk(0)) + (long)mi.fordblks);
}

void register_pico_mem(VM& vm) {
    vm.register_native("SYS.DF", 0, 0, [](const std::vector<Value>&) -> Value {
        unsigned avail = 0, total = 0;
        if (jdb_pico_fs_free(&avail, &total) != 0)
            return Value::make_string("no filesystem");
        char buf[96];
        snprintf(buf, sizeof buf, "flash %u free of %u bytes", avail, total);
        return Value::make_string(buf);
    });
    vm.register_native("SYS.FREEDISK", 0, 0, [](const std::vector<Value>&) -> Value {
        unsigned avail = 0, total = 0;
        if (jdb_pico_fs_free(&avail, &total) != 0) return Value::make_i64(0);
        return Value::make_i64((int64_t)avail);
    });
    vm.register_native("SYS.FREE", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)jdb_pico_heap_free());
    });
    // Where the heap actually stands, in the allocator's own terms.
    // arena is what it has taken off the break so far, used and free
    // divide that, frags counts the free pieces, and ground is the
    // stretch below the stack that nobody has claimed yet.
    // The name the ES3C28P uses for the same question: how many builtins
    // this build registered and what their names alone cost.
    // Every builtin this board has, by name, one line.
    vm.register_native("SYS.NATIVES$", 0, 0, [&vm](const std::vector<Value>&) -> Value {
        std::string out;
        for (const auto& n : vm.native_names()) { if (!out.empty()) out += " "; out += n; }
        return Value::make_string(out);
    });
    vm.register_native("SYS.NATIVES", 0, 0, [&vm](const std::vector<Value>&) -> Value {
        auto names = vm.native_names();
        size_t text = 0;
        for (const auto& n : names) text += n.size() + 1;
        char buf[128];
        snprintf(buf, sizeof buf, "%u natives, %u bytes of name, registry %u bytes",
                 (unsigned)names.size(), (unsigned)text, (unsigned)vm.native_registry_bytes());
        return Value::make_string(buf);
    });
    vm.register_native("SYS.HEAP$", 0, 0, [](const std::vector<Value>&) -> Value {
        struct mallinfo mi = mallinfo();
        char buf[160];
        snprintf(buf, sizeof buf,
                 "arena %u used %u free %u frags %u ground %u",
                 (unsigned)mi.arena, (unsigned)mi.uordblks,
                 (unsigned)mi.fordblks, (unsigned)mi.ordblks,
                 (unsigned)(&__StackLimit - (char*)sbrk(0)));
        return Value::make_string(buf);
    });
    // The number SYS.FREE cannot give: the biggest single block the heap
    // will actually hand over. A vector that doubles as it grows asks for
    // one of these, and a heap holding plenty in scattered pieces still
    // says no - which is what a program hits long before the total runs
    // out. Binary search over malloc, ~21 probes, each one handed back.
    // Where the memory a loaded program never gives back actually went,
    // component by component across every function the VM owns. Counts the
    // storage the containers hold, not their headers - enough to tell which
    // term dominates, which is the only thing worth knowing before changing
    // a representation.
    vm.register_native("SYS.CHUNKS", 0, 0, [&vm](const std::vector<Value>&) -> Value {
        size_t code = 0, lines = 0, names = 0, consts = 0, caches = 0, protos = 0;
        size_t fns = 0, slack = 0;
        // What the same text costs across chunks. A global read by five
        // functions is five copies of its name, and a string constant
        // repeats the same way - this measures the prize before anyone pays
        // for an intern table.
        std::set<std::string> uniq_names, uniq_strs;
        size_t names_txt = 0, strs_txt = 0;
        for (const auto& f : vm.get_funcs()) {
            for (size_t s = 0; s < f.chunk.name_count(); s++) {
                const char* n = f.chunk.name_at(s);
                names_txt += strlen(n) + 1;
                uniq_names.insert(n);
            }
            for (const auto& c : f.chunk.constants) {
                if (c.type != ValueType::STRING) continue;
                const std::string& s = c.as_string()->data;
                strs_txt += s.size() + 1;
                uniq_strs.insert(s);
            }
        }
        size_t names_uniq = 0, strs_uniq = 0;
        for (const auto& n : uniq_names) names_uniq += n.size() + 1;
        for (const auto& s : uniq_strs) strs_uniq += s.size() + 1;
        for (const auto& f : vm.get_funcs()) {
            fns++;
            const Chunk& c = f.chunk;
            // What a vector holds beyond what it uses: growth doubles, and a
            // chunk never grows again once it is loaded.
            slack += c.code.capacity() - c.code.size();
            slack += (c.line_table.capacity() - c.line_table.size()) * sizeof(Chunk::LineEntry);
            slack += (c.constants.capacity() - c.constants.size()) * sizeof(Value);
            slack += (c.name_offsets.capacity() - c.name_offsets.size()) * sizeof(uint32_t);
            code   += c.code.capacity();
            lines  += c.line_table.capacity() * sizeof(Chunk::LineEntry);
            consts += c.constants.capacity() * sizeof(Value);
            caches += c.call_cache.capacity() * sizeof(uint64_t);
            caches += c.global_cache.capacity() * sizeof(uint64_t);
            caches += c.method_cache.capacity() * sizeof(Chunk::MethodCacheEntry);
            names  += c.name_offsets.capacity() * sizeof(uint32_t);
            names  += c.name_blob.capacity() + 1;
            names  += c.source_file.capacity() + 1;
            protos += f.name.capacity() + 1;
            protos += f.param_names.capacity() * sizeof(std::string);
            for (const auto& p : f.param_names) protos += p.capacity() + 1;
        }
        char buf[320];
        snprintf(buf, sizeof buf,
                 "funcs=%u code=%u lines=%u names=%u consts=%u caches=%u protos=%u total=%u slack=%u "
                 "nametxt=%u/%u strtxt=%u/%u",
                 (unsigned)fns, (unsigned)code, (unsigned)lines, (unsigned)names,
                 (unsigned)consts, (unsigned)caches, (unsigned)protos,
                 (unsigned)(code + lines + names + consts + caches + protos),
                 (unsigned)slack,
                 (unsigned)names_uniq, (unsigned)names_txt,
                 (unsigned)strs_uniq, (unsigned)strs_txt);
        return Value::make_string(buf);
    });
    vm.register_native("SYS.LARGEST", 0, 0, [](const std::vector<Value>&) -> Value {
        size_t lo = 0, hi = 4u * 1024 * 1024;
        while (lo < hi) {
            size_t mid = lo + (hi - lo + 1) / 2;
            void* p = malloc(mid);
            if (p) {
                free(p);
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        return Value::make_i64((int64_t)lo);
    });
}
#if defined(PICOCALC) || defined(FRUITJAM)
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
#endif

extern "C" uint32_t jdb_pico_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}
