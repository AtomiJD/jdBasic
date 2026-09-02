// The drawing family reaches jdBasic under the same names the PicoCalc
// uses, so a program written for one board draws on the other. SCREENFLIP
// is kept and does nothing: this framebuffer is already on the wire.

#include "../../src/vm.h"
#include <ctype.h>
#include <stdio.h>

extern "C" {
void fruitjam_gfx_palette(int i, int r, int g, int b);
void fruitjam_gfx_color(int r, int g, int b);
void fruitjam_gfx_clear(int index);
void fruitjam_gfx_pset(int x, int y);
void fruitjam_gfx_line(int x1, int y1, int x2, int y2);
void fruitjam_gfx_rect(int x, int y, int w, int h, int fill);
void fruitjam_gfx_circle(int cx, int cy, int rad, int fill);
void fruitjam_gfx_text(int x, int y, const char* s, int scale);
int      fruitjam_dvi_alloc(void);
unsigned char* fruitjam_dvi_framebuffer(void);
int      fruitjam_dvi_width(void);
int      fruitjam_dvi_peek(int x, int y);
int      fruitjam_dvi_cache(char* out, int cap);
int      fruitjam_dvi_height(void);
unsigned fruitjam_dvi_frames(void);
int  fruitjam_con_enable(int on);
int  fruitjam_con_on(void);
void fruitjam_con_size(int* cols, int* rows);
unsigned fruitjam_psram_size(void);
unsigned fruitjam_psram_heap_free(void);
unsigned fruitjam_psram_heap_largest(void);
int  fruitjam_psram_torture(char* out, int cap, int rounds);
int  fruitjam_psram_check(char* out, int cap);
#ifdef FRUITJAM_USB
void fruitjam_kbd_layout(int de);
int  fruitjam_kbd_layout_get(void);
int  fruitjam_kbd_down(int code);
int  fruitjam_button(int n);
int  fruitjam_button_count(void);
int  fruitjam_ir_raw(void);
void fruitjam_neo_set(int index, int r, int g, int b);
void fruitjam_neo_show(void);
void fruitjam_neo_clear(void);
int  fruitjam_neo_count(void);
void jdb_snd_out_route(int speaker);
int  jdb_snd_out_probe(char* out, int cap);
int  jdb_snd_out_stat(char* out, int cap);
int  jdb_snd_out_pins(char* out, int cap);
int fruitjam_usb_start(void);
int fruitjam_usb_keyboards(void);
int fruitjam_usb_devices(void);
int fruitjam_usb_keys_waiting(void);
int fruitjam_usb_key_count(void);
int fruitjam_usb_diag(char* out, int cap);
int fruitjam_usb_time(char* out, int cap);
int fruitjam_pad_count(void);
int fruitjam_pad_raw(int idx, char* out, int cap);
int fruitjam_pad_name(int idx, char* out, int cap);
double fruitjam_pad_axis(int idx, int axis);
int fruitjam_pad_button(int idx, int btn);
int fruitjam_pad_hat(int idx);
#endif
unsigned fruitjam_dvi_irqs(void);
unsigned fruitjam_dvi_frame_us(void);
unsigned fruitjam_dvi_late(void);
unsigned fruitjam_dvi_worst(void);
unsigned fruitjam_dvi_short(void);
unsigned fruitjam_dvi_shortest(void);
unsigned fruitjam_dvi_csr(void);
unsigned fruitjam_dvi_hstx_meas(void);
unsigned fruitjam_dvi_sys_meas(void);
unsigned fruitjam_dvi_expand(void);
unsigned fruitjam_dvi_ctrl(void);
unsigned fruitjam_dvi_sys_hz(void);
}

static void gfx_maybe_color(const std::vector<Value>& args, size_t at) {
    if (args.size() >= at + 3)
        fruitjam_gfx_color((int)args[at].to_double(),
                           (int)args[at + 1].to_double(),
                           (int)args[at + 2].to_double());
}

void register_fruitjam_gfx(VM& vm) {
    vm.register_native("DRAWCOLOR", 3, 4, [](const std::vector<Value>& args) -> Value {
        fruitjam_gfx_color((int)args[0].to_double(), (int)args[1].to_double(),
                           (int)args[2].to_double());
        return Value();
    });
    vm.register_native("PSET", 2, 5, [](const std::vector<Value>& args) -> Value {
        gfx_maybe_color(args, 2);
        fruitjam_gfx_pset((int)args[0].to_double(), (int)args[1].to_double());
        return Value();
    });
    vm.register_native("LINE", 4, 7, [](const std::vector<Value>& args) -> Value {
        gfx_maybe_color(args, 4);
        fruitjam_gfx_line((int)args[0].to_double(), (int)args[1].to_double(),
                          (int)args[2].to_double(), (int)args[3].to_double());
        return Value();
    });
    vm.register_native("RECT", 4, 8, [](const std::vector<Value>& args) -> Value {
        int fill = args.size() >= 5 ? (args[4].to_double() != 0) : 0;
        gfx_maybe_color(args, 5);
        fruitjam_gfx_rect((int)args[0].to_double(), (int)args[1].to_double(),
                          (int)args[2].to_double(), (int)args[3].to_double(), fill);
        return Value();
    });
    vm.register_native("CIRCLE", 3, 7, [](const std::vector<Value>& args) -> Value {
        int fill = args.size() >= 4 ? (args[3].to_double() != 0) : 0;
        gfx_maybe_color(args, 4);
        fruitjam_gfx_circle((int)args[0].to_double(), (int)args[1].to_double(),
                            (int)args[2].to_double(), fill);
        return Value();
    });
    vm.register_native("TEXT", 3, 7, [](const std::vector<Value>& args) -> Value {
        gfx_maybe_color(args, 3);
        int scale = args.size() >= 7 ? (int)args[6].to_double() : 1;
        fruitjam_gfx_text((int)args[0].to_double(), (int)args[1].to_double(),
                          args[2].to_string().c_str(), scale);
        return Value();
    });
    vm.register_native("GFX.CLEAR", 0, 1, [](const std::vector<Value>& args) -> Value {
        fruitjam_gfx_clear(args.size() >= 1 ? (int)args[0].to_double() : 0);
        return Value();
    });
    vm.register_native("GFX.PALETTE", 4, 4, [](const std::vector<Value>& args) -> Value {
        fruitjam_gfx_palette((int)args[0].to_double(), (int)args[1].to_double(),
                             (int)args[2].to_double(), (int)args[3].to_double());
        return Value();
    });
    vm.register_native("SCREENFLIP", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value();
    });
    vm.register_native("SCREEN", 0, 4, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(fruitjam_dvi_alloc() != 0);
    });
    vm.register_native("GFX.BUFFERED", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(fruitjam_dvi_framebuffer() != nullptr);
    });
    vm.register_native("GFX.WIDTH", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_dvi_width());
    });
    vm.register_native("GFX.HEIGHT", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_dvi_height());
    });
    // The console and a drawing program share one framebuffer, so a
    // program that wants the screen to itself takes it here. The prompt
    // keeps running over the serial line meanwhile.
    vm.register_native("GFX.CONSOLE", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.size() >= 1) fruitjam_con_enable(args[0].to_double() != 0);
        return Value::make_bool(fruitjam_con_on() != 0);
    });
    // What the framebuffer holds at a point, the way the panel boards
    // answer it. Here that is the glass too - this framebuffer is what
    // the wire is reading.
    vm.register_native("GFX.PEEK", 2, 2, [](const std::vector<Value>& args) -> Value {
        int v = fruitjam_dvi_peek((int)args[0].to_double(), (int)args[1].to_double());
        Value arr = Value::make_array();
        if (v < 0) return arr;
        arr.as_array()->elements.push_back(Value::make_i64(v & 0xE0));
        arr.as_array()->elements.push_back(Value::make_i64((v << 3) & 0xE0));
        arr.as_array()->elements.push_back(Value::make_i64((v << 6) & 0xC0));
        return arr;
    });
    vm.register_native("GFX.CONSIZE", 0, 0, [](const std::vector<Value>&) -> Value {
        int cols = 0, rows = 0;
        fruitjam_con_size(&cols, &rows);
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(cols));
        arr.as_array()->elements.push_back(Value::make_i64(rows));
        return arr;
    });
    // Counts completed fields, so a program can measure what it costs to
    // draw and whether the signal is still running.
    vm.register_native("DVI.FRAMES", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_frames());
    });
    vm.register_native("DVI.IRQS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_irqs());
    });
    // Measured against the reference, not what the SDK was told: the
    // scanout retunes clk_hstx behind its back, so its bookkeeping value
    // still reads the 150 MHz default.
    vm.register_native("DVI.CLOCK", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_hstx_meas() * 1000);
    });
    // The name the ES3C28P uses for the same question. Mapped, not yet
    // in the heap: a program cannot ask malloc for it.
    vm.register_native("SYS.PSRAM", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_psram_heap_free());
    });
    vm.register_native("SYS.PSRAMLARGEST", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_psram_heap_largest());
    });
    // Alloc, fill, verify and free in the window while the scanout and
    // the USB host are running - the part a desktop cannot reproduce.
    vm.register_native("PSRAM.TORTURE$", 0, 1, [](const std::vector<Value>& args) -> Value {
        int rounds = args.size() >= 1 ? (int)args[0].to_double() : 2000;
        char b[96];
        fruitjam_psram_torture(b, sizeof b, rounds);
        return Value::make_string(b);
    });
    vm.register_native("PSRAM.TEST$", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[96];
        fruitjam_psram_check(b, sizeof b);
        return Value::make_string(b);
    });
    vm.register_native("SYS.CLOCK", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_sys_hz());
    });
    // Everything the scanout can be asked about in one line: the two
    // HSTX registers, the DMA control word, and the two clocks as the
    // counter actually measured them rather than as they were asked for.
    vm.register_native("DVI.CACHE$", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[128];
        fruitjam_dvi_cache(b, sizeof b);
        return Value::make_string(b);
    });
    // How many frames arrived late since boot, and the worst one. A
    // blackout that leaves this at zero is the monitor's doing; one that
    // moves it is ours.
    vm.register_native("DVI.LATE$", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[96];
        snprintf(b, sizeof b, "%u late %u early of %u, worst %u us, shortest %u us",
                 (unsigned)fruitjam_dvi_late(), (unsigned)fruitjam_dvi_short(),
                 (unsigned)fruitjam_dvi_frames(),
                 (unsigned)fruitjam_dvi_worst(), (unsigned)fruitjam_dvi_shortest());
        return Value::make_string(b);
    });
    vm.register_native("DVI.DIAG$", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[128];
        snprintf(b, sizeof b, "csr=%08x expand=%08x dma=%08x hstx=%u sys=%u",
                 (unsigned)fruitjam_dvi_csr(), (unsigned)fruitjam_dvi_expand(),
                 (unsigned)fruitjam_dvi_ctrl(), (unsigned)fruitjam_dvi_hstx_meas(),
                 (unsigned)fruitjam_dvi_sys_meas());
        return Value::make_string(b);
    });
    vm.register_native("DVI.FRAMEUS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_frame_us());
    });
#ifdef FRUITJAM_USB
    // 1 is the BOOT button, 2 and 3 are the pair beside it.
    vm.register_native("BUTTON.GET", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(fruitjam_button((int)args[0].to_double()) != 0);
    });
    vm.register_native("BUTTON.COUNT", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_button_count());
    });
    // KBD.LAYOUT with no argument reports, with one it sets: "DE" or "US".
    vm.register_native("KBD.LAYOUT", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (args.size() >= 1) {
            std::string w = args[0].to_string();
            for (auto& ch : w) ch = (char)toupper((unsigned char)ch);
            fruitjam_kbd_layout(w == "DE" || w == "DEUTSCH" || w == "1");
        }
        return Value::make_string(fruitjam_kbd_layout_get() ? "DE" : "US");
    });
    vm.register_native("IR.RAW", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_ir_raw());
    });
    // Nothing reaches the strip until NEOPIXEL.SHOW, so a whole pattern
    // arrives at once rather than crawling across.
    vm.register_native("NEOPIXEL.SET", 4, 4, [](const std::vector<Value>& args) -> Value {
        fruitjam_neo_set((int)args[0].to_double(), (int)args[1].to_double(),
                         (int)args[2].to_double(), (int)args[3].to_double());
        return Value();
    });
    vm.register_native("NEOPIXEL.SHOW", 0, 0, [](const std::vector<Value>&) -> Value {
        fruitjam_neo_show();
        return Value();
    });
    vm.register_native("NEOPIXEL.CLEAR", 0, 0, [](const std::vector<Value>&) -> Value {
        fruitjam_neo_clear();
        return Value();
    });
    vm.register_native("NEOPIXEL.COUNT", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_neo_count());
    });
    vm.register_native("SND.PROBE", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[160];
        jdb_snd_out_probe(b, sizeof b);
        return Value::make_string(b);
    });
    // 1 picks the Class-D speaker amplifier, 0 the headphone jack. The
    // routing bits carry one or the other, never both.
    vm.register_native("SND.STAT", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[160];
        jdb_snd_out_stat(b, sizeof b);
        return Value::make_string(b);
    });
    vm.register_native("SND.PINS", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[128];
        jdb_snd_out_pins(b, sizeof b);
        return Value::make_string(b);
    });
    vm.register_native("SND.OUT", 1, 1, [](const std::vector<Value>& args) -> Value {
        jdb_snd_out_route((int)args[0].to_double() != 0);
        return Value();
    });
    vm.register_native("USB.START", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(fruitjam_usb_start() != 0);
    });
    vm.register_native("USB.KEYBOARDS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_usb_keyboards());
    });
    vm.register_native("USB.DEVICES", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_usb_devices());
    });
    vm.register_native("USB.KEYS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_usb_key_count());
    });
    // Every address the host stack has, with what each says it is.
    // What the host stack costs the core it runs on.
    vm.register_native("USB.TIME$", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[96];
        fruitjam_usb_time(b, sizeof b);
        return Value::make_string(b);
    });
    vm.register_native("USB.DIAG$", 0, 0, [](const std::vector<Value>&) -> Value {
        char b[160];
        fruitjam_usb_diag(b, sizeof b);
        return Value::make_string(b);
    });
    // A gamepad's report as it arrives. What the bytes mean differs per
    // pad, so they are handed over before anything decides.
    vm.register_native("JOY.COUNT", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_pad_count());
    });
    // The names the desktop uses, so a program that reads a pad reads the
    // same on both. Axes 0 to 3 are the sticks and 4 and 5 the triggers;
    // buttons run square, cross, circle, triangle, then the shoulders.
    // Whether a key is held right now, rather than what was typed. A
    // game loop wants this: the queue gives one character a press and
    // then a pause, which is right for an editor and wrong for steering.
    // The name is the desktop's; the argument is the code KEY.NOW hands
    // back, or a one-character string.
    vm.register_native("GFX.KEYSTATE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int code;
        if (args[0].type == ValueType::STRING) {
            const std::string& s = args[0].as_string()->data;
            if (s.empty()) return Value::make_bool(false);
            code = (unsigned char)s[0];
        } else {
            code = (int)args[0].to_double();
        }
        return Value::make_bool(fruitjam_kbd_down(code) != 0);
    });
    vm.register_native("JOY.NAME$", 0, 1, [](const std::vector<Value>& args) -> Value {
        char b[32];
        fruitjam_pad_name(args.size() >= 1 ? (int)args[0].to_double() : 0, b, sizeof b);
        return Value::make_string(b);
    });
    vm.register_native("JOY.AXIS", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_f64(fruitjam_pad_axis((int)args[0].to_double(),
                                                 (int)args[1].to_double()));
    });
    vm.register_native("JOY.BUTTON", 2, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_bool(fruitjam_pad_button((int)args[0].to_double(),
                                                    (int)args[1].to_double()) != 0);
    });
    vm.register_native("JOY.HAT", 1, 2, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(fruitjam_pad_hat((int)args[0].to_double()));
    });
    vm.register_native("JOY.RAW$", 0, 1, [](const std::vector<Value>& args) -> Value {
        char b[64];
        fruitjam_pad_raw(args.size() >= 1 ? (int)args[0].to_double() : 0, b, sizeof b);
        return Value::make_string(b);
    });
    vm.register_native("USB.PENDING", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(fruitjam_usb_keys_waiting() != 0);
    });
#endif
}
