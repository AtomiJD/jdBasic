// The REPL: a prompt feeding the embedded VM, one persistent
// interpreter for the whole session. Console I/O runs through the
// SDK's stdio, which carries both USB-CDC and - on a PicoCalc - the
// screen and keyboard, registered here as one more stdio driver.
// The input line is a small editor: cursor keys, insert, delete,
// home, end, and a history ring on up and down.

#include <stdio.h>
#include <string.h>
#include <string>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
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

extern "C" void jdb_pico_fs_init(void);
#ifdef FRUITJAM
extern "C" void fruitjam_dvi_init(void);
extern "C" int  fruitjam_dvi_alloc(void);
#ifdef FRUITJAM_USB
extern "C" void fruitjam_usb_init(void);
#endif
extern "C" void fruitjam_dvi_trace(const char* s);
extern "C" void fruitjam_con_init(void);
extern "C" void fruitjam_snd_init(void);
extern "C" void fruitjam_board_init(void);
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

extern "C" int repl_read_key(void);
extern "C" void jdb_snd_note_due(void);
void pico_help(const char* topic);
void syntax_print(const char* s, int n);
#if defined(PICOCALC) || defined(FRUITJAM)
void pico_editor(const char* name);
#endif

// The program the prompt is working on, by name. LIST, SAVE, EDIT and RUN
// all mean "this one" when given no argument.
static char g_current[128];

// A name without a dot in it means a program, the same way it does at the
// desktop prompt: LOAD spiel finds spiel.jdb.
static const char* prog_name(const char* in, char* out, size_t cap) {
    if (strchr(in, '.')) return in;
    snprintf(out, cap, "%s.jdb", in);
    return out;
}

// The DOS set at the prompt, unquoted arguments welcome: CD, TYPE,
// DEL, COPY, REN, MD, RD. Returns 0 when the line is not one of them
// and belongs to the interpreter instead. TYPE steps aside for a
// one-line TYPE..ENDTYPE declaration.

static const char* dos_arg(char* s) {
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n && s[n-1] == ' ') s[--n] = 0;
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n-1] == s[0]) {
        s[n-1] = 0;
        s++;
    }
    return s;
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
    if (fclose(out) != 0) rc = -1;
    return rc;
}


// Autorun: a program name in the flash store, started at power-on so
// the board comes up as whatever its owner built. The name lives in a
// plain file, so DEL turns it off like anything else.

#define AUTORUN_CFG "/.autorun"

static int autorun_get(char* out, size_t cap) {
    FILE* f = fopen(AUTORUN_CFG, "r");
    if (!f) return 0;
    if (!fgets(out, (int)cap, f)) { fclose(f); return 0; }
    fclose(f);
    size_t n = strlen(out);
    while (n && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' ')) out[--n] = 0;
    return out[0] ? 1 : 0;
}

static int autorun_set(const char* name) {
    FILE* f = fopen(AUTORUN_CFG, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", name);
    return fclose(f) == 0 ? 0 : -1;
}

static int dos_command(char* line) {
    char cmd[8];
    int ci = 0;
    const char* p = line;
    while (*p && *p != ' ' && ci < 7) cmd[ci++] = toupper((unsigned char)*p++);
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
            if (autorun_get(name, sizeof name)) printf("autorun: %s\r\n", name);
            else printf("autorun: off\r\n");
        } else if (strcasecmp(arg, "OFF") == 0) {
            remove(AUTORUN_CFG);
            printf("autorun off\r\n");
        } else {
            FILE* probe = fopen(arg, "r");
            if (!probe) { printf("cannot open %s\r\n", arg); return 1; }
            fclose(probe);
            if (autorun_set(arg) != 0) printf("cannot save autorun\r\n");
            else printf("autorun: %s\r\n", arg);
        }
        return 1;
    }

    // Take a file straight off the wire. The line editor echoes every
    // keystroke as a full recoloured line, which costs kilobytes per
    // line of source; here nothing is echoed and nothing is parsed, so
    // a program arrives at the speed of the link. Ends on Ctrl-D, or on
    // a quiet line once something has arrived.
    if (strcmp(cmd, "RECV") == 0) {
        const char* nm = dos_arg(a);
        if (!*nm) { printf("RECV name\r\n"); return 1; }
        FILE* f = fopen(nm, "w");
        if (!f) { printf("cannot write %s\r\n", nm); return 1; }
        printf("receiving %s, end with Ctrl-D\r\n", nm);
        fflush(NULL);
        size_t n = 0;
        int idle = 0;
        int pending_cr = 0;
        for (;;) {
            int c = getchar_timeout_us(100000);
            if (c == PICO_ERROR_TIMEOUT) {
                idle++;
                if (n && idle >= 30) break;
                if (!n && idle >= 300) break;
                continue;
            }
            idle = 0;
            if (c == 0x04) break;
            if (c == 0x1B) {
                fclose(f);
                remove(nm);
                printf("cancelled\r\n");
                return 1;
            }
            // Whatever the sender's line endings, the file gets \n.
            if (pending_cr && c == '\n') { pending_cr = 0; continue; }
            pending_cr = 0;
            if (c == '\r') { pending_cr = 1; c = '\n'; }
            fputc(c, f);
            n++;
        }
        fclose(f);
        printf("%u bytes\r\n", (unsigned)n);
        return 1;
    }

    // Fetch a program off the web straight into the flash store. With a
    // radio on board this beats a serial transfer protocol: the name
    // defaults to the last part of the URL.
#ifdef JDB_HAS_CYW43
    if (strcmp(cmd, "INSTALL") == 0) {
        char url[256];
        snprintf(url, sizeof url, "%s", dos_arg(a));
        if (!url[0]) { printf("INSTALL url [name]\r\n"); return 1; }
        char* sp = strchr(url, ' ');
        const char* name = nullptr;
        if (sp) { *sp = 0; name = sp + 1; while (*name == ' ') name++; }
        if (!name || !*name) {
            const char* slash = strrchr(url, '/');
            name = (slash && slash[1]) ? slash + 1 : "download.jdb";
        }
        std::string body;
        extern bool pico_http_fetch(const char* url, std::string& out);
        if (!pico_http_fetch(url, body)) { printf("fetch failed\r\n"); return 1; }
        FILE* f = fopen(name, "w");
        if (!f) { printf("cannot write %s\r\n", name); return 1; }
        size_t n = fwrite(body.data(), 1, body.size(), f);
        if (fclose(f) != 0 || n != body.size()) { printf("write failed\r\n"); return 1; }
        printf("%s, %u bytes\r\n", name, (unsigned)n);
        return 1;
    }
#endif

    if (strcmp(cmd, "CD") == 0) {
        if (*a && chdir(dos_arg(a)) != 0) { printf("no such directory\r\n"); return 1; }
        char cwd[160];
        printf("%s\r\n", getcwd(cwd, sizeof cwd) ? cwd : "?");
        return 1;
    }
    if (strcmp(cmd, "TYPE") == 0 && *a && !strchr(a, ':')) {
        FILE* f = fopen(dos_arg(a), "r");
        if (!f) { printf("cannot open %s\r\n", a); return 1; }
        char buf[256];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            printf("%.*s", (int)n, buf);
        fclose(f);
        printf("\r\n");
        return 1;
    }
    if (strcmp(cmd, "DEL") == 0 && *a) {
        printf(remove(dos_arg(a)) == 0 ? "deleted\r\n" : "cannot delete %s\r\n", a);
        return 1;
    }
    if ((strcmp(cmd, "MD") == 0 || strcmp(cmd, "RD") == 0) && *a) {
        int rc = cmd[0] == 'M' ? mkdir(dos_arg(a), 0777) : rmdir(dos_arg(a));
        if (rc != 0) printf("cannot %s %s\r\n", cmd[0] == 'M' ? "create" : "remove", a);
        return 1;
    }
    if (strcmp(cmd, "DIR") == 0) {
        const char* path = *a ? dos_arg(a) : ".";
        DIR* d = opendir(path);
        if (!d) { printf("cannot open %s\r\n", path); return 1; }
        struct dirent* e;
        int n = 0;
        unsigned long bytes = 0;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            if (e->d_type == DT_DIR) {
                printf("%-24s   <DIR>\r\n", e->d_name);
            } else {
                char full[192];
                snprintf(full, sizeof full, "%s/%s", path, e->d_name);
                struct stat st;
                long sz = (stat(full, &st) == 0) ? (long)st.st_size : 0;
                printf("%-24s %7ld\r\n", e->d_name, sz);
                bytes += (unsigned long)sz;
            }
            n++;
        }
        closedir(d);
        printf("%d entries, %lu bytes\r\n", n, bytes);
        return 1;
    }

    // The program is the file; these three work on it by name. LIST is
    // TYPE with line numbers and the editor's colours, SAVE is a copy
    // that also moves the name along, and NEW starts an empty one.
    if (strcmp(cmd, "LIST") == 0) {
        char nb[160];
        const char* nm = *a ? prog_name(dos_arg(a), nb, sizeof nb) : g_current;
        if (!*nm) { printf("no program - LOAD name first\r\n"); return 1; }
        FILE* f = fopen(nm, "r");
        if (!f) { printf("cannot open %s\r\n", nm); return 1; }
        char ln[256];
        int no = 0;
        while (fgets(ln, sizeof ln, f)) {
            size_t l = strlen(ln);
            while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
            printf("%3d ", ++no);
            syntax_print(ln, (int)l);
            printf("\r\n");
        }
        fclose(f);
        return 1;
    }
    if (strcmp(cmd, "SAVE") == 0) {
        if (!*a) { printf("usage: SAVE name\r\n"); return 1; }
        if (!g_current[0]) { printf("no program - LOAD or NEW first\r\n"); return 1; }
        char nb[160];
        const char* dst = prog_name(dos_arg(a), nb, sizeof nb);
        if (copy_file(g_current, dst) != 0) { printf("cannot write %s\r\n", dst); return 1; }
        snprintf(g_current, sizeof g_current, "%s", dst);
        printf("saved %s\r\n", dst);
        return 1;
    }
    if (strcmp(cmd, "NEW") == 0) {
        char nb[160];
        const char* raw = dos_arg(a);
        if (!*raw) { g_current[0] = 0; printf("no program\r\n"); return 1; }
        const char* nm = prog_name(raw, nb, sizeof nb);
        FILE* f = fopen(nm, "w");
        if (!f) { printf("cannot create %s\r\n", nm); return 1; }
        fclose(f);
        snprintf(g_current, sizeof g_current, "%s", nm);
#if defined(PICOCALC) || defined(FRUITJAM)
        pico_editor(g_current);
#else
        printf("new %s\r\n", nm);
#endif
        return 1;
    }

    if (strcmp(cmd, "COPY") == 0 || strcmp(cmd, "REN") == 0) {
        char* sp = strchr(a, ' ');
        if (!*a || !sp) { printf("usage: %s from to\r\n", cmd); return 1; }
        *sp = 0;
        const char* src = dos_arg(a);
        char* b = sp + 1;
        const char* dst = dos_arg(b);
        int rc;
        if (cmd[0] == 'C') {
            rc = copy_file(src, dst);
        } else {
            rc = rename(src, dst);
            // Across flash and card a rename becomes copy plus delete.
            if (rc != 0 && errno == EXDEV) {
                rc = copy_file(src, dst);
                if (rc == 0) rc = remove(src);
            }
        }
        if (rc != 0) printf("cannot %s %s\r\n", cmd[0] == 'C' ? "copy" : "rename", src);
        return 1;
    }
    return 0;
}

// The editor needs a screen and a keyboard, nothing else - it draws with
// the same syntax printer the prompt uses and reads through the same key
// reader, so any board that has both can run it.
extern "C" void jdb_con_size(int* cols, int* rows) {
    *cols = 40;
#ifdef FRUITJAM
    *rows = 30;   // 320 by 240 in the 8 by 8 font
#else
    *rows = 40;   // the PicoCalc's panel is square
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

#define HIST_N   16
#define HIST_LEN 256
static char g_hist[HIST_N][HIST_LEN];
static int g_hist_count = 0;
static int g_hist_next = 0;

static void hist_add(const char* line) {
    if (!line[0]) return;
    int last = (g_hist_next + HIST_N - 1) % HIST_N;
    if (g_hist_count > 0 && strcmp(g_hist[last], line) == 0) return;
    strncpy(g_hist[g_hist_next], line, HIST_LEN - 1);
    g_hist[g_hist_next][HIST_LEN - 1] = 0;
    g_hist_next = (g_hist_next + 1) % HIST_N;
    if (g_hist_count < HIST_N) g_hist_count++;
}

static const char* hist_get(int back) {
    if (back < 1 || back > g_hist_count) return nullptr;
    return g_hist[(g_hist_next + HIST_N - back) % HIST_N];
}

static void redraw(const char* buf, int len, int cur, int old_len) {
    // The panel is 40 columns wide; long lines scroll horizontally so
    // the redraw never wraps and stacks. The visible slice prints in
    // colour and the cursor lands by stepping forward, not reprinting.
    (void)old_len;
    const int width = 37;
    int start = 0;
    if (cur > width) start = cur - width;
    int vis = len - start;
    if (vis > width) vis = width;
    printf("\r> ");
    syntax_print(buf + start, vis);
    printf("%*s\r", width - vis, "");
    if (cur - start + 2 > 0) printf("\x1b[%dC", cur - start + 2);
}

static void read_line(char* buf, int cap) {
    int len = 0, cur = 0, old_len = 0;
    int back = 0;
    char stash[HIST_LEN] = {0};
    buf[0] = 0;

    for (;;) {
        int c = repl_read_key();
        if (c == '\r' || c == '\n') {
            redraw(buf, len, len, old_len);
            printf("\r\n");
            break;
        }
        if (c == K_LEFT)  { if (cur > 0) cur--; }
        else if (c == K_RIGHT) { if (cur < len) cur++; }
        else if (c == K_HOME)  { cur = 0; }
        else if (c == K_END)   { cur = len; }
        else if (c == 8 || c == 127) {
            if (cur > 0) {
                memmove(buf + cur - 1, buf + cur, len - cur + 1);
                cur--; len--;
            }
        }
        else if (c == K_DEL) {
            if (cur < len) {
                memmove(buf + cur, buf + cur + 1, len - cur);
                len--;
            }
        }
        else if (c == K_UP || c == K_DOWN) {
            if (c == K_UP && back < g_hist_count) {
                if (back == 0) { strncpy(stash, buf, HIST_LEN - 1); }
                back++;
            } else if (c == K_DOWN && back > 0) {
                back--;
            } else {
                continue;
            }
            const char* src = (back == 0) ? stash : hist_get(back);
            if (!src) src = "";
            strncpy(buf, src, cap - 1);
            buf[cap - 1] = 0;
            len = (int)strlen(buf);
            cur = len;
        }
        else if (c >= 32 && c < 127 && len < cap - 1) {
            memmove(buf + cur + 1, buf + cur, len - cur + 1);
            buf[cur] = (char)c;
            cur++; len++;
            // Appending at the end of a short line: echo the character
            // and skip the full redraw, so pasted input keeps up.
            if (cur == len && len <= 37) {
                printf("%c", c);
                old_len = len;
                continue;
            }
        }
        else {
            continue;
        }
        redraw(buf, len, cur, old_len);
        old_len = len;
    }
    buf[len] = 0;
    hist_add(buf);
}

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
    fruitjam_con_init();
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

#if PICO_RP2040
    printf("\r\njdBasic on RP2040\r\n");
#else
    printf("\r\njdBasic on RP2350\r\n");
#endif

    JdbEmbed* vm = jdb_embed_init();
    if (!vm) {
        printf("VM init failed\r\n");
        for (;;) sleep_ms(1000);
    }
    // Output goes live to the console as programs print - INPUT shows
    // its prompt before it blocks, CLS clears when it runs.
    jdb_embed_output_stdout(vm);

    g_current[0] = 0;

    // Power-on program, with a window to get out of it: without one a
    // looping autorun program would own the board for good.
    char ar_name[128];
    if (autorun_get(ar_name, sizeof ar_name)) {
        printf("autorun %s - ESC to stop\r\n", ar_name);
        fflush(NULL);
        int cancelled = 0;
        for (int i = 0; i < 20 && !cancelled; i++) {
            int c = getchar_timeout_us(100000);
            if (c == 0x1B || c == 3) cancelled = 1;
        }
        if (cancelled) {
            printf("cancelled\r\n");
        } else {
            snprintf(g_current, sizeof g_current, "%s", ar_name);
            char* out = jdb_embed_load(vm, ar_name);
            if (out) {
                printf("%s", out);
                jdb_embed_free(out);
            } else {
                const char* err = jdb_embed_last_error(vm);
                printf("ERROR: %s\r\n", err ? err : "unknown");
            }
        }
        fflush(NULL);
    }

    static char line[1024];
    for (;;) {
        printf("> ");
        read_line(line, sizeof line);
        if (!line[0]) continue;

        if (strncasecmp(line, "HELP", 4) == 0 && (line[4] == 0 || line[4] == ' ')) {
            pico_help(line + 4);
            fflush(NULL);
            continue;
        }

        if (dos_command(line)) {
            fflush(NULL);
            continue;
        }

        // The classic trio, in any spelling, with or without quotes or
        // parentheses: LOAD remembers the current program, RUN executes
        // it (or a named one), EDIT opens it in the editor.
        char* meta_arg = nullptr;
        int meta = 0;
        if (strncasecmp(line, "EDIT", 4) == 0 && (line[4] == 0 || line[4] == ' ' || line[4] == '(' || line[4] == '"')) {
            meta = 1; meta_arg = line + 4;
        } else if (strncasecmp(line, "LOAD", 4) == 0 && (line[4] == 0 || line[4] == ' ' || line[4] == '(' || line[4] == '"')) {
            meta = 2; meta_arg = line + 4;
        } else if (strncasecmp(line, "RUN", 3) == 0 && (line[3] == 0 || line[3] == ' ' || line[3] == '(' || line[3] == '"')) {
            meta = 3; meta_arg = line + 3;
        }
        if (meta) {
            while (*meta_arg == ' ' || *meta_arg == '(') meta_arg++;
            size_t nl = strlen(meta_arg);
            while (nl && (meta_arg[nl-1] == ' ' || meta_arg[nl-1] == ')')) meta_arg[--nl] = 0;
            if (nl >= 2 && (meta_arg[0] == '"' || meta_arg[0] == '\'')) {
                meta_arg++;
                nl -= 2;
                meta_arg[nl] = 0;
            }
            char nb[160];
            const char* nm = *meta_arg ? prog_name(meta_arg, nb, sizeof nb) : g_current;
            if (!*nm) {
                printf("no program loaded - LOAD name first\r\n");
                fflush(NULL);
                continue;
            }
            if (meta == 1) {
#if defined(PICOCALC) || defined(FRUITJAM)
                snprintf(g_current, sizeof g_current, "%s", nm);
                pico_editor(nm);
#else
                printf("no editor without a screen\r\n");
#endif
            } else if (meta == 2) {
                FILE* probe = fopen(nm, "r");
                if (!probe) {
                    printf("cannot open %s\r\n", nm);
                } else {
                    fclose(probe);
                    snprintf(g_current, sizeof g_current, "%s", nm);
                    printf("loaded %s\r\n", nm);
                }
            } else {
                snprintf(g_current, sizeof g_current, "%s", nm);
                char* out = jdb_embed_load(vm, nm);
                if (out) {
                    printf("%s", out);
                    jdb_embed_free(out);
                } else {
                    const char* err = jdb_embed_last_error(vm);
                    printf("ERROR: %s\r\n", err ? err : "unknown");
                }
            }
            fflush(NULL);
            continue;
        }

        char* out = jdb_embed_eval(vm, line);
        if (out) {
            printf("%s", out);
            jdb_embed_free(out);
        } else {
            const char* err = jdb_embed_last_error(vm);
            printf("ERROR: %s\r\n", err ? err : "unknown");
        }
        // Escape sequences carry no newline; without a flush a CLS sits
        // in stdout until the next PRINT pushes it out.
        fflush(NULL);
    }
}
