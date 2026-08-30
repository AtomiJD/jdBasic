// jdBasic on the ESP32-S3: a serial REPL on the native USB port, with
// the flash store behind it. No display and no keyboard yet.

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"

#include "jdb_embed_api.h"

extern "C" bool esp32_fs_init(void);
void esp32_note_after_init(void);
void syntax_print(const char* s, int n);

#define AUTORUN_CFG "autorun.cfg"

static char g_current[128];

// A name without an extension may mean the .jdb of that name. What was
// typed wins, so a file that really has no extension is still reachable.
extern "C" int  es3c28p_con_on(void);
extern "C" void es3c28p_con_size(int* cols, int* rows);
void pico_editor(const char* name);

static const char* resolve_name(const char* name, char* buf, size_t cap) {
    FILE* f = fopen(name, "r");
    if (f) { fclose(f); return name; }
    if (strchr(name, '.')) return name;
    snprintf(buf, cap, "%s.jdb", name);
    f = fopen(buf, "r");
    if (f) { fclose(f); return buf; }
    return name;
}

static void console_init() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 1024;
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    // A terminal sends CR for the return key and expects CRLF back.
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#else
    // Without a driver the reader takes bytes straight out of the 128-byte
    // hardware FIFO, and RECV sleeps ten milliseconds whenever it finds it
    // empty. At 115200 baud ten milliseconds is 115 bytes, so an upload
    // arriving at line speed overruns the FIFO and loses whatever landed
    // during the sleep. The driver's interrupt collects into a ring buffer
    // instead, and four kilobytes is thirty times the gap.
    uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 4096, 0, 0, NULL, 0);
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    // The console turns a newline into CR LF, and everything writes plain
    // newlines: this file, the interpreter's own output, IDF's logging.
    // Only the terminal's return key needs saying, because it sends CR
    // where the reader below expects one.
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
    // RECV needs to know when the wire has gone quiet, and the boot
    // window needs to know that nobody pressed anything.
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);
}

static size_t free_internal() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static size_t free_psram() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static void report(const char* label) {
    printf("%-12s internal %7u  largest %7u  psram %8u\n", label,
           (unsigned)free_internal(),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)free_psram());
    fflush(stdout);
}

// One byte, or -1 once the wire has been quiet for the given time.
static int read_byte_ms(int timeout_ms) {
    for (int waited = 0; waited <= timeout_ms; waited += 10) {
        int ch = getchar();
        if (ch != EOF) return ch;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return -1;
}

// The keys an editor needs, with a terminal's escape sequences folded
// into the same codes the PicoCalc's keyboard controller sends. Blocking:
// stdin is non-blocking here, so an empty read waits rather than gives up.
#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5

static int getch_blocking(void) {
    for (;;) {
        int c = getchar();
        if (c != EOF) return c;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

extern "C" int repl_read_key(void) {
    int c = getch_blocking();
    if (c != 0x1B) return c;
    int c2 = getch_blocking();
    if (c2 != '[') return 0;
    switch (getch_blocking()) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case '3': getch_blocking(); return K_DEL;
    }
    return 0;
}

extern "C" void jdb_con_size(int* cols, int* rows) {
    es3c28p_con_size(cols, rows);
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
    printf("\n");
}

// Strip surrounding quotes and trailing blanks from a command argument.
static const char* dos_arg(char* a) {
    while (*a == ' ') a++;
    size_t n = strlen(a);
    while (n && (a[n - 1] == ' ' || a[n - 1] == '\r')) a[--n] = 0;
    if (n >= 2 && (a[0] == '"' || a[0] == '\'')) { a++; n -= 2; a[n] = 0; }
    return a;
}

static bool autorun_get(char* name, size_t cap) {
    FILE* f = fopen(AUTORUN_CFG, "r");
    if (!f) return false;
    bool ok = fgets(name, (int)cap, f) != NULL;
    fclose(f);
    if (!ok) return false;
    size_t n = strlen(name);
    while (n && (name[n - 1] == '\n' || name[n - 1] == '\r')) name[--n] = 0;
    return n > 0;
}

static int copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return -1;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[512];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    fclose(in);
    return fclose(out) == 0 ? rc : -1;
}

static int dos_command(char* line) {
    char cmd[10];
    int ci = 0;
    const char* p = line;
    while (*p && *p != ' ' && ci < 9) cmd[ci++] = toupper((unsigned char)*p++);
    cmd[ci] = 0;
    if (*p && *p != ' ') return 0;
    char rest[1024];
    snprintf(rest, sizeof rest, "%s", p);
    char* a = rest;
    while (*a == ' ') a++;

    if (strcmp(cmd, "AUTORUN") == 0) {
        char name[128];
        const char* arg = dos_arg(a);
        if (!*arg) {
            if (autorun_get(name, sizeof name)) printf("autorun: %s\n", name);
            else printf("autorun: off\n");
        } else if (strcasecmp(arg, "OFF") == 0) {
            remove(AUTORUN_CFG);
            printf("autorun off\n");
        } else {
            char resolved[160];
            const char* nm = resolve_name(arg, resolved, sizeof resolved);
            FILE* probe = fopen(nm, "r");
            if (!probe) { printf("cannot open %s\n", nm); return 1; }
            fclose(probe);
            FILE* f = fopen(AUTORUN_CFG, "w");
            if (!f) { printf("cannot save autorun\n"); return 1; }
            fprintf(f, "%s\n", nm);
            fclose(f);
            printf("autorun: %s\n", nm);
        }
        return 1;
    }

    // Take a file straight off the wire. Nothing is echoed and nothing
    // is parsed, so a program arrives at the speed of the link. Ends on
    // Ctrl-D, or once the line has been quiet for three seconds.
    if (strcmp(cmd, "RECV") == 0) {
        const char* nm = dos_arg(a);
        if (!*nm) { printf("RECV name\n"); return 1; }
        FILE* f = fopen(nm, "w");
        if (!f) { printf("cannot write %s\n", nm); return 1; }
        printf("receiving %s, end with Ctrl-D\n", nm);
        fflush(stdout);
        size_t n = 0;
        int pending_cr = 0;
        for (;;) {
            int c = read_byte_ms(n ? 3000 : 30000);
            if (c < 0 || c == 4) break;
            // Any line ending becomes a single newline.
            if (c == '\r') { pending_cr = 1; fputc('\n', f); n++; continue; }
            if (c == '\n' && pending_cr) { pending_cr = 0; continue; }
            pending_cr = 0;
            fputc(c, f);
            n++;
        }
        fclose(f);
        printf("%u bytes\n", (unsigned)n);
        return 1;
    }

    // The listing the desktop gives: a line number, a rule, and the line
    // coloured the way the editor colours it.
    if (strcmp(cmd, "EDIT") == 0) {
        char resolved[160];
        const char* nm = dos_arg(a);
        if (!*nm) nm = g_current;
        if (!*nm) { printf("EDIT what? Give it a name.\n"); return 1; }
        nm = resolve_name(nm, resolved, sizeof resolved);
        if (!es3c28p_con_on())
            printf("no panel console - GFX.CONSOLE 1 first, or edit blind\n");
        pico_editor(nm);
        return 1;
    }

    if (strcmp(cmd, "LIST") == 0) {
        char resolved[160];
        const char* nm = dos_arg(a);
        if (!*nm) nm = g_current;
        if (!*nm) { printf("No program loaded.\n"); return 1; }
        nm = resolve_name(nm, resolved, sizeof resolved);
        FILE* f = fopen(nm, "r");
        if (!f) { printf("cannot open %s\n", nm); return 1; }
        char buf[256];
        int ln = 1;
        while (fgets(buf, sizeof buf, f)) {
            int n = (int)strlen(buf);
            while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
            printf("\x1b[90m%4d | \x1b[0m", ln++);
            syntax_print(buf, n);
            printf("\n");
        }
        fclose(f);
        return 1;
    }

    if (strcmp(cmd, "TYPE") == 0 && *a) {
        char resolved[160];
        FILE* f = fopen(resolve_name(dos_arg(a), resolved, sizeof resolved), "r");
        if (!f) { printf("cannot open %s\n", a); return 1; }
        char buf[256];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            printf("%.*s", (int)n, buf);
        fclose(f);
        printf("\n");
        return 1;
    }

    if (strcmp(cmd, "DEL") == 0 && *a) {
        char resolved[160];
        const char* nm = resolve_name(dos_arg(a), resolved, sizeof resolved);
        printf(remove(nm) == 0 ? "deleted\n" : "cannot delete\n");
        return 1;
    }

    if ((strcmp(cmd, "COPY") == 0 || strcmp(cmd, "REN") == 0) && *a) {
        char* sp = strchr(a, ' ');
        if (!sp) { printf("usage: %s from to\n", cmd); return 1; }
        *sp = 0;
        const char* src = dos_arg(a);
        char* b = sp + 1;
        const char* dst = dos_arg(b);
        int rc = (cmd[0] == 'C') ? copy_file(src, dst) : rename(src, dst);
        if (rc != 0) printf("cannot %s %s\n", cmd[0] == 'C' ? "copy" : "rename", src);
        return 1;
    }

    return 0;
}

static void run_file(JdbEmbed* vm, const char* name) {
    char resolved[160];
    name = resolve_name(name, resolved, sizeof resolved);
    snprintf(g_current, sizeof g_current, "%s", name);
    char* out = jdb_embed_load(vm, name);
    if (out) {
        printf("%s", out);
        jdb_embed_free(out);
    } else {
        const char* err = jdb_embed_last_error(vm);
        printf("ERROR: %s\n", err ? err : "unknown");
    }
    fflush(stdout);
}

extern "C" void app_main(void) {
    console_init();

    // A host may be listening on USB; standalone, nothing is, and the
    // prompt should not wait for one.
    vTaskDelay(pdMS_TO_TICKS(600));

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    printf("\njdBasic on ESP32-S%d rev v%d.%d, %d core%s\n",
           chip.model == CHIP_ESP32S3 ? 3 : 2,
           chip.revision / 100, chip.revision % 100,
           chip.cores, chip.cores == 1 ? "" : "s");

    bool fs_ok = esp32_fs_init();
    if (!fs_ok) printf("no flash store\n");

    size_t before_int = free_internal();
    size_t before_psram = free_psram();
    report("boot");

    JdbEmbed* vm = jdb_embed_init();
    if (!vm) {
        printf("VM init failed\n");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    jdb_embed_output_stdout(vm);
    esp32_note_after_init();

    report("after init");
    printf("VM costs    internal %7d  psram %8d\n",
           (int)before_int - (int)free_internal(),
           (int)before_psram - (int)free_psram());

    // Power-on program, with a window to get out of it: without one a
    // looping autorun program would own the board for good.
    char ar_name[128];
    if (fs_ok && autorun_get(ar_name, sizeof ar_name)) {
        printf("autorun %s - ESC to stop\n", ar_name);
        fflush(stdout);
        bool cancelled = false;
        for (int i = 0; i < 20 && !cancelled; i++) {
            int c = read_byte_ms(100);
            if (c == 0x1B || c == 3) cancelled = true;
        }
        if (cancelled) printf("cancelled\n");
        else run_file(vm, ar_name);
    }

    static char line[1024];
    for (;;) {
        printf("> ");
        fflush(stdout);
        read_line(line, sizeof line);
        if (!line[0]) continue;

        if (dos_command(line)) { fflush(stdout); continue; }

        // RUN and LOAD name a file rather than an expression, and an
        // empty argument means the one already named.
        int meta = 0;
        char* arg = NULL;
        if (strncasecmp(line, "RUN", 3) == 0 && (line[3] == 0 || line[3] == ' ' || line[3] == '"')) {
            meta = 1; arg = line + 3;
        } else if (strncasecmp(line, "LOAD", 4) == 0 && (line[4] == 0 || line[4] == ' ' || line[4] == '"')) {
            meta = 2; arg = line + 4;
        }
        if (meta) {
            const char* nm = dos_arg(arg);
            if (!*nm) nm = g_current;
            if (!*nm) {
                printf("no program named - RUN name\n");
            } else if (meta == 1) {
                run_file(vm, nm);
            } else {
                char resolved[160];
                nm = resolve_name(nm, resolved, sizeof resolved);
                FILE* probe = fopen(nm, "r");
                if (!probe) printf("cannot open %s\n", nm);
                else {
                    fclose(probe);
                    snprintf(g_current, sizeof g_current, "%s", nm);
                    printf("loaded %s\n", nm);
                }
            }
            fflush(stdout);
            continue;
        }

        char* out = jdb_embed_eval(vm, line);
        if (out) {
            if (out[0]) printf("%s", out);
            jdb_embed_free(out);
        } else {
            const char* err = jdb_embed_last_error(vm);
            printf("ERROR: %s\n", err ? err : "unknown");
        }
        fflush(stdout);
    }
}
