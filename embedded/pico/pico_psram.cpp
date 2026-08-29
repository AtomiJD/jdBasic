// Is there a PSRAM chip on this board, and which one?
//
// The PicoCalc wires its ESP-PSRAM64H to ordinary GPIOs (SI GP2, SO GP3,
// IO2 GP4, IO3 GP5, CS GP20, SCK GP21), not to the QMI bus, so the
// RP2350 cannot map it into the address space: only chip select may sit
// on a GPIO, and only on 0, 8 or 19. Clock and data would have to share
// the flash pins. So this is a plain SPI device, and the most useful
// thing to know first is whether it is fitted at all.
//
// Read ID (0x9F) answers with a manufacturer byte, a known-good-die
// byte and six of identity. Bit-banged in one go because the part is
// DRAM behind an SPI face and refreshes itself: chip select must not
// stay low much beyond eight microseconds.

#include "../../src/vm.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

#define PS_SI   2
#define PS_SO   3
#define PS_IO2  4
#define PS_IO3  5
#define PS_CS  20
#define PS_SCK 21

static inline void ps_clock_out(uint8_t byte) {
    for (int b = 7; b >= 0; b--) {
        gpio_put(PS_SI, (byte >> b) & 1);
        gpio_put(PS_SCK, 1);
        gpio_put(PS_SCK, 0);
    }
}

static inline uint8_t ps_clock_in(void) {
    uint8_t v = 0;
    for (int b = 7; b >= 0; b--) {
        gpio_put(PS_SCK, 1);
        v = (uint8_t)((v << 1) | (gpio_get(PS_SO) ? 1 : 0));
        gpio_put(PS_SCK, 0);
    }
    return v;
}

static void psram_read_id(uint8_t out[8]) {
    for (int pin : { PS_SI, PS_SCK, PS_CS }) {
        gpio_init(pin);
        gpio_set_dir(pin, true);
    }
    gpio_init(PS_SO);
    gpio_set_dir(PS_SO, false);
    // In single-SPI mode the upper two lines are inputs on the chip and
    // must not float; a pull-up is the polite way to hold them.
    for (int pin : { PS_IO2, PS_IO3 }) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_up(pin);
    }

    gpio_put(PS_CS, 1);
    gpio_put(PS_SCK, 0);
    sleep_us(10);

    gpio_put(PS_CS, 0);
    ps_clock_out(0x9F);
    ps_clock_out(0x00);
    ps_clock_out(0x00);
    ps_clock_out(0x00);
    for (int i = 0; i < 8; i++) out[i] = ps_clock_in();
    gpio_put(PS_CS, 1);
}

void register_pico_psram(VM& vm) {
    vm.register_native("PSRAM.ID$", 0, 0, [](const std::vector<Value>&) -> Value {
        uint8_t id[8];
        psram_read_id(id);
        char buf[64];
        int at = snprintf(buf, sizeof buf, "%02x %02x  eid",
                          id[0], id[1]);
        for (int i = 2; i < 8 && at < 55; i++)
            at += snprintf(buf + at, sizeof buf - at, " %02x", id[i]);
        return Value::make_string(buf);
    });
}
