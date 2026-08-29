// Pins, converters, buses. The verbs are the RP2350 ones so a program
// reads the same on either board; what differs is which numbers are
// yours to use.

#include <stdio.h>
#include <string.h>
#include <string>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/temperature_sensor.h"
#include "esp_adc/adc_oneshot.h"

#include "../../src/vm.h"

// GPIO 26 to 32 carry the SPI flash and 33 to 37 the octal PSRAM. The
// board works because nobody touches them, and a program that does
// takes the whole thing down with no diagnostic, so the answer has to
// come before the write rather than after it.
bool esp32_pin_allowed(int pin, const char** why) {
    if (pin < 0 || pin > 48) { *why = "pin: the S3 has GPIO 0 to 48"; return false; }
    if (pin >= 26 && pin <= 32) { *why = "pin: 26 to 32 are the SPI flash"; return false; }
    if (pin >= 33 && pin <= 37) { *why = "pin: 33 to 37 are the octal PSRAM"; return false; }
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    if (pin == 43 || pin == 44) { *why = "pin: 43 and 44 are the console UART"; return false; }
#else
    if (pin == 19 || pin == 20) { *why = "pin: 19 and 20 are the native USB"; return false; }
#endif
    return true;
}

static void need_pin(int pin) {
    const char* why = nullptr;
    if (!esp32_pin_allowed(pin, &why)) throw std::runtime_error(why);
}

// ── Converters ───────────────────────────────────────────────

static adc_oneshot_unit_handle_t s_adc = nullptr;
static temperature_sensor_handle_t s_tsens = nullptr;

// ADC1 sits on GPIO 1 to 10, in order, so the pin is the channel.
static int adc_channel_of(int pin) {
    return (pin >= 1 && pin <= 10) ? pin - 1 : -1;
}

// ── Pulse width ──────────────────────────────────────────────

// One LEDC channel per pin, handed out in order of first use.
#define PWM_SLOTS 8
static int s_pwm_pin[PWM_SLOTS];
static bool s_pwm_used[PWM_SLOTS];
static bool s_pwm_timer_ready = false;

static int pwm_slot_for(int pin) {
    for (int i = 0; i < PWM_SLOTS; i++) if (s_pwm_used[i] && s_pwm_pin[i] == pin) return i;
    for (int i = 0; i < PWM_SLOTS; i++) if (!s_pwm_used[i]) { s_pwm_used[i] = true; s_pwm_pin[i] = pin; return i; }
    return -1;
}

// ── Buses ────────────────────────────────────────────────────

#define I2C_BUSES 2
static i2c_master_bus_handle_t s_i2c[I2C_BUSES];

#define SPI_BUSES 2
static spi_device_handle_t s_spi[SPI_BUSES];
static bool s_spi_up[SPI_BUSES];

static spi_host_device_t spi_host_of(int bus) {
    return bus == 1 ? SPI3_HOST : SPI2_HOST;
}

void register_esp32_hw(VM& vm) {
    // ── GPIO ─────────────────────────────────────────────────
    vm.register_native("GPIO.MODE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        need_pin(pin);
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = args[1].to_double() != 0 ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
        gpio_config(&cfg);
        return Value();
    });

    vm.register_native("GPIO.WRITE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        need_pin(pin);
        gpio_set_level((gpio_num_t)pin, args[1].to_double() != 0);
        return Value();
    });

    vm.register_native("GPIO.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        need_pin(pin);
        return Value::make_i64(gpio_get_level((gpio_num_t)pin) ? 1 : 0);
    });

    vm.register_native("GPIO.PULLUP", 1, 2, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        need_pin(pin);
        bool on = args.size() >= 2 ? args[1].to_double() != 0 : true;
        if (on) gpio_pullup_en((gpio_num_t)pin); else gpio_pullup_dis((gpio_num_t)pin);
        return Value();
    });

    // ── ADC ──────────────────────────────────────────────────
    vm.register_native("ADC.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        int ch = adc_channel_of(pin);
        if (ch < 0) throw std::runtime_error("ADC.READ: the converter is on GPIO 1 to 10");
        if (!s_adc) {
            adc_oneshot_unit_init_cfg_t init = {};
            init.unit_id = ADC_UNIT_1;
            if (adc_oneshot_new_unit(&init, &s_adc) != ESP_OK)
                throw std::runtime_error("ADC.READ: converter unavailable");
        }
        adc_oneshot_chan_cfg_t cc = {};
        cc.bitwidth = ADC_BITWIDTH_DEFAULT;
        cc.atten = ADC_ATTEN_DB_12;
        adc_oneshot_config_channel(s_adc, (adc_channel_t)ch, &cc);
        int raw = 0;
        if (adc_oneshot_read(s_adc, (adc_channel_t)ch, &raw) != ESP_OK)
            throw std::runtime_error("ADC.READ: conversion failed");
        return Value::make_i64(raw);
    });

    vm.register_native("ADC.TEMP", 0, 0, [](const std::vector<Value>&) -> Value {
        if (!s_tsens) {
            temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
            if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK)
                throw std::runtime_error("ADC.TEMP: sensor unavailable");
            temperature_sensor_enable(s_tsens);
        }
        float c = 0;
        if (temperature_sensor_get_celsius(s_tsens, &c) != ESP_OK)
            throw std::runtime_error("ADC.TEMP: read failed");
        return Value::make_f64(c);
    });

    // ── PWM ──────────────────────────────────────────────────
    vm.register_native("PWM.SET", 2, 3, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        need_pin(pin);
        int freq = (int)args[1].to_double();
        double duty = args.size() >= 3 ? args[2].to_double() : 50.0;
        if (freq <= 0) throw std::runtime_error("PWM.SET: frequency must be positive");
        int slot = pwm_slot_for(pin);
        if (slot < 0) throw std::runtime_error("PWM.SET: all eight channels are in use");

        ledc_timer_config_t t = {};
        t.speed_mode = LEDC_LOW_SPEED_MODE;
        t.duty_resolution = LEDC_TIMER_10_BIT;
        t.timer_num = LEDC_TIMER_0;
        t.freq_hz = (uint32_t)freq;
        t.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&t) != ESP_OK)
            throw std::runtime_error("PWM.SET: frequency out of range for 10 bits");
        s_pwm_timer_ready = true;

        if (duty < 0) duty = 0;
        if (duty > 100) duty = 100;
        ledc_channel_config_t c = {};
        c.gpio_num = pin;
        c.speed_mode = LEDC_LOW_SPEED_MODE;
        c.channel = (ledc_channel_t)slot;
        c.timer_sel = LEDC_TIMER_0;
        c.duty = (uint32_t)(duty * 1023.0 / 100.0);
        ledc_channel_config(&c);
        return Value::make_i64(0);
    });

    vm.register_native("PWM.OFF", 1, 1, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        for (int i = 0; i < PWM_SLOTS; i++) {
            if (!s_pwm_used[i] || s_pwm_pin[i] != pin) continue;
            if (s_pwm_timer_ready) ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, 0);
            s_pwm_used[i] = false;
        }
        return Value();
    });

    // ── I2C ──────────────────────────────────────────────────
    vm.register_native("I2C.SETUP", 3, 4, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_double();
        if (bus < 0 || bus >= I2C_BUSES) throw std::runtime_error("I2C.SETUP: bus is 0 or 1");
        int sda = (int)args[1].to_double();
        int scl = (int)args[2].to_double();
        need_pin(sda);
        need_pin(scl);
        if (s_i2c[bus]) { i2c_del_master_bus(s_i2c[bus]); s_i2c[bus] = nullptr; }
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = bus;
        cfg.sda_io_num = (gpio_num_t)sda;
        cfg.scl_io_num = (gpio_num_t)scl;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = true;
        if (i2c_new_master_bus(&cfg, &s_i2c[bus]) != ESP_OK)
            throw std::runtime_error("I2C.SETUP: bus would not open");
        return Value::make_i64(0);
    });

    // Speed is per device on this chip rather than per bus, so it is
    // named where the transfer is rather than where the pins are.
    vm.register_native("I2C.WRITE", 3, 4, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_double();
        if (bus < 0 || bus >= I2C_BUSES || !s_i2c[bus])
            throw std::runtime_error("I2C.WRITE: call I2C.SETUP first");
        int addr = (int)args[1].to_double();
        int hz = args.size() >= 4 ? (int)args[3].to_double() : 100000;

        std::vector<uint8_t> bytes;
        if (args[2].type == ValueType::ARRAY)
            for (const auto& e : args[2].as_array()->elements)
                bytes.push_back((uint8_t)e.to_double());
        else {
            std::string s = args[2].to_string();
            bytes.assign(s.begin(), s.end());
        }

        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = (uint16_t)addr;
        dc.scl_speed_hz = (uint32_t)hz;
        i2c_master_dev_handle_t dev = nullptr;
        if (i2c_master_bus_add_device(s_i2c[bus], &dc, &dev) != ESP_OK)
            return Value::make_i64(-1);
        esp_err_t rc = i2c_master_transmit(dev, bytes.data(), bytes.size(), 100);
        i2c_master_bus_rm_device(dev);
        return Value::make_i64(rc == ESP_OK ? (int)bytes.size() : -1);
    });

    vm.register_native("I2C.READ", 3, 4, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_double();
        if (bus < 0 || bus >= I2C_BUSES || !s_i2c[bus])
            throw std::runtime_error("I2C.READ: call I2C.SETUP first");
        int addr = (int)args[1].to_double();
        int n = (int)args[2].to_double();
        int hz = args.size() >= 4 ? (int)args[3].to_double() : 100000;
        if (n <= 0 || n > 512) throw std::runtime_error("I2C.READ: ask for 1 to 512 bytes");

        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = (uint16_t)addr;
        dc.scl_speed_hz = (uint32_t)hz;
        i2c_master_dev_handle_t dev = nullptr;
        if (i2c_master_bus_add_device(s_i2c[bus], &dc, &dev) != ESP_OK)
            return Value::make_array();
        std::vector<uint8_t> buf(n);
        esp_err_t rc = i2c_master_receive(dev, buf.data(), n, 100);
        i2c_master_bus_rm_device(dev);

        Value out = Value::make_array();
        if (rc != ESP_OK) return out;
        for (int i = 0; i < n; i++) out.as_array()->elements.push_back(Value::make_i64(buf[i]));
        return out;
    });

    vm.register_native("I2C.SCAN", 1, 1, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_double();
        if (bus < 0 || bus >= I2C_BUSES || !s_i2c[bus])
            throw std::runtime_error("I2C.SCAN: call I2C.SETUP first");
        Value out = Value::make_array();
        for (int a = 1; a < 127; a++)
            if (i2c_master_probe(s_i2c[bus], a, 50) == ESP_OK)
                out.as_array()->elements.push_back(Value::make_i64(a));
        return out;
    });

    // ── SPI ──────────────────────────────────────────────────
    vm.register_native("SPI.SETUP", 4, 5, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_double();
        if (bus < 0 || bus >= SPI_BUSES) throw std::runtime_error("SPI.SETUP: bus is 0 or 1");
        int sck = (int)args[1].to_double();
        int mosi = (int)args[2].to_double();
        int miso = (int)args[3].to_double();
        int hz = args.size() >= 5 ? (int)args[4].to_double() : 1000000;
        need_pin(sck);
        need_pin(mosi);
        if (miso >= 0) need_pin(miso);

        if (s_spi_up[bus]) {
            spi_bus_remove_device(s_spi[bus]);
            spi_bus_free(spi_host_of(bus));
            s_spi_up[bus] = false;
        }
        spi_bus_config_t bc = {};
        bc.mosi_io_num = mosi;
        bc.miso_io_num = miso;
        bc.sclk_io_num = sck;
        bc.quadwp_io_num = -1;
        bc.quadhd_io_num = -1;
        bc.max_transfer_sz = 4096;
        if (spi_bus_initialize(spi_host_of(bus), &bc, SPI_DMA_CH_AUTO) != ESP_OK)
            throw std::runtime_error("SPI.SETUP: bus would not open");
        spi_device_interface_config_t dc = {};
        dc.clock_speed_hz = hz;
        dc.mode = 0;
        dc.spics_io_num = -1;
        dc.queue_size = 1;
        if (spi_bus_add_device(spi_host_of(bus), &dc, &s_spi[bus]) != ESP_OK) {
            spi_bus_free(spi_host_of(bus));
            throw std::runtime_error("SPI.SETUP: device would not attach");
        }
        s_spi_up[bus] = true;
        return Value::make_i64(0);
    });

    // Full duplex: as many bytes come back as went out.
    vm.register_native("SPI.XFER", 2, 2, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_double();
        if (bus < 0 || bus >= SPI_BUSES || !s_spi_up[bus])
            throw std::runtime_error("SPI.XFER: call SPI.SETUP first");
        std::vector<uint8_t> tx;
        if (args[1].type == ValueType::ARRAY)
            for (const auto& e : args[1].as_array()->elements)
                tx.push_back((uint8_t)e.to_double());
        else {
            std::string s = args[1].to_string();
            tx.assign(s.begin(), s.end());
        }
        if (tx.empty()) return Value::make_array();
        std::vector<uint8_t> rx(tx.size());

        spi_transaction_t t = {};
        t.length = tx.size() * 8;
        t.tx_buffer = tx.data();
        t.rx_buffer = rx.data();
        Value out = Value::make_array();
        if (spi_device_transmit(s_spi[bus], &t) != ESP_OK) return out;
        for (size_t i = 0; i < rx.size(); i++)
            out.as_array()->elements.push_back(Value::make_i64(rx[i]));
        return out;
    });

    vm.register_native("PIN.FREE", 0, 0, [](const std::vector<Value>&) -> Value {
        std::string s;
        const char* why = nullptr;
        for (int p = 0; p <= 48; p++) {
            if (!esp32_pin_allowed(p, &why)) continue;
            if (!s.empty()) s += " ";
            s += std::to_string(p);
        }
        return Value::make_string(s);
    });
}
