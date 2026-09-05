// The panel as a text console: 40 columns by 30 rows of the 8x8 font,
// the same grid a jdBasic listing assumes and the same one the PicoCalc
// shows. Characters mark their row dirty, a flush sends the rows that
// changed, and scrolling moves the text and marks everything dirty.
//
// The PicoCalc scrolls with the panel's own registers over a ring in
// display RAM, because redrawing forty rows over a 25 MHz link costs a
// tenth of a second. Here the frame is already in PSRAM and a row is
// 5 KB, so a scroll is a memmove and thirty small transfers.

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include "esp_vfs.h"
#include "../../pico/picocalc_font.h"
#include "../../common/jdb_glyphs.h"

extern int   es3c28p_lcd_ready(void);
extern void  es3c28p_lcd_blit_rows(int y0, int rows);
extern uint16_t* es3c28p_lcd_row(int y);
extern uint16_t  es3c28p_lcd_encode(int r, int g, int b);

#define COLS 40
#define ROWS 30
#define CW   8
#define CH   8

static char    g_text[ROWS][COLS];
static uint8_t g_attr[ROWS][COLS];
static uint8_t g_dirty[ROWS];
static int g_cx = 0, g_cy = 0;
static int g_on = 0;
static int g_cursor = 1;

// Index 0 is the default. The SGR escapes pick from here, so a program
// that colours its output on a terminal colours it on the panel too.
//
// Sixteen rather than eight, and green has its own place. On the
// PicoCalc the default ink is green, so green could map onto the default
// and nobody noticed; here the default is a soft green-white, and a
// program printing green got white. Eight entries left no room to fix
// that, so the attribute carries four bits now instead of three.
#define PAL_N 16
static const uint8_t g_pal[PAL_N][3] = {
    { 0xC8, 0xE8, 0xC8 },       // 0 default, a soft green-white
    { 0xF8, 0xF8, 0xF8 },       // 1 white
    { 0xF8, 0xE8, 0x40 },       // 2 yellow
    { 0x50, 0xD8, 0xF8 },       // 3 cyan
    { 0x90, 0x90, 0x90 },       // 4 gray
    { 0xF8, 0x50, 0x50 },       // 5 red
    { 0xE0, 0x60, 0xE0 },       // 6 magenta
    { 0x60, 0x80, 0xF8 },       // 7 blue
    { 0x50, 0xE8, 0x70 },       // 8 green
    { 0xF8, 0xA0, 0x40 },       // 9 orange
    { 0x30, 0x90, 0x40 },       // 10 dark green
    { 0xA0, 0x50, 0xF8 },       // 11 violet
    { 0x60, 0x60, 0x70 },       // 12 dim
    { 0xF8, 0x90, 0xB0 },       // 13 pink
    { 0x40, 0xB0, 0xB0 },       // 14 teal
    { 0xE8, 0xE8, 0xB0 },       // 15 sand
};
static uint8_t g_cur_attr = 0;

static void draw_row(int row) {
    const uint8_t* bg = NULL;
    uint16_t back = es3c28p_lcd_encode(0, 0, 0);
    (void)bg;
    for (int line = 0; line < CH; line++) {
        uint16_t* px = es3c28p_lcd_row(row * CH + line);
        if (!px) return;
        for (int col = 0; col < COLS; col++) {
            uint8_t bits = jdb_cell_rows(jdos_font8x8_c64, (uint8_t)g_text[row][col])[line];
            int inv = g_cursor && row == g_cy && col == g_cx;
            const uint8_t* fg = g_pal[g_attr[row][col] & (PAL_N - 1)];
            uint16_t ink = es3c28p_lcd_encode(fg[0], fg[1], fg[2]);
            for (int b = 0; b < CW; b++) {
                int on = (bits & (0x80 >> b)) != 0;
                if (inv) on = !on;
                px[col * CW + b] = on ? ink : back;
            }
        }
    }
}

void es3c28p_con_flush(void) {
    if (!g_on) return;
    for (int r = 0; r < ROWS; r++) {
        if (!g_dirty[r]) continue;
        draw_row(r);
        es3c28p_lcd_blit_rows(r * CH, CH);
        g_dirty[r] = 0;
    }
}

static void clear_screen(void) {
    memset(g_text, ' ', sizeof g_text);
    memset(g_attr, 0, sizeof g_attr);
    memset(g_dirty, 1, sizeof g_dirty);
    g_cx = 0; g_cy = 0;
}

static void scroll_up(void) {
    memmove(g_text[0], g_text[1], (ROWS - 1) * COLS);
    memset(g_text[ROWS - 1], ' ', COLS);
    memmove(g_attr[0], g_attr[1], (ROWS - 1) * COLS);
    memset(g_attr[ROWS - 1], 0, COLS);
    memset(g_dirty, 1, sizeof g_dirty);
}

// The escape subset a full-screen editor needs: absolute cursor, clear
// screen, clear to end of line, cursor right, and the colour selects.
// Enough that the same byte stream drives the panel and a terminal.
static int g_esc = 0;
static int g_par[4];
static int g_parn = 0;

static int ansi_step(char c) {
    if (g_esc == 0) {
        if (c == 0x1B) { g_esc = 1; return 1; }
        return 0;
    }
    if (g_esc == 1) {
        if (c == '[') {
            g_esc = 2; g_parn = 0;
            memset(g_par, 0, sizeof g_par);
            return 1;
        }
        g_esc = 0;
        return 1;
    }
    if (c >= '0' && c <= '9') {
        g_par[g_parn] = g_par[g_parn] * 10 + (c - '0');
        return 1;
    }
    if (c == ';') {
        if (g_parn < 3) g_parn++;
        return 1;
    }
    g_esc = 0;
    if (c == 'H' || c == 'f') {
        int row = g_par[0] > 0 ? g_par[0] - 1 : 0;
        int col = (g_parn >= 1 && g_par[1] > 0) ? g_par[1] - 1 : 0;
        g_dirty[g_cy] = 1;
        g_cy = row < ROWS ? row : ROWS - 1;
        g_cx = col < COLS ? col : COLS - 1;
        g_dirty[g_cy] = 1;
    } else if (c == 'J') {
        clear_screen();
    } else if (c == 'K') {
        for (int x = g_cx; x < COLS; x++) {
            g_text[g_cy][x] = ' ';
            g_attr[g_cy][x] = 0;
        }
        g_dirty[g_cy] = 1;
    } else if (c == 'C') {
        int n = g_par[0] > 0 ? g_par[0] : 1;
        g_dirty[g_cy] = 1;
        g_cx = g_cx + n < COLS ? g_cx + n : COLS - 1;
    } else if (c == 'm') {
        for (int i = 0; i <= g_parn; i++) {
            switch (g_par[i]) {
                case 0:            g_cur_attr = 0; break;
                case 30: case 90:  g_cur_attr = 4; break;
                case 31: case 91:  g_cur_attr = 5; break;
                case 32: case 92:  g_cur_attr = 8; break;
                case 33: case 93:  g_cur_attr = 2; break;
                case 34: case 94:  g_cur_attr = 7; break;
                case 35: case 95:  g_cur_attr = 6; break;
                case 36: case 96:  g_cur_attr = 3; break;
                case 37: case 97:  g_cur_attr = 1; break;
            }
        }
    }
    return 1;
}

void es3c28p_con_putc(char c) {
    if (!g_on) return;
    int prev_cy = g_cy;
    if (ansi_step(c)) return;
    if (c == '\r') { g_cx = 0; g_dirty[g_cy] = 1; return; }
    if (c == '\n') {
        g_cx = 0;
        g_dirty[g_cy] = 1;
        if (++g_cy >= ROWS) { g_cy = ROWS - 1; scroll_up(); }
        g_dirty[g_cy] = 1;
        return;
    }
    if (c == '\b' || c == 127) {
        if (g_cx > 0) {
            g_cx--;
            g_text[g_cy][g_cx] = ' ';
            g_attr[g_cy][g_cx] = 0;
            g_dirty[g_cy] = 1;
        }
        return;
    }
    if (c == '\f') { clear_screen(); return; }
    if (c == '\t') {
        int next = (g_cx / 8 + 1) * 8;
        if (next >= COLS) next = COLS - 1;
        while (g_cx < next) { g_attr[g_cy][g_cx] = 0; g_text[g_cy][g_cx++] = ' '; }
        g_dirty[g_cy] = 1;
        return;
    }
    if ((unsigned char)c < 32) return;

    // Text arrives as UTF-8; a sequence is one cell.
    static struct jdb_utf8_dec dec = { 0, 0, 0 };
    uint32_t v[2];
    int n = jdb_utf8_feed(&dec, (unsigned char)c, v);
    for (int i = 0; i < n; i++) {
        g_text[g_cy][g_cx] = (char)jdb_cell_for(v[i]);
        g_attr[g_cy][g_cx] = g_cur_attr;
        g_dirty[g_cy] = 1;
        if (++g_cx >= COLS) {
            g_cx = 0;
            if (++g_cy >= ROWS) { g_cy = ROWS - 1; scroll_up(); }
        }
    }
    if (g_cy != prev_cy) g_dirty[prev_cy] = 1;
}

void es3c28p_con_write(const char* s, int n) {
    if (!g_on) return;
    for (int i = 0; i < n; i++) es3c28p_con_putc(s[i]);
    es3c28p_con_flush();
}

int es3c28p_con_on(void) { return g_on; }

// Everything printed goes to both places. Rather than route the REPL's
// own printf calls one by one, stdout is replaced with a file whose
// write hands the bytes to the panel and then on to the original - so
// the prompt, a DIR listing, an error and PRINT all land on the glass
// without any of them knowing about it.
static FILE* g_real;

static ssize_t tee_write(int fd, const void* data, size_t size) {
    (void)fd;
    es3c28p_con_write((const char*)data, (int)size);
    if (g_real) {
        fwrite(data, 1, size, g_real);
        fflush(g_real);
    }
    return (ssize_t)size;
}

static int tee_open(const char* path, int flags, int mode) {
    (void)path; (void)flags; (void)mode;
    return 0;
}

static int tee_close(int fd) { (void)fd; return 0; }

static int install_tee(void) {
    if (g_real) return 0;
    esp_vfs_t vfs = {0};
    vfs.flags = ESP_VFS_FLAG_DEFAULT;
    vfs.write = &tee_write;
    vfs.open = &tee_open;
    vfs.close = &tee_close;
    if (esp_vfs_register("/scr", &vfs, NULL) != ESP_OK) return -1;
    FILE* f = fopen("/scr/0", "w");
    if (!f) return -2;
    setvbuf(f, NULL, _IONBF, 0);
    g_real = stdout;
    stdout = f;
    return 0;
}

int es3c28p_con_enable(int on) {
    if (on && !es3c28p_lcd_ready()) return -1;
    if (on && install_tee() != 0) return -2;
    g_on = on ? 1 : 0;
    if (g_on) {
        clear_screen();
        es3c28p_con_flush();
    }
    return 0;
}

void es3c28p_con_cursor(int on) { g_cursor = on ? 1 : 0; g_dirty[g_cy] = 1; }

void es3c28p_con_size(int* cols, int* rows) { *cols = COLS; *rows = ROWS; }
