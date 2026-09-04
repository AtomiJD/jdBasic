// A USB keyboard on the board's own sockets, reaching jdBasic as ordinary
// console input.
//
// The host controller is PIO-USB on GP1 and GP2, with the 5V rail to the
// sockets switched by GP11, and the board's own hub fans out to the three
// A connectors.
//
// The work is split across the cores. Left to itself the library drives
// its 1 ms frame from an alarm interrupt, and that starves the timer the
// SDK uses to service the USB *device* side: the console then never
// enumerates, silently, because enumeration has millisecond deadlines.
// So the frame runs on core 1, which has nothing else to do now that the
// scanout is a DMA command list.
//
// The host stack itself is pumped from the stdio driver on core 0. The
// prompt spends its time in getchar_timeout_us, which polls every driver
// in turn, so asking for a key is also what services USB - and while a
// program runs without asking for input, nothing needs servicing either.
//
// Keys arrive as HID usage codes and leave as bytes in a ring the stdio
// layer drains, so the prompt and its line editor treat the keyboard
// exactly like a terminal on the serial port. Arrows and the editing keys
// use the PicoCalc's codes, which is what that editor already understands.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "hardware/dma.h"
#include "hardware/structs/hstx_fifo.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pio_usb.h"
#include "tusb.h"

int  fruitjam_usb_start(void);

#define USB_DP_PIN      1
#define USB_5V_PIN     11

#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5
// The editor and the prompt already speak the PicoCalc's codes, and a
// bare 27 would be read as the start of an escape sequence and block
// waiting for the rest of it.
#define K_ESC   0xB1
#define K_PGUP  0xD6
#define K_PGDN  0xD7
// Selection needs the shifted arrows to be distinguishable from the plain
// ones, so they get codes of their own.
#define K_SLEFT  0xB8
#define K_SUP    0xB9
#define K_SDOWN  0xBA
#define K_SRIGHT 0xBB
#define K_SHOME  0xD8
#define K_SEND   0xD9
#define K_CLEFT  0xBC
#define K_CRIGHT 0xBD
#define K_CHOME  0xDA
#define K_CEND   0xDB
#define K_F1     0xC1
#define K_STAB   0xC2

#define KEYRING 64
static volatile uint8_t  g_ring[KEYRING];
static volatile uint16_t g_head = 0, g_tail = 0;
static uint8_t g_keyboards = 0;
static uint32_t g_keys = 0;
static uint8_t  g_last_key = 0;
static uint32_t g_returned = 0;
static char     g_tail_txt[12] = {0};
static uint8_t g_devices = 0;
static bool    g_up = false;

// In RAM with the rest of core 1's loop: while a flash sector is being
// erased, code fetched from flash is not there to fetch.
static void __not_in_flash_func(key_push)(uint8_t c) {
    uint16_t next = (uint16_t)((g_head + 1) % KEYRING);
    if (next == g_tail) return;
    g_ring[g_head] = c;
    g_head = next;
    g_keys++;
    g_last_key = c;
}

static int key_pop(void) {
    if (g_tail == g_head) return -1;
    uint8_t c = g_ring[g_tail];
    g_tail = (uint16_t)((g_tail + 1) % KEYRING);
    return c;
}

int fruitjam_usb_keys_waiting(void) { return g_head != g_tail; }
int fruitjam_usb_key_count(void)     { return (int)g_keys; }
int fruitjam_usb_keyboards(void)    { return g_keyboards; }

// Enumeration only advances while the stack is asked to run, and the only
// thing that asks is a read for a key. Boot has none to make, so it drives
// the stack directly for the moment it takes a keyboard to appear.
void fruitjam_usb_poll(void) { if (g_up) tuh_task(); }
int fruitjam_usb_devices(void)      { return g_devices; }

static void usb_task_timed(void);
static void usb_pump(void);
static void pads_rearm(void);
// Milliseconds between asking a pad again. Zero stops asking at all,
// which is how the pad's own traffic gets told apart from ours.
static volatile uint32_t g_pad_rate_ms = 8;
static volatile uint32_t g_task_last = 0;
static volatile uint32_t g_f_last = 0;
static volatile uint32_t g_f_calls = 0;
static volatile uint32_t g_f_late = 0;
static volatile uint32_t g_f_worst = 0;
static volatile uint32_t g_f_dur_worst = 0;
static volatile uint32_t g_fifo_min = 0xFFFFFFFFu;
static volatile uint32_t g_fifo_dry = 0;
static volatile uint32_t g_fifo_seen = 0;
// How many times a millisecond the FIFO is looked at. Each look is a
// read on the peripheral bus, which is the same bus the DMA uses to
// feed that FIFO - so this number is also a way of adding known
// contention on purpose and watching what it does to the picture.
static volatile uint32_t g_fifo_rate = 0;
static volatile uint64_t g_f_total = 0;
// Counted, not just tracked: a device that leaves the bus and comes back
// is the difference between a keyboard that was disturbed and one that
// was never spoken to.
static volatile uint32_t g_mounts = 0;
static volatile uint32_t g_umounts = 0;

// Asking for a key drives the host stack too, but under the same brake
// as the state reads. Without it a blocking read calls into the stack as
// fast as the processor can loop - measured at 3600 times a frame, which
// at a microsecond each is a fifth of the frame spent on nothing.
static int fj_in_chars(char* buf, int len) {
    usb_pump();
    int n = 0;
    while (n < len) {
        int c = key_pop();
        if (c < 0) break;
        buf[n++] = (char)c;
        g_returned++;
        // A short tail of what actually leaves this driver, so the screen
        // can show whether the translation is right.
        size_t l = strlen(g_tail_txt);
        if (l >= sizeof(g_tail_txt) - 1) {
            memmove(g_tail_txt, g_tail_txt + 1, l);
            l--;
        }
        g_tail_txt[l] = (c >= 32 && c < 127) ? (char)c : '.';
        g_tail_txt[l + 1] = 0;
    }
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t fj_driver = {
    .in_chars = fj_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

static const uint8_t conv_table[128][2] = { HID_KEYCODE_TO_ASCII };

// The German layout as a sparse correction to the US one: only the keys
// whose engraving differs. The values are code points; umlauts and the
// sharp s leave the ring as UTF-8.
static int g_layout_de = 0;

struct de_key { uint8_t code; uint8_t plain; uint8_t shift; };
static const struct de_key DE[] = {
    { HID_KEY_Y, 'z', 'Z' }, { HID_KEY_Z, 'y', 'Y' },
    { HID_KEY_1, '1', '!' }, { HID_KEY_2, '2', '"' },
    { HID_KEY_3, '3', 0xA7 }, { HID_KEY_4, '4', '$' },
    { HID_KEY_5, '5', '%' }, { HID_KEY_6, '6', '&' },
    { HID_KEY_7, '7', '/' }, { HID_KEY_8, '8', '(' },
    { HID_KEY_9, '9', ')' }, { HID_KEY_0, '0', '=' },
    { HID_KEY_MINUS, 0xDF, '?' },
    { HID_KEY_EQUAL, 0xB4, '`' },
    { HID_KEY_BRACKET_LEFT, 0xFC, 0xDC },
    { HID_KEY_BRACKET_RIGHT, '+', '*' },
    { HID_KEY_BACKSLASH, '#', 0x27 },
    { HID_KEY_SEMICOLON, 0xF6, 0xD6 },
    { HID_KEY_APOSTROPHE, 0xE4, 0xC4 },
    { HID_KEY_GRAVE, '^', 0xB0 },
    { HID_KEY_COMMA, ',', ';' },
    { HID_KEY_PERIOD, '.', ':' },
    { HID_KEY_SLASH, '-', '_' },
    { HID_KEY_EUROPE_2, '<', '>' },
};

// The third level of the German layout, reached with AltGr.
struct de_altgr { uint8_t code; uint16_t cp; };
static const struct de_altgr DE_ALTGR[] = {
    { HID_KEY_Q, '@' }, { HID_KEY_E, 0x20AC },
    { HID_KEY_7, '{' }, { HID_KEY_8, '[' }, { HID_KEY_9, ']' }, { HID_KEY_0, '}' },
    { HID_KEY_MINUS, '\\' }, { HID_KEY_BRACKET_RIGHT, '~' },
    { HID_KEY_EUROPE_2, '|' }, { HID_KEY_M, 0xB5 },
};

// 0 is US, anything else German.
void fruitjam_kbd_layout(int de) { g_layout_de = de ? 1 : 0; }
int  fruitjam_kbd_layout_get(void) { return g_layout_de; }

// A key becomes a byte for the ring, or a code point above 127 marked as
// text, which leaves the ring as UTF-8. The two are told apart because
// the editing keys have codes in the same range as the Latin letters.
#define TEXT_CP 0x10000

static void __not_in_flash_func(key_emit)(int code) {
    if (!(code & TEXT_CP)) { key_push((uint8_t)code); return; }
    unsigned cp = (unsigned)(code & 0xFFFF);
    if (cp < 0x80) {
        key_push((uint8_t)cp);
    } else if (cp < 0x800) {
        key_push((uint8_t)(0xC0 | (cp >> 6)));
        key_push((uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        key_push((uint8_t)(0xE0 | (cp >> 12)));
        key_push((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        key_push((uint8_t)(0x80 | (cp & 0x3F)));
    }
}

static int translate(uint8_t keycode, uint8_t modifier) {
    const uint8_t shift = (uint8_t)(modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                                                KEYBOARD_MODIFIER_RIGHTSHIFT));
    const uint8_t ctrl = (uint8_t)(modifier & (KEYBOARD_MODIFIER_LEFTCTRL |
                                               KEYBOARD_MODIFIER_RIGHTCTRL));
    if (g_layout_de && (modifier & KEYBOARD_MODIFIER_RIGHTALT)) {
        for (unsigned i = 0; i < sizeof DE_ALTGR / sizeof DE_ALTGR[0]; i++)
            if (DE_ALTGR[i].code == keycode) return TEXT_CP | DE_ALTGR[i].cp;
    }
    switch (keycode) {
        case HID_KEY_ARROW_LEFT:  return ctrl ? K_CLEFT : shift ? K_SLEFT : K_LEFT;
        case HID_KEY_ARROW_RIGHT: return ctrl ? K_CRIGHT : shift ? K_SRIGHT : K_RIGHT;
        case HID_KEY_ARROW_UP:    return shift ? K_SUP : K_UP;
        case HID_KEY_ARROW_DOWN:  return shift ? K_SDOWN : K_DOWN;
        case HID_KEY_HOME:        return ctrl ? K_CHOME : shift ? K_SHOME : K_HOME;
        case HID_KEY_PAGE_UP:     return K_PGUP;
        case HID_KEY_PAGE_DOWN:   return K_PGDN;
        case HID_KEY_END:         return ctrl ? K_CEND : shift ? K_SEND : K_END;
        case HID_KEY_DELETE:      return K_DEL;
        case HID_KEY_ENTER:
        case HID_KEY_KEYPAD_ENTER: return '\r';
        case HID_KEY_BACKSPACE:   return 8;
        case HID_KEY_TAB:         return shift ? K_STAB : 9;
        case HID_KEY_ESCAPE:      return K_ESC;
        case HID_KEY_F1:          return K_F1;
        default: break;
    }
    if (keycode < 128) {
        uint8_t c = conv_table[keycode][shift ? 1 : 0];
        if (g_layout_de) {
            for (unsigned i = 0; i < sizeof DE / sizeof DE[0]; i++)
                if (DE[i].code == keycode) { c = shift ? DE[i].shift : DE[i].plain; break; }
        }
        // Control codes come from the letter row, the way a terminal makes
        // them: ctrl-c is 3.
        if (c && (modifier & (KEYBOARD_MODIFIER_LEFTCTRL |
                              KEYBOARD_MODIFIER_RIGHTCTRL))) {
            if (c == 'h' || c == 'H') return K_F1;
            if (c >= 'a' && c <= 'z') return c - 'a' + 1;
            if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
        }
        if (c >= 0x80) return TEXT_CP | c;
        return c;
    }
    return 0;
}

// Only keys that were not already down in the previous report count, so
// holding one down does not flood the ring.
// A held key. The keyboard reports a change, not a stream, so repeat is
// ours to make: remember what is down and when it is next due, and let
// the frame loop on core 1 post it again.
static volatile int      g_rep_char = 0;
static volatile uint32_t g_rep_due = 0;
#define REPEAT_DELAY_US  400000u
#define REPEAT_EVERY_US   40000u

// What is held right now, for a game that wants a key's state rather
// than the queue of what was typed.
static hid_keyboard_report_t g_held;

// Reading a state does not go through stdio, so nothing else would run
// the host stack while a game asks - which is exactly what happened:
// the pad was named at startup and then never moved again. Half a
// millisecond is the floor, so a loop that asks eight times a frame
// still only pumps once.
// How long the host stack actually takes when it is asked to run. The
// scanout has 16666 microseconds between interrupts and needs its own
// serviced promptly; anything here that approaches that number is the
// reason the picture lets go.
static uint32_t g_task_calls = 0;
static uint32_t g_task_worst = 0;
static uint32_t g_task_total = 0;
// A single outlier at boot - enumeration, half a second of it - hides
// everything after it behind the worst figure. What matters for a
// picture is how often a call runs longer than a frame, so those are
// counted separately.
static uint32_t g_task_slow = 0;

static void usb_task_timed(void) {
    uint32_t t0 = time_us_32();
    tuh_task();
    g_task_last = t0;
    uint32_t d = time_us_32() - t0;
    g_task_calls++;
    g_task_total += d;
    if (d > g_task_worst) g_task_worst = d;
    if (d > 4000u) g_task_slow++;
}

void fruitjam_dvi_probe_rate(int n) {
    g_fifo_rate = n < 0 ? 0 : (unsigned)n;
}

int fruitjam_dvi_fifo(char* out, int cap, int reset) {
    unsigned mn = g_fifo_min == 0xFFFFFFFFu ? 0 : (unsigned)g_fifo_min;
    int w = snprintf(out, cap, "fifo low %u, dry %u of %u, probe %u",
                     mn, (unsigned)g_fifo_dry, (unsigned)g_fifo_seen,
                     (unsigned)g_fifo_rate);
    if (reset) {
        g_fifo_min = 0xFFFFFFFFu;
        g_fifo_dry = 0;
        g_fifo_seen = 0;
    }
    return w;
}

void fruitjam_pad_rate(int ms) {
    g_pad_rate_ms = ms < 0 ? 0 : (unsigned)ms;
}

int fruitjam_usb_frame(char* out, int cap) {
    unsigned mean = g_f_calls ? (unsigned)(g_f_total / g_f_calls) : 0;
    return snprintf(out, cap,
                    "%u frames, %u late, gap %u us, work %u/%u us, %u mounts %u drops",
                    (unsigned)g_f_calls, (unsigned)g_f_late, (unsigned)g_f_worst,
                    mean, (unsigned)g_f_dur_worst,
                    (unsigned)g_mounts, (unsigned)g_umounts);
}

int fruitjam_usb_time(char* out, int cap) {
    return snprintf(out, cap, "%u calls, %u over 4ms, worst %u us, mean %u us",
                    (unsigned)g_task_calls, (unsigned)g_task_slow,
                    (unsigned)g_task_worst,
                    (unsigned)(g_task_calls ? g_task_total / g_task_calls : 0));
}

// Long running work can drive the stack from here, which keeps the
// keyboard alive across an operation that reads no keys.
void fruitjam_usb_pump(void) { usb_pump(); }

// Often and briefly, not seldom and long. Every call reaches into the
// host controller's registers, which is the bus the DMA feeds the
// picture over, and what hurts a FIFO with one and a third microseconds
// of slack is the length of a single uninterrupted block rather than
// the number of them. Stretching this to two milliseconds took the mean
// call from thirteen microseconds to fifty six and made the blackouts
// worse, which is the same total work in worse shape.
#define PUMP_EVERY_US 500u

static void usb_pump(void) {
    static uint32_t last = 0;
    static uint32_t last_arm = 0;
    uint32_t now = time_us_32();
    if (g_up && (uint32_t)(now - last) >= PUMP_EVERY_US) {
        last = now;
        usb_task_timed();
        if (g_pad_rate_ms &&
            (uint32_t)(now - last_arm) >= g_pad_rate_ms * 1000u) {
            last_arm = now;
            pads_rearm();
        }
    }
}

int fruitjam_kbd_down(int code) {
    usb_pump();
    for (int j = 0; j < 6; j++) {
        uint8_t k = g_held.keycode[j];
        if (!k) continue;
        if (translate(k, g_held.modifier) == code) return 1;
    }
    return 0;
}

static void handle_kbd(const hid_keyboard_report_t* now) {
    static hid_keyboard_report_t before = { 0, 0, { 0 } };
    for (int i = 0; i < 6; i++) {
        uint8_t k = now->keycode[i];
        if (!k) continue;
        bool held = false;
        for (int j = 0; j < 6; j++)
            if (before.keycode[j] == k) { held = true; break; }
        if (held) continue;
        int c = translate(k, now->modifier);
        if (c) {
            key_emit(c);
            g_rep_char = c;
            g_rep_due = time_us_32() + REPEAT_DELAY_US;
        }
    }
    // Nothing held any more, or a different key: the old one stops
    // repeating either way.
    if (g_rep_char) {
        int still = 0;
        for (int j = 0; j < 6; j++) {
            uint8_t k = now->keycode[j];
            if (k && translate(k, now->modifier) == g_rep_char) { still = 1; break; }
        }
        if (!still) g_rep_char = 0;
    }
    g_held = *now;
    before = *now;
}

// Called once a millisecond beside the frame timing.
static void __not_in_flash_func(kbd_repeat_tick)(void) {
    if (!g_rep_char) return;
    uint32_t now = time_us_32();
    // A repeat is only honest while the host stack is being serviced.
    // Behind a long operation that reads no keys, the release that
    // should end it cannot arrive, and the key would otherwise bank up
    // hundreds of characters to be delivered all at once afterwards.
    if ((uint32_t)(now - g_task_last) > 50000u) {
        g_rep_char = 0;
        return;
    }
    if ((int32_t)(now - g_rep_due) < 0) return;
    key_emit(g_rep_char);
    g_rep_due = now + REPEAT_EVERY_US;
}

void tuh_mount_cb(uint8_t dev_addr)   { (void)dev_addr; g_devices++; g_mounts++; }
void tuh_umount_cb(uint8_t dev_addr)  { (void)dev_addr; if (g_devices) g_devices--; g_umounts++; }

// A gamepad is an ordinary HID interface with no boot protocol, so it
// arrives here as HID_ITF_PROTOCOL_NONE. What its report means differs
// per pad, and the only way to find out is to look at the bytes - hence
// the raw capture below, which is what JOY.RAW$ hands over.
#define PAD_MAX      2
#define PAD_REPORT   20

typedef struct {
    uint8_t used;
    uint8_t needs_arm;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t len;
    uint8_t report[PAD_REPORT];
    uint32_t count;
    uint16_t vid, pid;
} FjPad;

static FjPad g_pads[PAD_MAX];

// Every HID interface that is not a keyboard gets a slot, because the
// report descriptor turned out not to be a reliable way to tell a pad
// from a keyboard's volume keys. What is reliable is the traffic: a pad
// sends a report of eight bytes or more, a volume-key interface sends
// nothing until someone presses a volume key. So a slot only becomes a
// joystick once it has spoken, and the numbering below skips the rest.
#define PAD_SPEAKS(p) ((p)->used && (p)->len >= 8)

static int pad_slot(int id) {
    if (id < 0) return -1;
    for (int i = 0; i < PAD_MAX; i++)
        if (PAD_SPEAKS(&g_pads[i]) && id-- == 0) return i;
    return -1;
}


static FjPad* pad_for(uint8_t dev_addr, uint8_t instance) {
    for (int i = 0; i < PAD_MAX; i++)
        if (g_pads[i].used && g_pads[i].dev_addr == dev_addr
            && g_pads[i].instance == instance) return &g_pads[i];
    return NULL;
}

int fruitjam_pad_count(void) {
    usb_pump();
    int n = 0;
    for (int i = 0; i < PAD_MAX; i++) if (PAD_SPEAKS(&g_pads[i])) n++;
    return n;
}

// A DualShock 4 over USB, report 0x01: the four sticks, then the face
// buttons packed above the d-pad hat, then the shoulders, then the
// analogue triggers. Verified against the pad; other pads fall back to
// the same shape, which is what most of them use, and JOY.RAW$ is there
// for the ones that do not.
#define PAD_IS_DS4(p) ((p)->vid == 0x054c)

int fruitjam_pad_present(int idx) { return pad_slot(idx) >= 0; }

int fruitjam_pad_name(int idx, char* out, int cap) {
    int i = pad_slot(idx);
    if (i < 0) return snprintf(out, cap, "");
    if (PAD_IS_DS4(&g_pads[i])) return snprintf(out, cap, "DualShock");
    return snprintf(out, cap, "pad %04x/%04x", g_pads[i].vid, g_pads[i].pid);
}

// Sticks come back as -1 to 1 with the middle eighth treated as centre,
// because a stick at rest does not sit exactly on 128. Triggers are 0
// to 1 and have no such trouble.
double fruitjam_pad_axis(int idx, int axis) {
    usb_pump();
    int i = pad_slot(idx);
    if (i < 0 || g_pads[i].len < 10) return 0.0;
    const uint8_t* r = g_pads[i].report;
    int raw;
    switch (axis) {
        case 0: raw = r[1]; break;
        case 1: raw = r[2]; break;
        case 2: raw = r[3]; break;
        case 3: raw = r[4]; break;
        case 4: return r[8] / 255.0;
        case 5: return r[9] / 255.0;
        default: return 0.0;
    }
    double v = (raw - 128) / 127.0;
    if (v > -0.12 && v < 0.12) return 0.0;
    return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
}

int fruitjam_pad_button(int idx, int btn) {
    usb_pump();
    int i = pad_slot(idx);
    if (i < 0) return 0;
    const uint8_t* r = g_pads[i].report;
    if (btn >= 0 && btn < 4)  return (r[5] >> (4 + btn)) & 1;   // square cross circle triangle
    if (btn >= 4 && btn < 12) return (r[6] >> (btn - 4)) & 1;   // L1 R1 L2 R2 share options L3 R3
    if (btn == 12 || btn == 13) return (r[7] >> (btn - 12)) & 1; // PS, touchpad
    return 0;
}

// The hat as the desktop reports it: 1 up, 2 right, 4 down, 8 left,
// added together on the diagonals.
int fruitjam_pad_hat(int idx) {
    usb_pump();
    int i = pad_slot(idx);
    if (i < 0) return 0;
    static const uint8_t dir[8] = { 1, 1|2, 2, 2|4, 4, 4|8, 8, 8|1 };
    int d = g_pads[i].report[5] & 0x0F;
    return d < 8 ? dir[d] : 0;
}

int fruitjam_pad_raw(int idx, char* out, int cap) {
    usb_pump();
    int s = pad_slot(idx);
    if (s < 0) return snprintf(out, cap, "");
    int at = snprintf(out, cap, "n=%u ", (unsigned)g_pads[s].count);
    for (int i = 0; i < g_pads[s].len && at < cap - 3; i++)
        at += snprintf(out + at, cap - at, "%02x", g_pads[s].report[i]);
    return at;
}

// Every address the stack has, and what it says it is.
int fruitjam_usb_diag(char* out, int cap) {
    int at = snprintf(out, cap, "dev=%u kbd=%u pad=%u",
                      (unsigned)g_devices, (unsigned)g_keyboards,
                      (unsigned)fruitjam_pad_count());
    for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX && at < cap - 24; a++) {
        if (!tuh_mounted(a)) continue;
        uint16_t vid = 0, pid = 0;
        tuh_vid_pid_get(a, &vid, &pid);
        at += snprintf(out + at, cap - at, " [%u:%04x/%04x]",
                       (unsigned)a, vid, pid);
    }
    return at;
}

// Deferred from the report callback, so the pad is polled at the rate a
// game reads it rather than the rate the bus will allow.
static void pads_rearm(void) {
    for (int i = 0; i < PAD_MAX; i++) {
        if (!g_pads[i].used || !g_pads[i].needs_arm) continue;
        g_pads[i].needs_arm = 0;
        tuh_hid_receive_report(g_pads[i].dev_addr, g_pads[i].instance);
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report; (void)desc_len;
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    if (proto == HID_ITF_PROTOCOL_KEYBOARD) {
        g_keyboards++;
    } else if (proto == HID_ITF_PROTOCOL_NONE) {
        for (int i = 0; i < PAD_MAX; i++) {
            if (g_pads[i].used) continue;
            g_pads[i].used = 1;
            g_pads[i].dev_addr = dev_addr;
            g_pads[i].instance = instance;
            g_pads[i].len = 0;
            g_pads[i].count = 0;
            g_pads[i].needs_arm = 0;
            g_pads[i].vid = g_pads[i].pid = 0;
            tuh_vid_pid_get(dev_addr, &g_pads[i].vid, &g_pads[i].pid);
            break;
        }
    }
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && g_keyboards)
        g_keyboards--;
    FjPad* p = pad_for(dev_addr, instance);
    if (p) p->used = 0;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    FjPad* pad = pad_for(dev_addr, instance);
    if (pad) {
        pad->len = (uint8_t)(len > PAD_REPORT ? PAD_REPORT : len);
        memcpy(pad->report, report, pad->len);
        pad->count++;
    }
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && len >= sizeof(hid_keyboard_report_t)) {
        handle_kbd((const hid_keyboard_report_t*)report);
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }

    // A pad is asked again on a timer rather than straight away. Every
    // packet makes the USB library spin on PIO registers, and that is
    // the same bus the DMA uses to feed the serialiser's FIFO - which
    // holds eight words, about one and a third microseconds of picture.
    // Enough of that traffic empties it, and an empty FIFO breaks the
    // image without moving a single frame boundary. A game wants the
    // stick a hundred times a second; the bus was being asked a thousand.
    if (pad) pad->needs_arm = 1;
    else tuh_hid_receive_report(dev_addr, instance);
}


// Nothing but the frame, once a millisecond, and the key repeat that
// hangs off the same clock. The host stack itself is not driven here:
// it is started on core 0 and has to be run from there, which two
// attempts at moving it established - split across the cores it
// enumerates nothing at all.
static void __not_in_flash_func(core1_usb_frames)(void) {
    // This loop and everything it reaches live in RAM, which is what
    // lets it keep running while core 0 erases a flash sector. That
    // matters: a sector takes fifty milliseconds, a USB device that
    // hears nothing for three goes to sleep, and nothing wakes it
    // again - which is how a save from the editor used to cost the
    // keyboard. The library's own frame path was already written this
    // way; only these two were not.
    //
    // Registered as a lockout victim anyway, so anything that does ask
    // for the safe wrapper still gets it.
    flash_safe_execute_core_init();
    while (true) {
        uint32_t t = timer_hw->timerawl;
        // What the frame timing on this core actually was. The display's
        // counters watch core 0 and say nothing about here, and a USB
        // frame that arrives late is invisible from over there - which
        // is exactly the gap to close when the picture and the keyboard
        // fail together.
        if (g_f_last) {
            uint32_t d = t - g_f_last;
            g_f_calls++;
            if (d > 2000u) {
                g_f_late++;
                if (d > g_f_worst) g_f_worst = d;
            }
        }
        g_f_last = t;
        pio_usb_host_frame();
        uint32_t dur = timer_hw->timerawl - t;
        g_f_total += dur;
        if (dur > g_f_dur_worst) g_f_dur_worst = dur;
        kbd_repeat_tick();

        // The rest of the millisecond watches the serialiser's FIFO. It
        // is filled by DMA and drained at the pixel clock, and if it ever
        // runs dry the picture breaks while every frame boundary still
        // arrives exactly on time - the one failure the frame counters
        // cannot see. This core has the time: the USB frame above costs
        // twenty seven microseconds of the thousand.
        unsigned probes = 0;
        while ((timer_hw->timerawl - t) < 1000) {
            if (probes < g_fifo_rate) {
                probes++;
                uint32_t st = hstx_fifo_hw->stat;
                if (st & HSTX_FIFO_STAT_EMPTY_BITS) g_fifo_dry++;
                uint32_t lvl = st & HSTX_FIFO_STAT_LEVEL_BITS;
                if (lvl < g_fifo_min) g_fifo_min = lvl;
                g_fifo_seen++;
            } else {
                tight_loop_contents();
            }
        }
    }
}

// Registering the keyboard as a console driver costs nothing and cannot
// fail. Bringing the host up is a separate act, on request, so that a
// board whose host stack misbehaves still comes up with a prompt to ask
// what happened.
void fruitjam_usb_init(void) {
    stdio_set_driver_enabled(&fj_driver, true);
    // Asserting the pull-up is what makes a PC notice the board. Harmless
    // if the device stack already did it.
    tud_connect();
    // A keyboard you have to ask for is no use: asking would take a
    // keyboard. USB.START stays for bringing it back up by hand.
    fruitjam_usb_start();
}

int fruitjam_usb_start(void) {
    if (g_up) return 1;

    gpio_init(USB_5V_PIN);
    gpio_set_dir(USB_5V_PIN, true);
    gpio_put(USB_5V_PIN, 1);

    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = USB_DP_PIN;

    // PIO_USB_DMA_TX_DEFAULT is 0, and the library takes that channel
    // without asking. Channel 0 is the scanout's pixel channel, so the
    // default reprograms the picture out from under itself. Claim a free
    // one, hand the number over, and release it again for the library to
    // take.
    int tx = dma_claim_unused_channel(true);
    cfg.tx_ch = (uint8_t)tx;
    dma_channel_unclaim(tx);

    cfg.skip_alarm_pool = true;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);

    g_up = tuh_init(1);
    if (g_up) multicore_launch_core1(core1_usb_frames);
    return g_up;
}
