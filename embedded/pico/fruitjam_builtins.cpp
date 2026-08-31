// The drawing family reaches jdBasic under the same names the PicoCalc
// uses, so a program written for one board draws on the other. SCREENFLIP
// is kept and does nothing: this framebuffer is already on the wire.

#include "../../src/vm.h"

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
int      fruitjam_dvi_height(void);
unsigned fruitjam_dvi_frames(void);
#ifdef FRUITJAM_USB
int fruitjam_usb_mounted(void);
int fruitjam_usb_keys_waiting(void);
#endif
unsigned fruitjam_dvi_irqs(void);
unsigned fruitjam_dvi_frame_us(void);
unsigned fruitjam_dvi_csr(void);
unsigned fruitjam_dvi_hstx_meas(void);
unsigned fruitjam_dvi_sys_meas(void);
unsigned fruitjam_dvi_expand(void);
unsigned fruitjam_dvi_ctrl(void);
unsigned fruitjam_dvi_hstx_hz(void);
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
    // Counts completed fields, so a program can measure what it costs to
    // draw and whether the signal is still running.
    vm.register_native("DVI.FRAMES", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_frames());
    });
    vm.register_native("DVI.IRQS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_irqs());
    });
    vm.register_native("DVI.CLOCK", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_hstx_hz());
    });
    vm.register_native("SYS.CLOCK", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_sys_hz());
    });
    vm.register_native("DVI.CSR", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_csr());
    });
    vm.register_native("DVI.EXPAND", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_expand());
    });
    vm.register_native("DVI.DMACTRL", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_ctrl());
    });
    vm.register_native("DVI.MEASHSTX", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_hstx_meas());
    });
    vm.register_native("DVI.MEASSYS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_sys_meas());
    });
    vm.register_native("DVI.FRAMEUS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64((int64_t)fruitjam_dvi_frame_us());
    });
#ifdef FRUITJAM_USB
    vm.register_native("USB.KEYBOARDS", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(fruitjam_usb_mounted());
    });
    vm.register_native("USB.PENDING", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_bool(fruitjam_usb_keys_waiting() != 0);
    });
#endif
}
