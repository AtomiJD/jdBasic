// jdBasic on the ESP32-S3: what this board brings to the shared prompt.
// The console is the native USB port, the flash store sits behind it and
// the panel console comes up with the board. The prompt itself, the file
// verbs and the editor live in ../../common.

#include <stdio.h>
#include <string.h>
#include <strings.h>
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
#include "../../../src/version.h"
#include "esp_private/esp_clk.h"
#include "../../common/jdb_repl.h"

extern "C" bool esp32_fs_init(void);
void esp32_note_after_init(void);

extern "C" int  es3c28p_con_on(void);
extern "C" int  es3c28p_lcd_init(void);
extern "C" int  es3c28p_con_enable(int on);
extern "C" int  es3c28p_lcd_width(void);
extern "C" int  es3c28p_lcd_height(void);
extern "C" void es3c28p_con_size(int* cols, int* rows);

#define AUTORUN_CFG "autorun.cfg"

// Whether the panel console starts with the board. On unless a file says
// otherwise, because a display board that boots dark is a display board
// you have to be told about.
#define CONSOLE_CFG "console.cfg"

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
static int esp32_read_byte_ms(int timeout_ms) {
    for (int waited = 0; waited <= timeout_ms; waited += 10) {
        int ch = getchar();
        if (ch != EOF) return ch;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return -1;
}

// The keys an editor needs, with a terminal's escape sequences folded
// into the same codes the PicoCalc's keyboard controller sends.
#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5
#define K_SLEFT  0xB8
#define K_SUP    0xB9
#define K_SDOWN  0xBA
#define K_SRIGHT 0xBB
#define K_SHOME  0xD8
#define K_SEND   0xD9
#define K_PGUP   0xD6
#define K_PGDN   0xD7
#define K_CLEFT  0xBC
#define K_CRIGHT 0xBD
#define K_CHOME  0xDA
#define K_CEND   0xDB
#define K_F1     0xC1
#define K_STAB   0xC2

// Blocking: stdin is non-blocking here, so an empty read waits rather
// than gives up.
static int getch_blocking(void) {
    for (;;) {
        int c = getchar();
        if (c != EOF) return c;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// A shifted arrow carries a modifier parameter: ESC[1;2A. Collecting the
// parameters rather than reading a fixed three bytes is what lets the
// editor's selection work over the serial line.
extern "C" int repl_read_key(void) {
    int c = getch_blocking();
    if (c != 0x1B) return c;
    int intro = getch_blocking();
    if (intro == 'O') return getch_blocking() == 'P' ? K_F1 : 0;
    if (intro != '[') return 0;

    char par[8];
    int n = 0, f;
    for (;;) {
        f = getch_blocking();
        if ((f >= '0' && f <= '9') || f == ';') {
            if (n < (int)sizeof par - 1) par[n++] = (char)f;
            continue;
        }
        break;
    }
    par[n] = 0;

    // The modifier is the parameter after the semicolon: 2 shift, 5 ctrl.
    int shift = 0, ctrl = 0;
    const char* semi = strchr(par, ';');
    if (semi && semi[1] == '2') shift = 1;
    if (semi && semi[1] == '5') ctrl = 1;

    switch (f) {
        case 'A': return shift ? K_SUP : K_UP;
        case 'B': return shift ? K_SDOWN : K_DOWN;
        case 'C': return ctrl ? K_CRIGHT : shift ? K_SRIGHT : K_RIGHT;
        case 'D': return ctrl ? K_CLEFT : shift ? K_SLEFT : K_LEFT;
        case 'H': return ctrl ? K_CHOME : shift ? K_SHOME : K_HOME;
        case 'F': return ctrl ? K_CEND : shift ? K_SEND : K_END;
        case 'Z': return K_STAB;
        case '~':
            if (par[0] == '1' && par[1] == '1') return K_F1;
            if (par[0] == '3') return K_DEL;
            if (par[0] == '5') return K_PGUP;
            if (par[0] == '6') return K_PGDN;
            if (par[0] == '1' || par[0] == '7') return ctrl ? K_CHOME : shift ? K_SHOME : K_HOME;
            if (par[0] == '4' || par[0] == '8') return ctrl ? K_CEND : shift ? K_SEND : K_END;
            break;
    }
    return 0;
}

extern "C" void jdb_con_size(int* cols, int* rows) {
    es3c28p_con_size(cols, rows);
}

static bool console_boot_wanted(void) {
    FILE* f = fopen(CONSOLE_CFG, "r");
    if (!f) return true;
    char buf[8] = {0};
    bool got = fgets(buf, sizeof buf, f) != NULL;
    fclose(f);
    return !(got && (buf[0] == 'o' || buf[0] == 'O') &&
                    (buf[1] == 'f' || buf[1] == 'F'));
}

static int esp32_board_command(const char* cmd, char* a) {
    if (strcmp(cmd, "CONSOLE") != 0) return 0;
    const char* arg = jdb_repl_arg(a);
    if (!*arg) {
        printf("console at boot: %s\n", console_boot_wanted() ? "on" : "off");
    } else if (strcasecmp(arg, "OFF") == 0) {
        FILE* f = fopen(CONSOLE_CFG, "w");
        if (!f) { printf("cannot write %s\n", CONSOLE_CFG); return 1; }
        fputs("off\n", f);
        fclose(f);
        es3c28p_con_enable(0);
        printf("console off at boot\n");
    } else if (strcasecmp(arg, "ON") == 0) {
        remove(CONSOLE_CFG);
        if (es3c28p_lcd_init() == 0) es3c28p_con_enable(1);
        printf("console on at boot\n");
    } else {
        printf("CONSOLE ON, CONSOLE OFF, or CONSOLE to ask\n");
    }
    return 1;
}

static void esp32_before_edit(void) {
    if (!es3c28p_con_on())
        printf("no panel console - GFX.CONSOLE 1 first, or edit blind\n");
}

static const JdbReplPort PORT = {
    esp32_read_byte_ms,
    esp32_board_command,
    esp32_before_edit,
    // The page goes up with the panel console rather than with the
    // prompt: by then the serial line has had sixty columns of diagnosis.
    nullptr,
    AUTORUN_CFG,
};

// The first page on the panel: forty columns, so it is written for
// forty. Kilobytes rather than bytes, because the digit that matters on
// a screen this size is the first one.
bool esp32_fs_space(uint64_t* total, uint64_t* freebytes);

// The page every board shows at power-on, in the same shape: what it
// is, what it has, and the four verbs to start with.
static void panel_hello(const esp_chip_info_t* chip) {
    printf("\x1b[93m jdBasic " JDBASIC_VERSION "\x1b[0m   on an ES3C28P\n");
    // Two short of the width: a rule that fills the row exactly makes the
    // console wrap, and the newline after it then costs a blank line.
    printf("\x1b[90m--------------------------------------\x1b[0m\n");
    printf(" built  " __DATE__ "\n");
    printf(" chip   ESP32-S%d rev v%d.%d, %d core%s at %u MHz\n",
           chip->model == CHIP_ESP32S3 ? 3 : 2,
           chip->revision / 100, chip->revision % 100,
           chip->cores, chip->cores == 1 ? "" : "s",
           (unsigned)(esp_clk_cpu_freq() / 1000000));
    printf(" ram    %u KB free\n", (unsigned)(free_internal() / 1024));
    printf(" psram  %u KB free\n", (unsigned)(free_psram() / 1024));
    printf(" panel  %dx%d, touch and sound\n",
           es3c28p_lcd_width(), es3c28p_lcd_height());
    uint64_t total = 0, avail = 0;
    if (esp32_fs_space(&total, &avail))
        printf(" store  %u KB free of %u KB\n",
               (unsigned)(avail / 1024), (unsigned)(total / 1024));
    printf("\n");
    jdb_repl_hints();
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

    // Everything above is sixty columns of diagnosis and belongs on the
    // wire. The panel is forty, and gets a page written for forty: the
    // same facts, shaped to be read rather than wrapped.
    //
    // The console starts here rather than earlier for that reason - it
    // clears on start, so anything printed before it would be lost, and
    // anything printed to both would have to fit the narrower of the two.
    if (fs_ok && console_boot_wanted() && es3c28p_lcd_init() == 0) {
        es3c28p_con_enable(1);
        panel_hello(&chip);
    }

    jdb_repl_run(vm, &PORT);
}
