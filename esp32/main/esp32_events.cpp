// Event sources on the board, feeding the VM's own event system:
// ON "TICK" CALL Handler and friends. The interrupts only record what
// happened; the VM drains the record at a safe point in its loop, so a
// handler never runs inside an ISR.

#include <stdio.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "../../src/vm.h"

bool esp32_pin_allowed(int pin, const char** why);

static esp_timer_handle_t g_timer = nullptr;
static bool g_timer_on = false;
static volatile uint32_t g_ticks = 0;

// Pin edges, oldest first. A full ring drops the newest edge rather
// than overwriting history.
#define PIN_QUEUE 16
static volatile uint8_t g_pin_no[PIN_QUEUE];
static volatile uint8_t g_pin_level[PIN_QUEUE];
static volatile uint8_t g_pin_head = 0, g_pin_tail = 0;

static bool g_key_watch = false;
static bool g_isr_service = false;
static volatile uint32_t g_isr_raw = 0;

static void timer_isr(void*) {
    g_ticks++;
}

static void IRAM_ATTR gpio_isr(void* arg) {
    int pin = (int)(intptr_t)arg;
    g_isr_raw++;
    uint8_t next = (uint8_t)((g_pin_head + 1) % PIN_QUEUE);
    if (next == g_pin_tail) return;
    g_pin_no[g_pin_head] = (uint8_t)pin;
    g_pin_level[g_pin_head] = (uint8_t)gpio_get_level((gpio_num_t)pin);
    g_pin_head = next;
}

// Handlers do not nest. A handler that SLEEPs outlives its own period,
// and SLEEP polls events again - without this the second call lands on
// top of the first and the board disappears into itself.
struct PollGuard {
    static bool busy;
    bool taken;
    PollGuard() : taken(!busy) { if (taken) busy = true; }
    ~PollGuard() { if (taken) busy = false; }
};
bool PollGuard::busy = false;

void esp32_event_poll(VM& vm) {
    PollGuard guard;
    if (!guard.taken) return;

    if (g_ticks) {
        // One handler call per poll, however many periods elapsed: a
        // slow handler must not build a backlog it can never work off.
        g_ticks = 0;
        vm.event_raise("TICK", {});
        if (vm.is_halted) return;
    }

    while (g_pin_tail != g_pin_head) {
        int pin = g_pin_no[g_pin_tail];
        int level = g_pin_level[g_pin_tail];
        g_pin_tail = (uint8_t)((g_pin_tail + 1) % PIN_QUEUE);
        vm.event_raise("PIN", { Value::make_i64(pin), Value::make_i64(level) });
        if (vm.is_halted) return;
    }

    if (g_key_watch) {
        int c = getchar();
        if (c != EOF) vm.event_raise("KEY", { Value::make_i64(c) });
    }
}

void register_esp32_events(VM& vm) {
    vm.register_native("TIMER.EVERY", 1, 1, [](const std::vector<Value>& args) -> Value {
        int ms = (int)args[0].to_double();
        if (g_timer_on) { esp_timer_stop(g_timer); g_timer_on = false; }
        g_ticks = 0;
        if (ms <= 0) return Value::make_i64(0);
        if (!g_timer) {
            esp_timer_create_args_t cfg = {};
            cfg.callback = timer_isr;
            cfg.name = "jdb_tick";
            if (esp_timer_create(&cfg, &g_timer) != ESP_OK) return Value::make_i64(-1);
        }
        g_timer_on = esp_timer_start_periodic(g_timer, (uint64_t)ms * 1000) == ESP_OK;
        return Value::make_i64(g_timer_on ? 0 : -1);
    });

    vm.register_native("TIMER.STOP", 0, 0, [](const std::vector<Value>&) -> Value {
        if (g_timer_on) { esp_timer_stop(g_timer); g_timer_on = false; }
        g_ticks = 0;
        return Value();
    });

    // edge: 1 rising, 2 falling, 3 both, 0 stop watching
    vm.register_native("GPIO.WATCH", 2, 2, [](const std::vector<Value>& args) -> Value {
        int pin = (int)args[0].to_double();
        int edge = (int)args[1].to_double();
        const char* why = nullptr;
        if (!esp32_pin_allowed(pin, &why)) throw std::runtime_error(why);

        if (!edge) {
            gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
            gpio_isr_handler_remove((gpio_num_t)pin);
            return Value::make_i64(0);
        }
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.intr_type = (edge == 1) ? GPIO_INTR_POSEDGE
                      : (edge == 2) ? GPIO_INTR_NEGEDGE
                                    : GPIO_INTR_ANYEDGE;
        if (gpio_config(&cfg) != ESP_OK) return Value::make_i64(-1);
        if (!g_isr_service) {
            if (gpio_install_isr_service(0) != ESP_OK) return Value::make_i64(-1);
            g_isr_service = true;
        }
        gpio_isr_handler_add((gpio_num_t)pin, gpio_isr, (void*)(intptr_t)pin);
        return Value::make_i64(0);
    });

    vm.register_native("PIN.DIAG$", 0, 0, [](const std::vector<Value>&) -> Value {
        char buf[64];
        snprintf(buf, sizeof buf, "isr=%u head=%d tail=%d",
                 (unsigned)g_isr_raw, (int)g_pin_head, (int)g_pin_tail);
        return Value::make_string(buf);
    });

    vm.register_native("KEY.WATCH", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_key_watch = args[0].to_double() != 0;
        return Value();
    });
}
