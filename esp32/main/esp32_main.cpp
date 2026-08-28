// jdBasic on the ESP32-S3: a serial REPL on the native USB port, and
// nothing else yet. No display, no keyboard, no filesystem. What this
// build exists to answer is how much room the interpreter leaves on a
// 512 KB part, with and without PSRAM behind it.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include "jdb_embed_api.h"

static void console_init() {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 1024;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    // A terminal sends CR for the return key and expects CRLF back.
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    setvbuf(stdin, NULL, _IONBF, 0);
}

static size_t free_internal() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static size_t free_psram() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static void report(const char* label) {
    printf("%-12s internal %7u  largest %7u  psram %8u\r\n", label,
           (unsigned)free_internal(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)free_psram());
    fflush(stdout);
}

// The board's stdio does not echo: read by character, show what
// arrives, honour backspace, stop at return.
static void read_line(char* buf, size_t cap) {
    size_t len = 0;
    for (;;) {
        int ch = getchar();
        if (ch == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (ch == '\r' || ch == '\n') break;
        if (ch == 8 || ch == 127) {
            if (len > 0) { len--; printf("\b \b"); fflush(stdout); }
            continue;
        }
        if (ch < 32 || len + 1 >= cap) continue;
        buf[len++] = (char)ch;
        putchar(ch);
        fflush(stdout);
    }
    buf[len] = 0;
    printf("\r\n");
}

extern "C" void app_main(void) {
    console_init();

    // A host may be listening on USB; standalone, nothing is, and the
    // prompt should not wait for one.
    vTaskDelay(pdMS_TO_TICKS(600));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\r\njdBasic on ESP32-S%d rev v%d.%d, %d core%s\r\n",
           chip.model == CHIP_ESP32S3 ? 3 : 2,
           chip.revision / 100, chip.revision % 100,
           chip.cores, chip.cores == 1 ? "" : "s");

    size_t before_int = free_internal();
    size_t before_psram = free_psram();
    report("boot");

    JdbEmbed* vm = jdb_embed_init();
    if (!vm) {
        printf("VM init failed\r\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    jdb_embed_output_stdout(vm);

    report("after init");
    printf("VM costs    internal %7d  psram %8d\r\n",
           (int)before_int - (int)free_internal(),
           (int)before_psram - (int)free_psram());

    static char line[1024];
    for (;;) {
        printf("> ");
        fflush(stdout);
        read_line(line, sizeof line);
        if (!line[0]) continue;

        char* out = jdb_embed_eval(vm, line);
        if (out) {
            if (out[0]) printf("%s", out);
            jdb_embed_free(out);
        } else {
            const char* err = jdb_embed_last_error(vm);
            printf("ERROR: %s\r\n", err ? err : "unknown");
        }
        fflush(stdout);
    }
}
