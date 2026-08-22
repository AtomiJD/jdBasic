// The hardware families beyond bare GPIO: ADC, PWM, I2C, SPI and a
// small PIO surface. All of them exist on every board in the family,
// so they register unconditionally. Buses and pins are the caller's
// choice - on a PicoCalc, i2c1 is the keyboard, spi1 the display and
// spi0 the SD card, so user peripherals belong on i2c0.

#include "../src/vm.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/pio.h"

// ── ADC ──────────────────────────────────────────────────────────────
// Channels 0..3 sit on GP26..29; channel 4 is the on-die temperature
// sensor.

static bool g_adc_up = false;

static uint16_t adc_sample(int ch) {
    if (!g_adc_up) { adc_init(); g_adc_up = true; }
    if (ch >= 0 && ch <= 3) adc_gpio_init(26 + ch);
    if (ch == 4) adc_set_temp_sensor_enabled(true);
    adc_select_input(ch);
    return adc_read();
}

// ── I2C / SPI instance lookup ────────────────────────────────────────

static i2c_inst_t* i2c_bus(int n) { return n == 1 ? i2c1 : i2c0; }
static spi_inst_t* spi_bus(int n) { return n == 1 ? spi1 : spi0; }

// A data argument is an array of byte values or a single number.
static void value_bytes(const Value& v, std::vector<uint8_t>& out) {
    if (v.type == ValueType::ARRAY) {
        for (auto& e : v.as_array()->elements)
            out.push_back((uint8_t)e.to_double());
    } else {
        out.push_back((uint8_t)v.to_double());
    }
}

static Value bytes_value(const uint8_t* data, size_t n) {
    Value arr = Value::make_array();
    auto& el = arr.as_array()->elements;
    el.reserve(n);
    for (size_t i = 0; i < n; i++) el.push_back(Value::make_i64(data[i]));
    return arr;
}

// ── PIO ──────────────────────────────────────────────────────────────
// One block, pio0. LOAD places raw instruction words and returns the
// offset; START wires a state machine to a pin and lets it run. PUT
// and GET are non-blocking so a stuck FIFO can never hang the REPL.

static Value pio_load(const Value& v) {
    if (v.type != ValueType::ARRAY) return Value::make_i64(-1);
    auto& el = v.as_array()->elements;
    if (el.empty() || el.size() > 32) return Value::make_i64(-1);
    uint16_t instrs[32];
    for (size_t i = 0; i < el.size(); i++)
        instrs[i] = (uint16_t)el[i].to_double();
    pio_program_t prog;
    prog.instructions = instrs;
    prog.length = (uint8_t)el.size();
    prog.origin = -1;
    if (!pio_can_add_program(pio0, &prog)) return Value::make_i64(-1);
    return Value::make_i64(pio_add_program(pio0, &prog));
}

static void pio_start(int sm, int off, int len, int pin, double div) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, off, off + len - 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_clkdiv(&c, div > 0 ? (float)div : 1.0f);
    pio_gpio_init(pio0, pin);
    pio_sm_set_consecutive_pindirs(pio0, sm, pin, 1, true);
    pio_sm_init(pio0, sm, off, &c);
    pio_sm_set_enabled(pio0, sm, true);
}

// ── Registration ─────────────────────────────────────────────────────

void register_pico_hw(VM& vm) {
    vm.register_native("ADC.READ", 1, 1, [](const std::vector<Value>& args) -> Value {
        return Value::make_i64(adc_sample((int)args[0].to_double()));
    });
    vm.register_native("ADC.TEMP", 0, 0, [](const std::vector<Value>&) -> Value {
        double volts = adc_sample(4) * 3.3 / 4096.0;
        return Value::make_f64(27.0 - (volts - 0.706) / 0.001721);
    });

    vm.register_native("PWM.SET", 2, 3, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        double freq = args[1].to_double();
        double duty = args.size() >= 3 ? args[2].to_double() : 50.0;
        if (freq < 8 || freq > 62500000 || duty < 0 || duty > 100)
            return Value::make_i64(-1);
        gpio_set_function(pin, GPIO_FUNC_PWM);
        unsigned slice = pwm_gpio_to_slice_num(pin);
        uint32_t clk = clock_get_hz(clk_sys);
        uint32_t div = 1;
        uint32_t wrap = (uint32_t)(clk / freq);
        while (wrap / div > 65535) div++;
        pwm_config c = pwm_get_default_config();
        pwm_config_set_clkdiv_int(&c, div);
        pwm_config_set_wrap(&c, (uint16_t)(wrap / div));
        pwm_init(slice, &c, true);
        pwm_set_gpio_level(pin, (uint16_t)((wrap / div) * duty / 100.0));
        return Value::make_i64(0);
    });
    vm.register_native("PWM.OFF", 1, 1, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        pwm_set_gpio_level(pin, 0);
        pwm_set_enabled(pwm_gpio_to_slice_num(pin), false);
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, false);
        return Value();
    });

    vm.register_native("I2C.SETUP", 3, 4, [](const std::vector<Value>& args) -> Value {
        i2c_inst_t* bus = i2c_bus((int)args[0].to_double());
        int sda = (int)args[1].to_double();
        int scl = (int)args[2].to_double();
        int khz = args.size() >= 4 ? (int)args[3].to_double() : 100;
        i2c_init(bus, khz * 1000);
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(sda);
        gpio_pull_up(scl);
        return Value();
    });
    vm.register_native("I2C.WRITE", 3, 3, [](const std::vector<Value>& args) -> Value {
        std::vector<uint8_t> data;
        value_bytes(args[2], data);
        int rc = i2c_write_timeout_us(i2c_bus((int)args[0].to_double()),
                                      (uint8_t)args[1].to_double(),
                                      data.data(), data.size(), false, 50000);
        return Value::make_i64(rc);
    });
    vm.register_native("I2C.READ", 3, 3, [](const std::vector<Value>& args) -> Value {
        int n = (int)args[2].to_double();
        if (n < 1 || n > 4096) return Value::make_array();
        std::vector<uint8_t> data(n);
        int rc = i2c_read_timeout_us(i2c_bus((int)args[0].to_double()),
                                     (uint8_t)args[1].to_double(),
                                     data.data(), n, false, 50000);
        if (rc < 0) return Value::make_array();
        return bytes_value(data.data(), rc);
    });
    vm.register_native("I2C.SCAN", 1, 1, [](const std::vector<Value>& args) -> Value {
        i2c_inst_t* bus = i2c_bus((int)args[0].to_double());
        Value arr = Value::make_array();
        auto& el = arr.as_array()->elements;
        for (int addr = 0x08; addr < 0x78; addr++) {
            uint8_t probe;
            if (i2c_read_timeout_us(bus, addr, &probe, 1, false, 5000) >= 0)
                el.push_back(Value::make_i64(addr));
        }
        return arr;
    });

    vm.register_native("SPI.SETUP", 4, 5, [](const std::vector<Value>& args) -> Value {
        spi_inst_t* bus = spi_bus((int)args[0].to_double());
        int sck  = (int)args[1].to_double();
        int mosi = (int)args[2].to_double();
        int miso = (int)args[3].to_double();
        int khz  = args.size() >= 5 ? (int)args[4].to_double() : 1000;
        spi_init(bus, khz * 1000);
        gpio_set_function(sck, GPIO_FUNC_SPI);
        gpio_set_function(mosi, GPIO_FUNC_SPI);
        if (miso >= 0) gpio_set_function(miso, GPIO_FUNC_SPI);
        return Value();
    });
    vm.register_native("SPI.XFER", 2, 2, [](const std::vector<Value>& args) -> Value {
        std::vector<uint8_t> out;
        value_bytes(args[1], out);
        std::vector<uint8_t> in(out.size());
        spi_write_read_blocking(spi_bus((int)args[0].to_double()),
                                out.data(), in.data(), out.size());
        return bytes_value(in.data(), in.size());
    });

    vm.register_native("PIO.LOAD", 1, 1, [](const std::vector<Value>& args) -> Value {
        return pio_load(args[0]);
    });
    vm.register_native("PIO.START", 4, 5, [](const std::vector<Value>& args) -> Value {
        pio_start((int)args[0].to_double(), (int)args[1].to_double(),
                  (int)args[2].to_double(), (int)args[3].to_double(),
                  args.size() >= 5 ? args[4].to_double() : 1.0);
        return Value();
    });
    vm.register_native("PIO.STOP", 1, 1, [](const std::vector<Value>& args) -> Value {
        pio_sm_set_enabled(pio0, (int)args[0].to_double(), false);
        return Value();
    });
    vm.register_native("PIO.PUT", 2, 2, [](const std::vector<Value>& args) -> Value {
        int sm = (int)args[0].to_double();
        if (pio_sm_is_tx_fifo_full(pio0, sm)) return Value::make_i64(0);
        pio_sm_put(pio0, sm, (uint32_t)args[1].to_double());
        return Value::make_i64(1);
    });
    vm.register_native("PIO.GET", 1, 1, [](const std::vector<Value>& args) -> Value {
        int sm = (int)args[0].to_double();
        if (pio_sm_is_rx_fifo_empty(pio0, sm)) return Value::make_i64(-1);
        return Value::make_i64(pio_sm_get(pio0, sm));
    });
}
