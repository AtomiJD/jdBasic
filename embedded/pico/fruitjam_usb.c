// A USB keyboard on the board's own sockets, reaching jdBasic as ordinary
// console input.
//
// The host controller is PIO-USB on GP1 and GP2, with the 5V rail to the
// sockets switched by GP11. It lives on core 1 alongside the scanout: the
// video interrupt outranks everything and costs a few percent, and what is
// left is more than the host stack needs.
//
// Keys arrive as HID usage codes and leave as bytes in a ring the stdio
// layer drains, so the prompt and its line editor treat the keyboard
// exactly like a terminal on the serial port. Arrows and the editing keys
// use the PicoCalc's codes, which is what that editor already understands.

#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pio_usb.h"
#include "tusb.h"

#define USB_DP_PIN      1
#define USB_5V_PIN     11

#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5

#define KEYRING 64
static volatile uint8_t  g_ring[KEYRING];
static volatile uint16_t g_head = 0, g_tail = 0;
static uint8_t g_mounted = 0;

static void key_push(uint8_t c) {
    uint16_t next = (uint16_t)((g_head + 1) % KEYRING);
    if (next == g_tail) return;
    g_ring[g_head] = c;
    g_head = next;
}

static int key_pop(void) {
    if (g_tail == g_head) return -1;
    uint8_t c = g_ring[g_tail];
    g_tail = (uint16_t)((g_tail + 1) % KEYRING);
    return c;
}

int fruitjam_usb_keys_waiting(void) { return g_head != g_tail; }
int fruitjam_usb_mounted(void)      { return g_mounted; }

static int fj_in_chars(char* buf, int len) {
    int n = 0;
    while (n < len) {
        int c = key_pop();
        if (c < 0) break;
        buf[n++] = (char)c;
    }
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t fj_driver = {
    .in_chars = fj_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

static const uint8_t conv_table[128][2] = { HID_KEYCODE_TO_ASCII };

static uint8_t translate(uint8_t keycode, uint8_t modifier) {
    const uint8_t shift = (uint8_t)(modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                                                KEYBOARD_MODIFIER_RIGHTSHIFT));
    switch (keycode) {
        case HID_KEY_ARROW_LEFT:  return K_LEFT;
        case HID_KEY_ARROW_RIGHT: return K_RIGHT;
        case HID_KEY_ARROW_UP:    return K_UP;
        case HID_KEY_ARROW_DOWN:  return K_DOWN;
        case HID_KEY_HOME:        return K_HOME;
        case HID_KEY_END:         return K_END;
        case HID_KEY_DELETE:      return K_DEL;
        case HID_KEY_ENTER:
        case HID_KEY_KEYPAD_ENTER: return '\r';
        case HID_KEY_BACKSPACE:   return 8;
        case HID_KEY_TAB:         return 9;
        case HID_KEY_ESCAPE:      return 27;
        default: break;
    }
    if (keycode < 128) {
        uint8_t c = conv_table[keycode][shift ? 1 : 0];
        // Control codes come from the letter row, the way a terminal makes
        // them: ctrl-c is 3.
        if (c && (modifier & (KEYBOARD_MODIFIER_LEFTCTRL |
                              KEYBOARD_MODIFIER_RIGHTCTRL))) {
            if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 1);
            if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 1);
        }
        return c;
    }
    return 0;
}

// Only keys that were not already down in the previous report count, so
// holding one down does not flood the ring.
static void handle_kbd(const hid_keyboard_report_t* now) {
    static hid_keyboard_report_t before = { 0, 0, { 0 } };
    for (int i = 0; i < 6; i++) {
        uint8_t k = now->keycode[i];
        if (!k) continue;
        bool held = false;
        for (int j = 0; j < 6; j++)
            if (before.keycode[j] == k) { held = true; break; }
        if (held) continue;
        uint8_t c = translate(k, now->modifier);
        if (c) key_push(c);
    }
    before = *now;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report; (void)desc_len;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD)
        g_mounted++;
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && g_mounted)
        g_mounted--;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && len >= sizeof(hid_keyboard_report_t))
        handle_kbd((const hid_keyboard_report_t*)report);
    tuh_hid_receive_report(dev_addr, instance);
}

// Runs on core 1, before the task loop: the PIO host controller has to be
// set up by the core that will service it.
void fruitjam_dvi_trace(const char* s);

void fruitjam_usb_core1_init(void) {
    fruitjam_dvi_trace("2a powering sockets");
    gpio_init(USB_5V_PIN);
    gpio_set_dir(USB_5V_PIN, true);
    gpio_put(USB_5V_PIN, 1);

    fruitjam_dvi_trace("2b configuring pio-usb");
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = USB_DP_PIN;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);
    fruitjam_dvi_trace("2c tuh_init");
    tuh_init(1);
}

void fruitjam_usb_core1_task(void) { tuh_task(); }

void fruitjam_usb_stdio_init(void) { stdio_set_driver_enabled(&fj_driver, true); }
