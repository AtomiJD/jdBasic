// The prompt, shared by every board: a line editor with history and
// syntax colour, the DOS-flavoured file verbs, the program verbs LIST,
// NEW, SAVE, LOAD, RUN and EDIT, and the power-on program behind
// AUTORUN. Everything here is C library plus the port's four hooks, so
// a board only has to say how a byte arrives and where the autorun name
// lives.
//
// Newlines are written bare. The RP2350's stdio turns one into CR LF on
// the way out and the ESP32's VFS does the same, so the escape belongs
// to neither.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "jdb_repl.h"

extern "C" int repl_read_key(void);
extern "C" void jdb_con_size(int* cols, int* rows);
void syntax_print(const char* s, int n);
void jdb_help(const char* topic);
void jdb_editor(const char* name);

// Key codes as the PicoCalc's keyboard controller sends them; a USB
// terminal's escape sequences fold into the same values.
#define K_LEFT  0xB4
#define K_UP    0xB5
#define K_DOWN  0xB6
#define K_RIGHT 0xB7
#define K_HOME  0xD2
#define K_DEL   0xD4
#define K_END   0xD5

static const JdbReplPort* g_port = nullptr;
static JdbEmbed* g_vm = nullptr;

// The program the prompt is working on, by name. LIST, SAVE, EDIT and
// RUN all mean "this one" when given no argument.
static char g_current[128];

const char* jdb_repl_arg(char* s) {
    while (*s == ' ') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = 0;
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) {
        s[n - 1] = 0;
        s++;
    }
    return s;
}

const char* jdb_repl_progname(const char* in, char* out, size_t cap) {
    if (strchr(in, '.')) return in;
    // A file that really has no extension keeps its name.
    FILE* f = fopen(in, "r");
    if (f) { fclose(f); return in; }
    snprintf(out, cap, "%s.jdb", in);
    return out;
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

static int autorun_get(char* out, size_t cap) {
    FILE* f = fopen(g_port->autorun_path, "r");
    if (!f) return 0;
    if (!fgets(out, (int)cap, f)) { fclose(f); return 0; }
    fclose(f);
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) out[--n] = 0;
    return out[0] ? 1 : 0;
}

static void run_file(const char* name) {
    snprintf(g_current, sizeof g_current, "%s", name);
    char* out = jdb_embed_load(g_vm, name);
    if (out) {
        printf("%s", out);
        jdb_embed_free(out);
    } else {
        const char* err = jdb_embed_last_error(g_vm);
        printf("ERROR: %s\n", err ? err : "unknown");
    }
}

// Takes a file straight off the wire. The line editor echoes every
// keystroke as a full recoloured line, which costs kilobytes per line of
// source; here nothing is echoed and nothing is parsed, so a program
// arrives at the speed of the link.
// With a byte count the transfer is raw: no line-ending translation and
// no terminator byte, because a compiled program contains every byte
// there is, including the ones that used to mean "stop" and "newline".
// One acknowledgement per block, and nothing left over for the parser.
//
// Storing what has arrived stops reading the line for as long as a flash
// erase takes, which is around fifty milliseconds, and the port's buffer
// holds sixty four bytes. A sender that keeps going loses everything it
// sends meanwhile, the read then times out, and - this is the part that
// bites - the rest of the file arrives at the prompt and is run as
// commands. A graphics program's first two lines turn the console off
// and clear the screen, so a truncated transfer looked exactly like the
// board dying.
#define RECV_BLOCK 256

static void recv_binary(const char* name, long want) {
    FILE* f = fopen(name, "wb");
    if (!f) { printf("cannot write %s\n", name); return; }
    printf("receiving %s, %ld bytes\n", name, want);
    fflush(NULL);
    long n = 0;
    int block = 0;
    while (n < want) {
        int c = g_port->read_byte_ms(5000);
        if (c < 0) break;
        fputc(c, f);
        n++;
        if (++block == RECV_BLOCK) {
            block = 0;
            // Stored first, then acknowledged, so the sender waits
            // through the erase instead of talking into it.
            fflush(f);
            putchar('#');
            fflush(NULL);
        }
    }
    fclose(f);

    if (n < want) {
        // Whatever is still on its way belongs to the file, not to the
        // prompt.
        int drained = 0;
        while (g_port->read_byte_ms(400) >= 0) drained++;
        printf("%ld of %ld bytes SHORT, %d discarded\n", n, want, drained);
        return;
    }
    printf("%ld of %ld bytes\n", n, want);
}

static void recv_file(const char* name) {
    FILE* f = fopen(name, "w");
    if (!f) { printf("cannot write %s\n", name); return; }
    printf("receiving %s, end with Ctrl-D\n", name);
    fflush(NULL);
    size_t n = 0;
    int pending_cr = 0;
    for (;;) {
        int c = g_port->read_byte_ms(n ? 3000 : 30000);
        if (c < 0 || c == 0x04) break;
        if (c == 0x1B) {
            fclose(f);
            remove(name);
            printf("cancelled\n");
            return;
        }
        // Whatever the sender's line endings, the file gets one newline.
        if (pending_cr && c == '\n') { pending_cr = 0; continue; }
        pending_cr = 0;
        if (c == '\r') { pending_cr = 1; c = '\n'; }
        fputc(c, f);
        n++;
    }
    fclose(f);
    printf("%u bytes\n", (unsigned)n);
}

static void list_file(const char* name) {
    FILE* f = fopen(name, "r");
    if (!f) { printf("cannot open %s\n", name); return; }
    char ln[256];
    int no = 0;
    while (fgets(ln, sizeof ln, f)) {
        size_t l = strlen(ln);
        while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
        printf("%3d ", ++no);
        syntax_print(ln, (int)l);
        printf("\n");
    }
    fclose(f);
}

static void dir_listing(const char* path) {
    DIR* d = opendir(path);
    if (!d) { printf("cannot open %s\n", path); return; }
    struct dirent* e;
    int n = 0;
    unsigned long bytes = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (e->d_type == DT_DIR) {
            printf("%-24s   <DIR>\n", e->d_name);
        } else {
            char full[192];
            snprintf(full, sizeof full, "%s/%s", path, e->d_name);
            struct stat st;
            long sz = (stat(full, &st) == 0) ? (long)st.st_size : 0;
            printf("%-24s %7ld\n", e->d_name, sz);
            bytes += (unsigned long)sz;
        }
        n++;
    }
    closedir(d);
    printf("%d entries, %lu bytes\n", n, bytes);
}

// The DOS set at the prompt, unquoted arguments welcome. Returns 0 when
// the line is not one of them and belongs to the interpreter instead.
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

    if (strcmp(cmd, "HELP") == 0) { jdb_help(a); return 1; }

    if (strcmp(cmd, "AUTORUN") == 0) {
        char name[128];
        const char* arg = jdb_repl_arg(a);
        if (!*arg) {
            if (autorun_get(name, sizeof name)) printf("autorun: %s\n", name);
            else printf("autorun: off\n");
        } else if (strcasecmp(arg, "OFF") == 0) {
            remove(g_port->autorun_path);
            printf("autorun off\n");
        } else {
            char nb[160];
            const char* nm = jdb_repl_progname(arg, nb, sizeof nb);
            FILE* probe = fopen(nm, "r");
            if (!probe) { printf("cannot open %s\n", nm); return 1; }
            fclose(probe);
            FILE* f = fopen(g_port->autorun_path, "w");
            if (!f) { printf("cannot save autorun\n"); return 1; }
            fprintf(f, "%s\n", nm);
            if (fclose(f) != 0) printf("cannot save autorun\n");
            else printf("autorun: %s\n", nm);
        }
        return 1;
    }

    if (strcmp(cmd, "RECV") == 0) {
        char* sp = strchr(a, ' ');
        long want = 0;
        if (sp) {
            *sp = 0;
            want = strtol(sp + 1, NULL, 10);
        }
        const char* nm = jdb_repl_arg(a);
        if (!*nm) { printf("RECV name [bytes]\n"); return 1; }
        if (want > 0) recv_binary(nm, want);
        else recv_file(nm);
        return 1;
    }

#ifndef ESP32
    // IDF has no working directory, so the board that lacks one says so
    // rather than pretending.
    if (strcmp(cmd, "CD") == 0) {
        if (*a && chdir(jdb_repl_arg(a)) != 0) { printf("no such directory\n"); return 1; }
        char cwd[160];
        printf("%s\n", getcwd(cwd, sizeof cwd) ? cwd : "?");
        return 1;
    }
#endif

    // TYPE steps aside for a one-line TYPE..ENDTYPE declaration.
    if (strcmp(cmd, "TYPE") == 0 && *a && !strchr(a, ':')) {
        FILE* f = fopen(jdb_repl_arg(a), "r");
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
        printf(remove(jdb_repl_arg(a)) == 0 ? "deleted\n" : "cannot delete %s\n", a);
        return 1;
    }

    if ((strcmp(cmd, "MD") == 0 || strcmp(cmd, "RD") == 0) && *a) {
        int rc = cmd[0] == 'M' ? mkdir(jdb_repl_arg(a), 0777) : rmdir(jdb_repl_arg(a));
        if (rc != 0) printf("cannot %s %s\n", cmd[0] == 'M' ? "create" : "remove", a);
        return 1;
    }

    if (strcmp(cmd, "DIR") == 0) {
        dir_listing(*a ? jdb_repl_arg(a) : ".");
        return 1;
    }

    // The program is the file; these three work on it by name. LIST is
    // TYPE with line numbers and the editor's colours, SAVE is a copy
    // that also moves the name along, and NEW starts an empty one.
    if (strcmp(cmd, "LIST") == 0) {
        char nb[160];
        const char* nm = *a ? jdb_repl_progname(jdb_repl_arg(a), nb, sizeof nb) : g_current;
        if (!*nm) { printf("no program - LOAD name first\n"); return 1; }
        list_file(nm);
        return 1;
    }

    if (strcmp(cmd, "SAVE") == 0) {
        if (!*a) { printf("usage: SAVE name\n"); return 1; }
        if (!g_current[0]) { printf("no program - LOAD or NEW first\n"); return 1; }
        char nb[160];
        const char* dst = jdb_repl_progname(jdb_repl_arg(a), nb, sizeof nb);
        if (copy_file(g_current, dst) != 0) { printf("cannot write %s\n", dst); return 1; }
        snprintf(g_current, sizeof g_current, "%s", dst);
        printf("saved %s\n", dst);
        return 1;
    }

    if (strcmp(cmd, "NEW") == 0) {
        char nb[160];
        const char* raw = jdb_repl_arg(a);
        if (!*raw) { g_current[0] = 0; printf("no program\n"); return 1; }
        const char* nm = jdb_repl_progname(raw, nb, sizeof nb);
        FILE* f = fopen(nm, "w");
        if (!f) { printf("cannot create %s\n", nm); return 1; }
        fclose(f);
        snprintf(g_current, sizeof g_current, "%s", nm);
        if (g_port->before_edit) g_port->before_edit();
        jdb_editor(g_current);
        return 1;
    }

    if ((strcmp(cmd, "COPY") == 0 || strcmp(cmd, "REN") == 0) && *a) {
        char* sp = strchr(a, ' ');
        if (!sp) { printf("usage: %s from to\n", cmd); return 1; }
        *sp = 0;
        const char* src = jdb_repl_arg(a);
        char* b = sp + 1;
        const char* dst = jdb_repl_arg(b);
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
        if (rc != 0) printf("cannot %s %s\n", cmd[0] == 'C' ? "copy" : "rename", src);
        return 1;
    }

    if (g_port->board_command && g_port->board_command(cmd, a)) return 1;
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

static int line_width(void) {
    int cols = 40, rows = 40;
    jdb_con_size(&cols, &rows);
    return cols - 3;                    // the prompt takes two, the cursor one
}

static void redraw(const char* buf, int len, int cur) {
    // Long lines scroll sideways so the redraw never wraps and stacks.
    // The visible slice prints in colour and the cursor lands by stepping
    // forward, not by reprinting.
    const int width = line_width();
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
    int len = 0, cur = 0;
    int back = 0;
    char stash[HIST_LEN] = {0};
    buf[0] = 0;

    for (;;) {
        int c = repl_read_key();
        if (c == '\r' || c == '\n') {
            redraw(buf, len, len);
            printf("\n");
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
                if (back == 0) strncpy(stash, buf, HIST_LEN - 1);
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
            if (cur == len && len <= line_width()) {
                printf("%c", c);
                continue;
            }
        }
        else {
            continue;
        }
        redraw(buf, len, cur);
    }
    buf[len] = 0;
    hist_add(buf);
}

// LOAD, RUN and EDIT name a file rather than an expression, in any
// spelling, with or without quotes or parentheses.
static int program_verb(char* line) {
    char* arg = nullptr;
    int verb = 0;
    if (strncasecmp(line, "EDIT", 4) == 0 &&
        (line[4] == 0 || line[4] == ' ' || line[4] == '(' || line[4] == '"')) {
        verb = 1; arg = line + 4;
    } else if (strncasecmp(line, "LOAD", 4) == 0 &&
               (line[4] == 0 || line[4] == ' ' || line[4] == '(' || line[4] == '"')) {
        verb = 2; arg = line + 4;
    } else if (strncasecmp(line, "RUN", 3) == 0 &&
               (line[3] == 0 || line[3] == ' ' || line[3] == '(' || line[3] == '"')) {
        verb = 3; arg = line + 3;
    }
    if (!verb) return 0;

    while (*arg == ' ' || *arg == '(') arg++;
    size_t nl = strlen(arg);
    while (nl && (arg[nl - 1] == ' ' || arg[nl - 1] == ')')) arg[--nl] = 0;
    if (nl >= 2 && (arg[0] == '"' || arg[0] == '\'')) {
        arg++;
        nl -= 2;
        arg[nl] = 0;
    }
    char nb[160];
    const char* nm = *arg ? jdb_repl_progname(arg, nb, sizeof nb) : g_current;
    if (!*nm) {
        printf("no program loaded - LOAD name first\n");
        return 1;
    }
    if (verb == 1) {
        snprintf(g_current, sizeof g_current, "%s", nm);
        if (g_port->before_edit) g_port->before_edit();
        jdb_editor(nm);
    } else if (verb == 2) {
        FILE* probe = fopen(nm, "r");
        if (!probe) {
            printf("cannot open %s\n", nm);
        } else {
            fclose(probe);
            snprintf(g_current, sizeof g_current, "%s", nm);
            printf("loaded %s\n", nm);
        }
    } else {
        run_file(nm);
    }
    return 1;
}

// Sixteen columns for the command and the rest for what it does, so the
// two make columns rather than a paragraph.
void jdb_repl_hints(void) {
    static const char* const hints[][2] = {
        { "HELP",       "the board's own manual" },
        { "DIR",        "what is on the board" },
        { "EDIT name",  "write something" },
        { "RUN name",   "start it" },
    };
    for (size_t i = 0; i < sizeof hints / sizeof hints[0]; i++)
        printf(" \x1b[96m%-14s\x1b[0m%s\n", hints[i][0], hints[i][1]);
    printf("\n");
}

void jdb_repl_run(JdbEmbed* vm, const JdbReplPort* port) {
    g_vm = vm;
    g_port = port;
    g_current[0] = 0;

    if (port->hello) port->hello();

    // Power-on program, with a window to get out of it: without one a
    // looping autorun program would own the board for good.
    char ar_name[128];
    if (autorun_get(ar_name, sizeof ar_name)) {
        printf("autorun %s - ESC to stop\n", ar_name);
        fflush(NULL);
        int cancelled = 0;
        for (int i = 0; i < 20 && !cancelled; i++) {
            int c = port->read_byte_ms(100);
            if (c == 0x1B || c == 3) cancelled = 1;
        }
        if (cancelled) printf("cancelled\n");
        else run_file(ar_name);
        fflush(NULL);
    }

    static char line[1024];
    for (;;) {
        printf("> ");
        fflush(NULL);
        read_line(line, sizeof line);
        if (!line[0]) continue;

        if (dos_command(line) || program_verb(line)) {
            fflush(NULL);
            continue;
        }

        char* out = jdb_embed_eval(vm, line);
        if (out) {
            printf("%s", out);
            jdb_embed_free(out);
        } else {
            const char* err = jdb_embed_last_error(vm);
            printf("ERROR: %s\n", err ? err : "unknown");
        }
        // Escape sequences carry no newline; without a flush a CLS sits
        // in stdout until the next PRINT pushes it out.
        fflush(NULL);
    }
}
