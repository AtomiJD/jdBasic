// The REPL: a prompt feeding the embedded VM, one persistent
// interpreter for the whole session. Console I/O runs through the
// SDK's stdio, which carries both USB-CDC and - on a PicoCalc - the
// screen and keyboard, registered here as one more stdio driver.
// The input line is a small editor: cursor keys, insert, delete,
// home, end, and a history ring on up and down.

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#ifdef JDB_HAS_CYW43
#include "pico/cyw43_arch.h"
#endif
#include "jdb_embed_api.h"

extern "C" void jdb_pico_fs_init(void);
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

extern "C" int repl_read_key(void);
void pico_help(const char* topic);

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
#ifdef PICOCALC
void pico_editor(const char* name);
#endif

extern "C" int repl_read_key(void) {
    int c = getchar();
    if (c != 0x1B) return c;
    int c2 = getchar();
    if (c2 != '[') return 0;
    int c3 = getchar();
    switch (c3) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case '3': getchar(); return K_DEL;
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
    // the redraw never wraps and stacks.
    (void)old_len;
    const int width = 37;
    int start = 0;
    if (cur > width) start = cur - width;
    printf("\r> %-*.*s", width, width, buf + start);
    printf("\r> ");
    for (int i = start; i < cur; i++) printf("%c", buf[i]);
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
    stdio_init_all();
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

    static char line[1024];
    static char g_current[128];
    g_current[0] = 0;
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
            const char* nm = *meta_arg ? meta_arg : g_current;
            if (!*nm) {
                printf("no program loaded - LOAD name first\r\n");
                fflush(NULL);
                continue;
            }
            if (meta == 1) {
#ifdef PICOCALC
                snprintf(g_current, sizeof g_current, "%s", nm);
                pico_editor(nm);
#else
                printf("no editor on a bare board\r\n");
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
