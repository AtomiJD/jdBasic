// The REPL: a prompt feeding the embedded VM, one persistent
// interpreter for the whole session. Console I/O runs through the
// SDK's stdio, which carries both USB-CDC and - on a PicoCalc - the
// screen and keyboard, registered here as one more stdio driver.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "pico/cyw43_arch.h"
#include "jdb_embed_api.h"

extern "C" void jdb_pico_fs_init(void);
extern "C" void picocalc_lcd_init(void);
extern "C" void picocalc_lcd_putc(char c);
extern "C" void picocalc_kbd_init(void);
extern "C" int  picocalc_kbd_poll(void);

static void pc_out_chars(const char* buf, int len) {
    for (int i = 0; i < len; i++) picocalc_lcd_putc(buf[i]);
}

static int pc_in_chars(char* buf, int len) {
    int n = 0;
    while (n < len) {
        int c = picocalc_kbd_poll();
        if (c < 0) break;
        buf[n++] = (char)c;
    }
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t pc_driver = {
    .out_chars = pc_out_chars,
    .in_chars = pc_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

static void read_line(char* buf, int cap) {
    int n = 0;
    for (;;) {
        int c = getchar();
        if (c == '\r' || c == '\n') {
            putchar('\r');
            putchar('\n');
            break;
        }
        if (c == 8 || c == 127) {
            if (n > 0) {
                n--;
                printf("\b \b");
            }
            continue;
        }
        if (c >= 32 && c < 127 && n < cap - 1) {
            buf[n++] = (char)c;
            putchar(c);
        }
    }
    buf[n] = 0;
}

int main() {
    stdio_init_all();
    cyw43_arch_init();
    jdb_pico_fs_init();
    picocalc_lcd_init();
    picocalc_kbd_init();
    stdio_set_driver_enabled(&pc_driver, true);

    // A host may be listening on USB; standalone, nothing is, and the
    // prompt should not wait for one.
    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(200);

    printf("\r\njdBasic on RP2350\r\n");

    JdbEmbed* vm = jdb_embed_init();
    if (!vm) {
        printf("VM init failed\r\n");
        for (;;) sleep_ms(1000);
    }

    static char line[1024];
    for (;;) {
        printf("> ");
        read_line(line, sizeof line);
        if (!line[0]) continue;
        char* out = jdb_embed_eval(vm, line);
        if (out) {
            fputs(out, stdout);
            jdb_embed_free(out);
        } else {
            const char* err = jdb_embed_last_error(vm);
            printf("ERROR: %s\r\n", err ? err : "unknown");
        }
    }
}
