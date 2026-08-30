// The drawing verbs on the ES3C28P's panel. The same names and the same
// argument order as the RP2350 port and the desktop, so a program that
// draws reads the same wherever it runs.
//
// The one difference a program can feel: here every primitive writes a
// framebuffer and nothing reaches the glass until SCREENFLIP. On the
// PicoCalc drawing goes straight out unless GFX.BUFFER asked otherwise,
// because that board has no room to keep a frame.

#include <string>
#include <vector>
#include "../../../src/vm.h"

extern "C" {
int  es3c28p_lcd_init(void);
int  es3c28p_lcd_ready(void);
int  es3c28p_lcd_width(void);
int  es3c28p_lcd_height(void);
void es3c28p_lcd_backlight(int on);
void es3c28p_lcd_color(int r, int g, int b);
void es3c28p_lcd_pset(int x, int y);
void es3c28p_lcd_line(int x1, int y1, int x2, int y2);
void es3c28p_lcd_rect(int x, int y, int w, int h, int fill);
void es3c28p_lcd_circle(int cx, int cy, int rad, int fill);
void es3c28p_lcd_text(int x, int y, const char* s, int scale);
void es3c28p_lcd_clear(void);
void es3c28p_lcd_flip(void);
void es3c28p_lcd_peek(int x, int y, int* r, int* g, int* b);
int  es3c28p_lcd_readback(int x, int y, int* r, int* g, int* b);
int  es3c28p_lcd_panel_id(int* a, int* b, int* c);
int  es3c28p_con_enable(int on);
void es3c28p_con_size(int* cols, int* rows);
int  es3c28p_touch_init(void);
int  es3c28p_touch_ready(void);
int  es3c28p_touch_id(int* chip, int* vendor);
int  es3c28p_touch_raw(int* n, int* x, int* y);
int  es3c28p_touch_read(int* n, int* x, int* y);
int  es3c28p_snd_start(void);
int  es3c28p_snd_ready(void);
void picocalc_snd_tone(int freq);
void picocalc_snd_beep(int freq, int ms);
void picocalc_snd_volume(int pct);
void es3c28p_lcd_diag(int* sent, int* failed, int* last);
}

static void need_panel(const char* verb) {
    (void)verb;
    if (!es3c28p_lcd_ready())
        throw std::runtime_error(std::string("no panel, call SCREEN first"));
}

static Value rgb_value(int r, int g, int b) {
    Value arr = Value::make_array();
    auto& el = arr.as_array()->elements;
    el.push_back(Value::make_i64(r));
    el.push_back(Value::make_i64(g));
    el.push_back(Value::make_i64(b));
    return arr;
}

static void maybe_color(const std::vector<Value>& args, size_t at) {
    if (args.size() >= at + 3)
        es3c28p_lcd_color((int)args[at].to_double(),
                          (int)args[at + 1].to_double(),
                          (int)args[at + 2].to_double());
}

void register_es3c28p_gfx(VM& vm) {
    // SCREEN takes the desktop's arguments and ignores the size: the
    // panel is 320 by 240 and cannot be asked for another. It answers
    // with the bytes the framebuffer took, so a program can tell whether
    // it got one.
    vm.register_native("SCREEN", 0, 4, [](const std::vector<Value>&) -> Value {
        int rc = es3c28p_lcd_init();
        if (rc != 0)
            throw std::runtime_error("the panel did not start (" +
                                     std::to_string(rc) + ")");
        return Value::make_i64((int64_t)es3c28p_lcd_width() *
                               es3c28p_lcd_height() * 2);
    });
    vm.register_native("SCREENFLIP", 0, 0, [](const std::vector<Value>&) -> Value {
        need_panel("SCREENFLIP");
        es3c28p_lcd_flip();
        return Value();
    });
    vm.register_native("DRAWCOLOR", 3, 3, [](const std::vector<Value>& args) -> Value {
        es3c28p_lcd_color((int)args[0].to_double(), (int)args[1].to_double(),
                          (int)args[2].to_double());
        return Value();
    });
    vm.register_native("PSET", 2, 5, [](const std::vector<Value>& args) -> Value {
        need_panel("PSET");
        maybe_color(args, 2);
        es3c28p_lcd_pset((int)args[0].to_double(), (int)args[1].to_double());
        return Value();
    });
    vm.register_native("LINE", 4, 7, [](const std::vector<Value>& args) -> Value {
        need_panel("LINE");
        maybe_color(args, 4);
        es3c28p_lcd_line((int)args[0].to_double(), (int)args[1].to_double(),
                         (int)args[2].to_double(), (int)args[3].to_double());
        return Value();
    });
    vm.register_native("RECT", 4, 8, [](const std::vector<Value>& args) -> Value {
        need_panel("RECT");
        int fill = args.size() >= 5 ? (args[4].to_double() != 0) : 0;
        maybe_color(args, 5);
        es3c28p_lcd_rect((int)args[0].to_double(), (int)args[1].to_double(),
                         (int)args[2].to_double(), (int)args[3].to_double(), fill);
        return Value();
    });
    vm.register_native("CIRCLE", 3, 7, [](const std::vector<Value>& args) -> Value {
        need_panel("CIRCLE");
        int fill = args.size() >= 4 ? (args[3].to_double() != 0) : 0;
        maybe_color(args, 4);
        es3c28p_lcd_circle((int)args[0].to_double(), (int)args[1].to_double(),
                           (int)args[2].to_double(), fill);
        return Value();
    });
    vm.register_native("TEXT", 3, 7, [](const std::vector<Value>& args) -> Value {
        need_panel("TEXT");
        maybe_color(args, 3);
        int scale = args.size() >= 7 ? (int)args[6].to_double() : 1;
        es3c28p_lcd_text((int)args[0].to_double(), (int)args[1].to_double(),
                         args[2].to_string().c_str(), scale);
        return Value();
    });
    vm.register_native("GFX.CLEAR", 0, 3, [](const std::vector<Value>& args) -> Value {
        need_panel("GFX.CLEAR");
        if (args.size() >= 3)
            es3c28p_lcd_color((int)args[0].to_double(), (int)args[1].to_double(),
                              (int)args[2].to_double());
        else
            es3c28p_lcd_color(0, 0, 0);
        es3c28p_lcd_clear();
        return Value();
    });
    vm.register_native("GFX.LIGHT", 1, 1, [](const std::vector<Value>& args) -> Value {
        es3c28p_lcd_backlight(args[0].to_double() != 0);
        return Value();
    });
    // The panel as a text console. Everything printed goes to both the
    // serial line and the glass while it is on.
    vm.register_native("GFX.CONSOLE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int on = args[0].to_double() != 0;
        if (on && !es3c28p_lcd_ready()) es3c28p_lcd_init();
        int rc = es3c28p_con_enable(on);
        if (rc != 0)
            throw std::runtime_error("the console would not start (" +
                                     std::to_string(rc) + ")");
        return Value();
    });
    vm.register_native("GFX.CONSIZE", 0, 0, [](const std::vector<Value>&) -> Value {
        int c, r;
        es3c28p_con_size(&c, &r);
        Value arr = Value::make_array();
        arr.as_array()->elements.push_back(Value::make_i64(c));
        arr.as_array()->elements.push_back(Value::make_i64(r));
        return arr;
    });
    vm.register_native("GFX.WIDTH", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(es3c28p_lcd_width());
    });
    vm.register_native("GFX.HEIGHT", 0, 0, [](const std::vector<Value>&) -> Value {
        return Value::make_i64(es3c28p_lcd_height());
    });
    // What the framebuffer holds at a point, as [r, g, b].
    vm.register_native("GFX.PEEK", 2, 2, [](const std::vector<Value>& args) -> Value {
        need_panel("GFX.PEEK");
        int r, g, b;
        es3c28p_lcd_peek((int)args[0].to_double(), (int)args[1].to_double(),
                         &r, &g, &b);
        return rgb_value(r, g, b);
    });
    // What the SPI side actually did: transfers attempted, transfers the
    // driver refused, and the last reason it gave.
    vm.register_native("GFX.DIAG", 0, 0, [](const std::vector<Value>&) -> Value {
        int sent, failed, last;
        es3c28p_lcd_diag(&sent, &failed, &last);
        return rgb_value(sent, failed, last);
    });
    // The touch screen. TOUCH answers [count, x, y] in screen coordinates,
    // TOUCH.RAW the controller's own portrait numbers, so a mapping can be
    // argued with a finger instead of an assumption.
    vm.register_native("TOUCH", 0, 0, [](const std::vector<Value>&) -> Value {
        if (!es3c28p_touch_ready() && es3c28p_touch_init() != 0)
            throw std::runtime_error("no touch controller");
        int n, x, y;
        if (es3c28p_touch_read(&n, &x, &y) != 0)
            throw std::runtime_error("the touch controller did not answer");
        return rgb_value(n, x, y);
    });
    vm.register_native("TOUCH.RAW", 0, 0, [](const std::vector<Value>&) -> Value {
        if (!es3c28p_touch_ready() && es3c28p_touch_init() != 0)
            throw std::runtime_error("no touch controller");
        int n, x, y;
        if (es3c28p_touch_raw(&n, &x, &y) != 0)
            throw std::runtime_error("the touch controller did not answer");
        return rgb_value(n, x, y);
    });
    vm.register_native("TOUCH.ID", 0, 0, [](const std::vector<Value>&) -> Value {
        if (!es3c28p_touch_ready() && es3c28p_touch_init() != 0)
            throw std::runtime_error("no touch controller");
        int c, v;
        if (es3c28p_touch_id(&c, &v) != 0)
            throw std::runtime_error("the touch controller did not answer");
        return rgb_value(c, v, 0);
    });
    // The panel says who it is: an ILI9341 answers 0, 147, 65. The one
    // read with an answer the datasheet already knows.
    vm.register_native("GFX.PANELID", 0, 0, [](const std::vector<Value>&) -> Value {
        need_panel("GFX.PANELID");
        int a, b, c;
        int rc = es3c28p_lcd_panel_id(&a, &b, &c);
        if (rc != 0)
            throw std::runtime_error("the panel did not answer (" +
                                     std::to_string(rc) + ")");
        return rgb_value(a, b, c);
    });
    // What the panel holds at a point, read back over MISO. Not the same
    // question as GFX.PEEK: this one crosses the wire, so it answers
    // whether the flush arrived rather than whether the drawing did.
    vm.register_native("GFX.READBACK", 2, 2, [](const std::vector<Value>& args) -> Value {
        need_panel("GFX.READBACK");
        int r, g, b;
        int rc = es3c28p_lcd_readback((int)args[0].to_double(),
                                      (int)args[1].to_double(), &r, &g, &b);
        if (rc != 0)
            throw std::runtime_error("the panel did not answer (" +
                                     std::to_string(rc) + ")");
        return rgb_value(r, g, b);
    });
}
