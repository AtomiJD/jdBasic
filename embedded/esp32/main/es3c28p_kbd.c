// The M5Stack CardKB v1.1 on the board's I2C bus: an ATmega8A at 0x5F
// that answers a one-byte read with a key, or with zero when it has
// none. No register and no event byte - its firmware keeps a ring and
// hands over one code per read.
//
// The codes are already the ones the editor speaks, because the CardKB
// and the PicoCalc's controller agree on them: 27 escape, 8 delete,
// 9 tab, 13 enter, 180 to 183 for the arrows. What the CardKB has no
// key for is control, and the Fn layer carries it instead - the
// firmware sends 128 to 175 there, one value per physical key, so the
// position is the code minus 128 and the rest follows from the key that
// sits there.
//
// It runs off the 3.3 V of the I2C connector rather than the 5 V a
// Grove port would give it. The ATmega8A is specified from 2.7 V up
// as long as it stays under 8 MHz, which the internal oscillator does,
// and 3.3 V is also what keeps its pullups off the S3's pins.

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_timer.h"

#define KBD_ADDR 0x5F
#define KBD_HZ   100000
#define PROBE_US 2000000

#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_F1    0xC1
#define K_STAB  0xC2
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5
#define K_PGUP  0xD6
#define K_PGDN  0xD7

extern esp_err_t es3c28p_i2c_recv(int addr, uint8_t* out, int n, int hz);
extern i2c_master_bus_handle_t es3c28p_i2c_bus(void);

// The unshifted key at each position, in the order the CardKB firmware
// scans them, so a Fn code indexes straight into it.
static const unsigned char g_base[48] = {
    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 8,
    9,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0,
    K_LEFT, K_UP,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 13,
    K_DOWN, K_RIGHT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', ' '
};

// Fn turns the arrows into the far-movement keys, delete into a forward
// delete, tab into a back tab, h into help, and every other letter into
// its control code - so Ctrl-C to break and Ctrl-S to save are reachable
// on a keyboard that has no control key.
static int fn_key(int code) {
    int i = code - 128;
    if (i < 0 || i >= 48) return -1;
    int base = g_base[i];
    switch (base) {
        case K_LEFT:  return K_HOME;
        case K_RIGHT: return K_END;
        case K_UP:    return K_PGUP;
        case K_DOWN:  return K_PGDN;
        case 8:       return K_DEL;
        case 9:       return K_STAB;
        case 'h':     return K_F1;
    }
    if (base >= 'a' && base <= 'z') return base - 'a' + 1;
    return -1;
}

static int g_present = 0;
static int64_t g_next_probe = 0;

// Probed rather than assumed, and probed again every couple of seconds
// while it is missing, so a keyboard plugged in after boot starts
// working without a reset. Between probes an absent keyboard costs
// nothing, which matters because the poll below sits in the read path.
static int ready(void) {
    if (g_present) return 1;
    int64_t now = esp_timer_get_time();
    if (now < g_next_probe) return 0;
    g_next_probe = now + PROBE_US;
    i2c_master_bus_handle_t bus = es3c28p_i2c_bus();
    if (!bus) return 0;
    g_present = i2c_master_probe(bus, KBD_ADDR, 20) == ESP_OK;
    return g_present;
}

int es3c28p_kbd_ready(void) { return g_present; }

// The codes as they arrive, before the Fn layer is folded in, so a key
// whose meaning is in doubt can be held down and read back rather than
// guessed at.
static uint8_t g_raw[16];
static int g_rawn = 0;

int es3c28p_kbd_rawget(uint8_t* out, int cap) {
    int n = g_rawn < cap ? g_rawn : cap;
    for (int i = 0; i < n; i++) out[i] = g_raw[i];
    g_rawn = 0;
    return n;
}

// One key, or -1 when the keyboard has none or is not there.
int es3c28p_kbd_poll(void) {
    if (!ready()) return -1;
    uint8_t b = 0;
    if (es3c28p_i2c_recv(KBD_ADDR, &b, 1, KBD_HZ) != ESP_OK) {
        g_present = 0;
        return -1;
    }
    if (b == 0) return -1;
    if (g_rawn < (int)sizeof g_raw) g_raw[g_rawn++] = b;
    if (b >= 128 && b < 176) return fn_key(b);
    return b;
}
