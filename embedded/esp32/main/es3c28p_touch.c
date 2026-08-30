// The capacitive touch screen: an FT6336G on I2C at 0x38, with its own
// reset and interrupt lines. It reports in the panel's native portrait
// frame, 240 by 320, while the screen is turned to landscape, so the
// mapping from one to the other lives here and TOUCH.RAW exists to
// check it against a finger rather than against an assumption.

#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define PIN_SDA   16
#define PIN_SCL   15
#define PIN_INT   17
#define PIN_RST   18

#define FT_ADDR   0x38
#define FT_TD_STATUS 0x02
#define FT_P1_XH     0x03
#define FT_CHIP_ID   0xA3
#define FT_VENDOR_ID 0xA8

// Native frame of the panel the controller sits on, before the screen
// is turned.
#define TP_W 240
#define TP_H 320

extern int es3c28p_i2c_up(void);
extern int es3c28p_i2c_read_reg(int addr, uint8_t reg, uint8_t* out, int n);

static int g_ready = 0;

static int rd(uint8_t reg, uint8_t* out, size_t n) {
    return es3c28p_i2c_read_reg(FT_ADDR, reg, out, (int)n);
}

int es3c28p_touch_ready(void) { return g_ready; }

int es3c28p_touch_init(void) {
    if (g_ready) return 0;

    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << PIN_RST);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    io.pin_bit_mask = (1ULL << PIN_INT);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    // Low resets the controller; it needs a moment on either side.
    gpio_set_level((gpio_num_t)PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    if (es3c28p_i2c_up() != 0) return -1;

    uint8_t id = 0;
    if (rd(FT_CHIP_ID, &id, 1) != 0) return -3;

    g_ready = 1;
    return 0;
}

// Who the controller says it is: chip id and vendor id. FocalTech
// answers 0x11 as the vendor, which is the one value here with an
// answer known in advance.
int es3c28p_touch_id(int* chip, int* vendor) {
    uint8_t a = 0, b = 0;
    *chip = *vendor = -1;
    if (!g_ready) return -1;
    if (rd(FT_CHIP_ID, &a, 1) != 0) return -2;
    if (rd(FT_VENDOR_ID, &b, 1) != 0) return -2;
    *chip = a; *vendor = b;
    return 0;
}

// The controller's own numbers, untouched. What a calibration argues
// about is the mapping below, not this.
int es3c28p_touch_raw(int* n, int* x, int* y) {
    uint8_t buf[7];
    *n = 0; *x = *y = -1;
    if (!g_ready) return -1;
    if (rd(FT_TD_STATUS, buf, 1) != 0) return -2;
    int count = buf[0] & 0x0F;
    if (count > 2) count = 0;          // a stale register reads 0xFF
    *n = count;
    if (count == 0) return 0;
    if (rd(FT_P1_XH, buf, 4) != 0) return -2;
    *x = ((buf[0] & 0x0F) << 8) | buf[1];
    *y = ((buf[2] & 0x0F) << 8) | buf[3];
    return 0;
}

// Native portrait to the landscape the screen is turned to. MADCTL's MV
// swaps the axes, so the controller's Y is the screen's X; the flip on
// the other axis is what a finger in a corner decides.
void es3c28p_touch_map(int rx, int ry, int* sx, int* sy) {
    *sx = ry;
    *sy = TP_W - 1 - rx;
}

int es3c28p_touch_read(int* n, int* x, int* y) {
    int rx, ry;
    int rc = es3c28p_touch_raw(n, &rx, &ry);
    *x = *y = -1;
    if (rc != 0 || *n == 0) return rc;
    es3c28p_touch_map(rx, ry, x, y);
    return 0;
}
