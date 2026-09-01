// jdBasic on the Pico family: what this board brings to the shared
// prompt. Console I/O runs through the SDK's stdio, which carries both
// USB-CDC and - on a PicoCalc or a Fruit Jam - the screen and keyboard,
// registered here as one more stdio driver. The prompt itself, the file
// verbs and the editor live in ../common.

#include <stdio.h>
#include <string.h>
#include <string>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#ifdef JDB_HAS_CYW43
#include "pico/cyw43_arch.h"
#endif
#include "jdb_embed_api.h"
#include "../common/jdb_repl.h"

extern "C" void jdb_pico_fs_init(void);
#ifdef FRUITJAM
extern "C" void fruitjam_dvi_init(void);
extern "C" int  fruitjam_dvi_alloc(void);
#ifdef FRUITJAM_USB
extern "C" void fruitjam_usb_init(void);
#endif
extern "C" void fruitjam_con_init(void);
extern "C" void fruitjam_snd_init(void);
extern "C" void fruitjam_board_init(void);
extern "C" int  fruitjam_dvi_width(void);
extern "C" int  fruitjam_dvi_height(void);
extern "C" unsigned fruitjam_dvi_frame_us(void);
#ifdef FRUITJAM_USB
extern "C" int  fruitjam_usb_keyboards(void);
extern "C" int  fruitjam_usb_devices(void);
extern "C" void fruitjam_usb_poll(void);
#endif
#endif
#ifdef PICOCALC
extern "C" void picocalc_lcd_init(void);
extern "C" void picocalc_lcd_putc(char c);
extern "C" void picocalc_lcd_flush(void);
extern "C" void picocalc_kbd_init(void);
extern "C" int  picocalc_kbd_poll(void);

static void pc_out_chars(const char* buf, int len) {
    for (int i = 0; i < len; i++) picocalc_lcd_putc(buf[i]);
    picocalc_lcd_flush();
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
#endif // PICOCALC

// Key codes as the PicoCalc's keyboard controller sends them; a USB
// terminal's ANSI sequences fold into the same values.
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

extern "C" void jdb_snd_note_due(void);

// The editor asks how big the page is and adapts. A board with no screen
// of its own is being read in a terminal, and a terminal is 80 by 24
// until it says otherwise.
extern "C" void jdb_con_size(int* cols, int* rows) {
#if defined(FRUITJAM)
    *cols = 40; *rows = 30;      // 320 by 240 in the 8 by 8 font
#elif defined(PICOCALC)
    *cols = 40; *rows = 40;      // the PicoCalc's panel is square
#else
    *cols = 80; *rows = 24;
#endif
}

// The melody engine's timer and lock, in SDK terms.
static alarm_id_t g_note_alarm = 0;

static int64_t note_alarm_cb(alarm_id_t, void*) {
    g_note_alarm = 0;
    jdb_snd_note_due();
    return 0;
}

extern "C" void jdb_snd_timer_start(int ms) {
    if (g_note_alarm) cancel_alarm(g_note_alarm);
    g_note_alarm = add_alarm_in_ms(ms > 0 ? ms : 1, note_alarm_cb, nullptr, true);
}

extern "C" void jdb_snd_timer_cancel(void) {
    if (g_note_alarm) { cancel_alarm(g_note_alarm); g_note_alarm = 0; }
}

extern "C" uint32_t jdb_snd_lock(void) { return save_and_disable_interrupts(); }
extern "C" void jdb_snd_unlock(uint32_t saved) { restore_interrupts(saved); }

// A terminal sends its arrows as escape sequences, and a shifted arrow
// carries a modifier parameter: ESC[1;2A. Collecting the parameters
// rather than reading a fixed three bytes is what lets selection work
// over the serial line as well as on the board's own keyboard.
extern "C" int repl_read_key(void) {
    // A terminal ends a line with both characters, and whoever read the
    // carriage return leaves the line feed behind - often for whatever
    // runs next. Taking both as a return doubles every line break in
    // pasted text, so the pair is coalesced here, once, for everyone.
    static bool after_cr = false;
    int c;
    for (;;) {
        c = getchar();
        if (c == 10 && after_cr) { after_cr = false; continue; }
        after_cr = (c == 13);
        break;
    }
    if (c != 0x1B) return c;
    if (getchar() != '[') return 0;

    char par[8];
    int n = 0, f;
    for (;;) {
        f = getchar();
        if (f < 0) return 0;
        if ((f >= '0' && f <= '9') || f == ';') {
            if (n < (int)sizeof par - 1) par[n++] = (char)f;
            continue;
        }
        break;
    }
    par[n] = 0;

    // The modifier is the parameter after the semicolon; 2 means shift.
    int shift = 0;
    const char* semi = strchr(par, ';');
    if (semi && semi[1] == '2') shift = 1;

    switch (f) {
        case 'A': return shift ? K_SUP : K_UP;
        case 'B': return shift ? K_SDOWN : K_DOWN;
        case 'C': return shift ? K_SRIGHT : K_RIGHT;
        case 'D': return shift ? K_SLEFT : K_LEFT;
        case 'H': return shift ? K_SHOME : K_HOME;
        case 'F': return shift ? K_SEND : K_END;
        case '~':
            if (par[0] == '3') return K_DEL;
            if (par[0] == '1' || par[0] == '7') return shift ? K_SHOME : K_HOME;
            if (par[0] == '4' || par[0] == '8') return shift ? K_SEND : K_END;
            break;
    }
    return 0;
}

static int pico_read_byte_ms(int timeout_ms) {
    int c = getchar_timeout_us((uint32_t)timeout_ms * 1000u);
    return c == PICO_ERROR_TIMEOUT ? -1 : c;
}

// Fetching a program off the web beats a serial transfer protocol when
// there is a radio on board: the name defaults to the last part of the
// URL.
static int pico_board_command(const char* cmd, char* a) {
#ifdef JDB_HAS_CYW43
    if (strcmp(cmd, "INSTALL") == 0) {
        char url[256];
        snprintf(url, sizeof url, "%s", jdb_repl_arg(a));
        if (!url[0]) { printf("INSTALL url [name]\n"); return 1; }
        char* sp = strchr(url, ' ');
        const char* name = nullptr;
        if (sp) { *sp = 0; name = sp + 1; while (*name == ' ') name++; }
        if (!name || !*name) {
            const char* slash = strrchr(url, '/');
            name = (slash && slash[1]) ? slash + 1 : "download.jdb";
        }
        std::string body;
        extern bool pico_http_fetch(const char* url, std::string& out);
        if (!pico_http_fetch(url, body)) { printf("fetch failed\n"); return 1; }
        FILE* f = fopen(name, "w");
        if (!f) { printf("cannot write %s\n", name); return 1; }
        size_t n = fwrite(body.data(), 1, body.size(), f);
        if (fclose(f) != 0 || n != body.size()) { printf("write failed\n"); return 1; }
        printf("%s, %u bytes\n", name, (unsigned)n);
        return 1;
    }
#else
    (void)cmd; (void)a;
#endif
    return 0;
}

extern "C" unsigned jdb_pico_heap_free(void);
extern "C" int jdb_pico_fs_free(unsigned* freebytes, unsigned* total);

#if defined(FRUITJAM)
#define BOARD_TEXT "a Fruit Jam"
#define CHIP_TEXT  "RP2350B"
#elif defined(PICOCALC)
#define BOARD_TEXT "a PicoCalc"
#define CHIP_TEXT  "RP2350"
#elif PICO_RP2040
#define BOARD_TEXT "a Pico"
#define CHIP_TEXT  "RP2040"
#else
#define BOARD_TEXT "a Pico 2"
#define CHIP_TEXT  "RP2350"
#endif

// The first page: what this machine is and what it has, written for forty
// columns. Kilobytes rather than bytes, because the digit that matters on
// a screen this size is the first one.
static void pico_hello(void) {
    printf("\x1b[93m jdBasic\x1b[0m   on " BOARD_TEXT "\n");
    // Two short of the width: a rule that fills the row exactly makes the
    // console wrap, and the newline after it then costs a blank line.
    printf("\x1b[90m--------------------------------------\x1b[0m\n");
    printf(" chip   " CHIP_TEXT ", 2 cores at %u MHz\n",
           (unsigned)(clock_get_hz(clk_sys) / 1000000u));
    printf(" ram    %u KB free\n", jdb_pico_heap_free() / 1024u);
#ifdef FRUITJAM
    unsigned us = fruitjam_dvi_frame_us();
    printf(" video  %dx%d over DVI at %u Hz\n",
           fruitjam_dvi_width(), fruitjam_dvi_height(),
           us ? (1000000u + us / 2) / us : 0);
    printf(" sound  TLV320 codec, jack and speaker\n");
#ifdef FRUITJAM_USB
    int kb = fruitjam_usb_keyboards(), dev = fruitjam_usb_devices();
    if (dev == 0) printf(" usb    host running, nothing attached\n");
    else printf(" usb    %d keyboard%s, %d device%s\n",
                kb, kb == 1 ? "" : "s", dev, dev == 1 ? "" : "s");
#endif
#endif
#ifdef PICOCALC
    printf(" panel  320x320, keyboard and sound\n");
#endif
    unsigned avail = 0, total = 0;
    if (jdb_pico_fs_free(&avail, &total) == 0)
        printf(" store  %u KB free of %u KB\n", avail / 1024u, total / 1024u);
    printf("\n");
    jdb_repl_hints();
}

static const JdbReplPort PORT = {
    pico_read_byte_ms,
    pico_board_command,
    nullptr,
    pico_hello,
    "/.autorun",
};

int main() {
#ifdef FRUITJAM
    // 480p60 wants a 252 MHz bit clock, and the serialiser puts out two
    // bits per cycle, so HSTX has to see exactly 126 MHz. clk_hstx follows
    // clk_sys, so the system runs there too and the pixel rate lands on
    // 25.2 MHz. PIO-USB divides the same clock down to 12 MHz and takes a
    // fractional divider to do it, which is what the reference ports use.
    set_sys_clock_khz(126000, true);
#endif
    stdio_init_all();
#ifdef FRUITJAM
    fruitjam_dvi_init();
    fruitjam_dvi_alloc();
    fruitjam_snd_init();
    fruitjam_board_init();
#ifdef FRUITJAM_USB
    fruitjam_usb_init();
#endif
#endif
#ifdef JDB_HAS_CYW43
    cyw43_arch_init();
#endif
    jdb_pico_fs_init();
#ifdef PICOCALC
    picocalc_lcd_init();
    picocalc_kbd_init();
    stdio_set_driver_enabled(&pc_driver, true);
#endif

    // A host may be listening on USB; standalone, nothing is, and the
    // prompt should not wait for one.
    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(200);

#ifdef FRUITJAM
#ifdef FRUITJAM_USB
    // Enumeration only advances while something drives the host stack,
    // and the only thing that does is a read for a key. Nothing reads one
    // before the prompt exists, so the welcome page would count no
    // keyboard however long it waited. Two seconds is more than one
    // needs, and is only spent when nothing is attached.
    for (int i = 0; i < 100 && fruitjam_usb_devices() == 0; i++) {
        fruitjam_usb_poll();
        sleep_ms(20);
    }
#endif
    // The screen stays black until here, so the first thing on it is the
    // welcome page rather than whatever the drivers had to say.
    fruitjam_con_init();
#endif

    JdbEmbed* vm = jdb_embed_init();
    if (!vm) {
        printf("VM init failed\n");
        for (;;) sleep_ms(1000);
    }
    // Output goes live to the console as programs print - INPUT shows
    // its prompt before it blocks, CLS clears when it runs.
    jdb_embed_output_stdout(vm);

    jdb_repl_run(vm, &PORT);
}
