// Event sources on the board, feeding the VM's own event system:
// ON "TICK" CALL Handler and friends. The interrupts only record what
// happened; the VM drains the record at a safe point in its loop, so a
// handler never runs inside an ISR.

#include "../src/vm.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

static repeating_timer_t g_timer;
static bool g_timer_on = false;
static volatile uint32_t g_ticks = 0;

// Pin edges, oldest first. A full ring drops the newest edge rather
// than overwriting history.
#define PIN_QUEUE 16
static volatile uint8_t g_pin_no[PIN_QUEUE];
static volatile uint8_t g_pin_level[PIN_QUEUE];
static volatile uint8_t g_pin_head = 0, g_pin_tail = 0;

static bool g_key_watch = false;
static volatile uint32_t g_isr_raw = 0;

static bool timer_isr(repeating_timer_t*) {
    g_ticks++;
    return true;
}

static void gpio_isr(uint gpio, uint32_t events) {
    g_isr_raw++;
    uint8_t next = (uint8_t)((g_pin_head + 1) % PIN_QUEUE);
    if (next == g_pin_tail) return;
    g_pin_no[g_pin_head] = (uint8_t)gpio;
    g_pin_level[g_pin_head] = (events & GPIO_IRQ_EDGE_RISE) ? 1 : 0;
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

void pico_event_poll(VM& vm) {
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
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            vm.event_raise("KEY", { Value::make_i64(c) });
        }
    }
}

void register_pico_events(VM& vm) {
    vm.register_native("TIMER.EVERY", 1, 1, [](const std::vector<Value>& args) -> Value {
        int ms = (int)args[0].to_double();
        if (g_timer_on) { cancel_repeating_timer(&g_timer); g_timer_on = false; }
        g_ticks = 0;
        if (ms <= 0) return Value::make_i64(0);
        g_timer_on = add_repeating_timer_ms(-ms, timer_isr, nullptr, &g_timer);
        return Value::make_i64(g_timer_on ? 0 : -1);
    });
    vm.register_native("TIMER.STOP", 0, 0, [](const std::vector<Value>&) -> Value {
        if (g_timer_on) { cancel_repeating_timer(&g_timer); g_timer_on = false; }
        g_ticks = 0;
        return Value();
    });
    // edge: 1 rising, 2 falling, 3 both
    vm.register_native("GPIO.WATCH", 2, 2, [](const std::vector<Value>& args) -> Value {
        unsigned pin = (unsigned)args[0].to_double();
        int edge = (int)args[1].to_double();
        if (pin > 29) return Value::make_i64(-1);
        uint32_t mask = 0;
        if (edge & 1) mask |= GPIO_IRQ_EDGE_RISE;
        if (edge & 2) mask |= GPIO_IRQ_EDGE_FALL;
        if (!mask) {
            gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
            return Value::make_i64(0);
        }
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_set_irq_enabled_with_callback(pin, mask, true, gpio_isr);
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
