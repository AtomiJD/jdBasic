// The board's own I2C bus, shared by the two parts that sit on it: the
// FT6336G touch controller at 0x38 and the ES8311 audio codec at 0x18.
// Both were found by scanning, not by assumption.
//
// One bus can only have one driver, and the two ESP-IDF I2C drivers
// cannot even coexist in one binary - it aborts at boot with "CONFLICT!
// driver_ng is not allowed to be used with this old driver". jdBasic's
// own I2C verbs use i2c_master, so everything here does too, and the
// vendored codec driver was pointed at these two functions rather than
// the old API it was written against.

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define PIN_SDA 16
#define PIN_SCL 15
#define MAX_DEV 4

static i2c_master_bus_handle_t g_bus;
static i2c_master_dev_handle_t g_dev[MAX_DEV];
static int g_addr[MAX_DEV];
static int g_n = 0;

int es3c28p_i2c_up(void) {
    if (g_bus) return 0;
    i2c_master_bus_config_t bc = {0};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = PIN_SDA;
    bc.scl_io_num = PIN_SCL;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bc, &g_bus) == ESP_OK ? 0 : -1;
}

int es3c28p_i2c_ready(void) { return g_bus != NULL; }

// One handle per address, kept, because adding and removing a device for
// every register write would cost more than the transfer.
static i2c_master_dev_handle_t dev_for(int addr) {
    if (es3c28p_i2c_up() != 0) return NULL;
    for (int i = 0; i < g_n; i++)
        if (g_addr[i] == addr) return g_dev[i];
    if (g_n >= MAX_DEV) return NULL;
    i2c_device_config_t dc = {0};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = (uint16_t)addr;
    dc.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(g_bus, &dc, &g_dev[g_n]) != ESP_OK) return NULL;
    g_addr[g_n] = addr;
    return g_dev[g_n++];
}

esp_err_t es3c28p_i2c_write(int addr, const uint8_t* data, int n) {
    i2c_master_dev_handle_t d = dev_for(addr);
    if (!d) return ESP_FAIL;
    return i2c_master_transmit(d, data, (size_t)n, 100);
}

esp_err_t es3c28p_i2c_read(int addr, uint8_t reg, uint8_t* out, int n) {
    i2c_master_dev_handle_t d = dev_for(addr);
    if (!d) return ESP_FAIL;
    return i2c_master_transmit_receive(d, &reg, 1, out, (size_t)n, 100);
}

int es3c28p_i2c_read_reg(int addr, uint8_t reg, uint8_t* out, int n) {
    return es3c28p_i2c_read(addr, reg, out, n) == ESP_OK ? 0 : -2;
}
