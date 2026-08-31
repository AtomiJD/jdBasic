// A USB keyboard on the board's own sockets, reaching jdBasic as ordinary
// console input.
//
// The host controller is PIO-USB on GP1 and GP2, with the 5V rail to the
// sockets switched by GP11, and the board's own hub fans out to the three
// A connectors.
//
// The work is split across the cores. Left to itself the library drives
// its 1 ms frame from an alarm interrupt, and that starves the timer the
// SDK uses to service the USB *device* side: the console then never
// enumerates, silently, because enumeration has millisecond deadlines.
// So the frame runs on core 1, which has nothing else to do now that the
// scanout is a DMA command list.
//
// The host stack itself is pumped from the stdio driver on core 0. The
// prompt spends its time in getchar_timeout_us, which polls every driver
// in turn, so asking for a key is also what services USB - and while a
// program runs without asking for input, nothing needs servicing either.
//
// Keys arrive as HID usage codes and leave as bytes in a ring the stdio
// layer drains, so the prompt and its line editor treat the keyboard
// exactly like a terminal on the serial port. Arrows and the editing keys
// use the PicoCalc's codes, which is what that editor already understands.

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "hardware/dma.h"
#include "pico/multicore.h"
#include "hardware/structs/usb.h"
#include "hardware/irq.h"
#include "pio_usb.h"
#include "tusb.h"

void fruitjam_dvi_trace(const char* s);
void fruitjam_dvi_status(int row, const char* s);
int  fruitjam_usb_start(void);

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
static uint8_t g_keyboards = 0;
static uint32_t g_keys = 0;
static uint8_t  g_last_key = 0;
static uint32_t g_returned = 0;
static char     g_tail_txt[12] = {0};
static uint8_t g_devices = 0;
static bool    g_up = false;

static void key_push(uint8_t c) {
    uint16_t next = (uint16_t)((g_head + 1) % KEYRING);
    if (next == g_tail) return;
    g_ring[g_head] = c;
    g_head = next;
    g_keys++;
    g_last_key = c;
}

static int key_pop(void) {
    if (g_tail == g_head) return -1;
    uint8_t c = g_ring[g_tail];
    g_tail = (uint16_t)((g_tail + 1) % KEYRING);
    return c;
}

int fruitjam_usb_keys_waiting(void) { return g_head != g_tail; }
int fruitjam_usb_key_count(void)     { return (int)g_keys; }
int fruitjam_usb_keyboards(void)    { return g_keyboards; }
int fruitjam_usb_devices(void)      { return g_devices; }

static uint32_t g_polls = 0;

// Everything worth knowing about both USB roles, refreshed twice a second
// from the poll the prompt is already doing.
static void status_tick(void) {
    static uint32_t last = 0;
    uint32_t now = time_us_32();
    if (now - last < 500000) return;
    last = now;
    char b[64];
    snprintf(b, sizeof b, "dev  inited=%d mounted=%d susp=%d",
             tud_inited() ? 1 : 0, tud_mounted() ? 1 : 0, tud_suspended() ? 1 : 0);
    fruitjam_dvi_status(0, b);
    snprintf(b, sizeof b, "cdc  connected=%d   polls=%lu",
             stdio_usb_connected() ? 1 : 0, (unsigned long)g_polls);
    fruitjam_dvi_status(1, b);
    snprintf(b, sizeof b, "host up=%d devices=%d keyboards=%d",
             g_up ? 1 : 0, g_devices, g_keyboards);
    fruitjam_dvi_status(2, b);
    // The bus itself. PULLUP_EN in sie_ctrl is what tells a PC that
    // anything is plugged in at all.
    snprintf(b, sizeof b, "sie=%08lx main=%08lx pull=%d",
             (unsigned long)usb_hw->sie_ctrl, (unsigned long)usb_hw->main_ctrl,
             (usb_hw->sie_ctrl & USB_SIE_CTRL_PULLUP_EN_BITS) ? 1 : 0);
    fruitjam_dvi_status(3, b);
    // sof counts frames the PC sends us. Standing still means the host is
    // not talking to this device at all; climbing means it is, and the
    // fault is further up.
    snprintf(b, sizeof b, "keys=%lu out=%lu last=%u [%s]",
             (unsigned long)g_keys, (unsigned long)g_returned,
             g_last_key, g_tail_txt);
    fruitjam_dvi_status(4, b);
    snprintf(b, sizeof b, "inte=%08lx irq=%d sof=%lu",
             (unsigned long)usb_hw->inte, irq_is_enabled(USBCTRL_IRQ) ? 1 : 0,
             (unsigned long)(usb_hw->sof_rd & 0x7ff));
    fruitjam_dvi_status(5, b);
}

// Asking for a key is what drives the host stack.
static int fj_in_chars(char* buf, int len) {
    g_polls++;
    status_tick();
    if (g_up) tuh_task();
    int n = 0;
    while (n < len) {
        int c = key_pop();
        if (c < 0) break;
        buf[n++] = (char)c;
        g_returned++;
        // A short tail of what actually leaves this driver, so the screen
        // can show whether the translation is right.
        size_t l = strlen(g_tail_txt);
        if (l >= sizeof(g_tail_txt) - 1) {
            memmove(g_tail_txt, g_tail_txt + 1, l);
            l--;
        }
        g_tail_txt[l] = (c >= 32 && c < 127) ? (char)c : '.';
        g_tail_txt[l + 1] = 0;
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

void tuh_mount_cb(uint8_t dev_addr)   { (void)dev_addr; g_devices++; }
void tuh_umount_cb(uint8_t dev_addr)  { (void)dev_addr; if (g_devices) g_devices--; }

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report; (void)desc_len;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD)
        g_keyboards++;
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && g_keyboards)
        g_keyboards--;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD
        && len >= sizeof(hid_keyboard_report_t))
        handle_kbd((const hid_keyboard_report_t*)report);
    tuh_hid_receive_report(dev_addr, instance);
}


// Nothing but the frame, once a millisecond. The library does the bit
// level work in PIO and DMA; this only has to be punctual.
static void core1_usb_frames(void) {
    while (true) {
        uint32_t t = timer_hw->timerawl;
        pio_usb_host_frame();
        while ((timer_hw->timerawl - t) < 1000) tight_loop_contents();
    }
}

// Registering the keyboard as a console driver costs nothing and cannot
// fail. Bringing the host up is a separate act, on request, so that a
// board whose host stack misbehaves still comes up with a prompt to ask
// what happened.
void fruitjam_usb_init(void) {
    stdio_set_driver_enabled(&fj_driver, true);
    // Asserting the pull-up is what makes a PC notice the board. Harmless
    // if the device stack already did it.
    tud_connect();
    // A keyboard you have to ask for is no use: asking would take a
    // keyboard. USB.START stays for bringing it back up by hand.
    fruitjam_usb_start();
}

int fruitjam_usb_start(void) {
    if (g_up) return 1;

    gpio_init(USB_5V_PIN);
    gpio_set_dir(USB_5V_PIN, true);
    gpio_put(USB_5V_PIN, 1);

    fruitjam_dvi_trace("usb: configuring host");
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = USB_DP_PIN;

    // PIO_USB_DMA_TX_DEFAULT is 0, and the library takes that channel
    // without asking. Channel 0 is the scanout's pixel channel, so the
    // default reprograms the picture out from under itself. Claim a free
    // one, hand the number over, and release it again for the library to
    // take.
    int tx = dma_claim_unused_channel(true);
    cfg.tx_ch = (uint8_t)tx;
    dma_channel_unclaim(tx);

    cfg.skip_alarm_pool = true;
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &cfg);

    fruitjam_dvi_trace("usb: starting host");
    g_up = tuh_init(1);
    fruitjam_dvi_trace(g_up ? "usb: host up" : "usb: host FAILED");
    if (g_up) {
        multicore_launch_core1(core1_usb_frames);
        fruitjam_dvi_trace("usb: frames on core 1");
    }
    return g_up;
}
