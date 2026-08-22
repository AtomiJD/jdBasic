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
int picocalc_kbd_poll(void) {
    uint8_t reg = 0x09;
    uint16_t buff = 0;

    if (i2c_write_timeout_us(KBD_I2C, KBD_ADDR, &reg, 1, false, 20000) < 0)
        return -1;
    sleep_ms(2);
    if (i2c_read_timeout_us(KBD_I2C, KBD_ADDR, (uint8_t*)&buff, 2, false, 20000) < 0)
        return -1;

    if (buff == 0) return -1;
    if (buff == 0x7e02) { g_ctrl = 1; return -1; }
    if (buff == 0x7e03) { g_ctrl = 0; return -1; }
    if ((buff & 0xFF) != 1) return -1;

    int c = buff >> 8;
    // Modifier keys arrive as key events of their own; they carry no text.
    if (c >= 0xA1 && c <= 0xA5) return -1;
    if (g_ctrl && c >= 'a' && c <= 'z') c = c - 'a' + 1;
    return c;
}
