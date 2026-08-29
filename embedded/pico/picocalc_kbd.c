// The PicoCalc's keyboard: an STM32 on i2c1 answering register 0x09
// with an event byte and a key code. Polled, no blocking waits.

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define KBD_I2C   i2c1
#define KBD_SDA   6
#define KBD_SCL   7
#define KBD_HZ    10000
#define KBD_ADDR  0x1F

static int g_ctrl = 0;

void picocalc_kbd_init(void) {
    i2c_init(KBD_I2C, KBD_HZ);
    gpio_set_function(KBD_SDA, GPIO_FUNC_I2C);
    gpio_set_function(KBD_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_SDA);
    gpio_pull_up(KBD_SCL);
}

// One poll: the pressed key's code, or -1 when nothing is down.
// One poll: the pressed key's code, or -1 when nothing is down. The
// controller reports press (1), hold (2) and release (3); holds repeat,
// releases only matter for the modifiers.
// One poll: the pressed key's code, or -1 when nothing is down. The
// controller repeats a held key on every poll; a throttle turns that
// into a sane typematic rate.
// One poll: the pressed key's code, or -1 when nothing is down. The
// controller repeats a held key on every poll; a throttle turns that
// into a typematic rate - first repeat after 400 ms, then every 80.
int picocalc_kbd_poll(void) {
    uint8_t reg = 0x09;
    uint16_t buff = 0;

    if (i2c_write_timeout_us(KBD_I2C, KBD_ADDR, &reg, 1, false, 20000) < 0)
        return -1;
    sleep_ms(2);
    if (i2c_read_timeout_us(KBD_I2C, KBD_ADDR, (uint8_t*)&buff, 2, false, 20000) < 0)
        return -1;

    if (buff == 0) return -1;
    { void picocalc_kbd_rawlog(uint16_t); picocalc_kbd_rawlog(buff); }

    int state = buff & 0xFF;
    int c = buff >> 8;

    if (c == 0xA5) { g_ctrl = (state != 3); return -1; }
    if (c >= 0xA1 && c <= 0xA4) return -1;

    static uint32_t press_ms = 0;
    static uint32_t last_out_ms = 0;
    static int held = -1;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (state == 1) {
        held = c;
        press_ms = now;
        last_out_ms = now;
    } else if (state == 2) {
        if (c != held) { held = c; press_ms = now; last_out_ms = now; }
        else {
            if (now - press_ms < 400) return -1;
            if (now - last_out_ms < 80) return -1;
            last_out_ms = now;
        }
    } else {
        if (c == held) held = -1;
        return -1;
    }

    if (g_ctrl && c >= 'a' && c <= 'z') c = c - 'a' + 1;
    if (g_ctrl && c >= 'A' && c <= 'Z') c = c - 'A' + 1;
    return c;
}

// The last raw words from the controller, for diagnosis from the prompt.
static uint16_t g_raw[16];
static int g_rawn = 0;

void picocalc_kbd_rawlog(uint16_t w) {
    if (g_rawn < 16) g_raw[g_rawn++] = w;
}

int picocalc_kbd_rawget(uint16_t* out, int cap) {
    int n = g_rawn < cap ? g_rawn : cap;
    for (int i = 0; i < n; i++) out[i] = g_raw[i];
    g_rawn = 0;
    return n;
}
