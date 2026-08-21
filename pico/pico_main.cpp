// The REPL: a serial prompt over USB-CDC feeding the embedded VM. One
// persistent interpreter for the whole session, the way the desktop
// REPL keeps its state.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/cyw43_arch.h"
#include "jdb_embed_api.h"

extern "C" void jdb_pico_fs_init(void);

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
    while (!stdio_usb_connected()) sleep_ms(100);
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
