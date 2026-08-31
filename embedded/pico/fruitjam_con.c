// A text console on the DVI framebuffer, so the board is a computer on its
// own rather than a thing that needs a PC to be watched through.
//
// Forty columns by thirty rows of the 8 by 8 font. The prompt's line
// editor talks in the same escape sequences it sends down the serial
// port - it colours its syntax and steps the cursor forward rather than
// reprinting - so this understands enough of them to follow: the colours
// it uses, cursor left and right, erase to end of line, and clear.
// Anything else in a sequence is swallowed rather than printed as
// wreckage.

#include <stdint.h>
#include <string.h>
#include "pico/stdio/driver.h"
#include "picocalc_font.h"

uint8_t* fruitjam_dvi_framebuffer(void);
size_t   fruitjam_dvi_stride(void);

#define COLS 40
#define ROWS 30
#define CH_W 8
#define CH_H 8

// The framebuffer stores every pixel twice across, which is the
// horizontal doubling, so a character cell is 16 bytes wide.
#define CELL_BYTES (CH_W * 2)

static int     g_col = 0, g_row = 0;
static uint8_t g_fg = 0xB6;     // light gray, the resting colour
static uint8_t g_bg = 0x00;

// Escape sequence state: 0 idle, 1 saw ESC, 2 inside a CSI.
static int  g_esc = 0;
static char g_seq[16];
static int  g_seq_len = 0;

static int g_cursor_on = 0;
static volatile int g_busy = 0;
static int g_blink = 0;

static void cell_fill(int col, int row, uint8_t colour) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb) return;
    size_t stride = fruitjam_dvi_stride();
    for (int y = 0; y < CH_H; y++)
        memset(fb + (size_t)(row * CH_H + y) * stride + (size_t)col * CELL_BYTES,
               colour, CELL_BYTES);
}

static void cell_glyph(int col, int row, unsigned char c) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb) return;
    size_t stride = fruitjam_dvi_stride();
    if (c < 32 || c > 151) c = 32;
    const uint8_t* gl = &jdos_font8x8_c64[(c - 32) * 8];
    for (int y = 0; y < CH_H; y++) {
        uint8_t bits = gl[y];
        uint8_t* p = fb + (size_t)(row * CH_H + y) * stride + (size_t)col * CELL_BYTES;
        for (int x = 0; x < CH_W; x++) {
            uint8_t v = (bits & (0x80 >> x)) ? g_fg : g_bg;
            *p++ = v;
            *p++ = v;
        }
    }
}

static void cursor_draw(int on) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb || g_col >= COLS || g_row >= ROWS) return;
    size_t stride = fruitjam_dvi_stride();
    // An underline, so it marks the place without hiding the character.
    memset(fb + (size_t)(g_row * CH_H + CH_H - 1) * stride + (size_t)g_col * CELL_BYTES,
           on ? g_fg : g_bg, CELL_BYTES);
}

static void scroll_up(void) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb) return;
    size_t stride = fruitjam_dvi_stride();
    memmove(fb, fb + (size_t)CH_H * stride, (size_t)(ROWS - 1) * CH_H * stride);
    memset(fb + (size_t)(ROWS - 1) * CH_H * stride, g_bg, (size_t)CH_H * stride);
}

static void newline(void) {
    g_col = 0;
    if (++g_row >= ROWS) { g_row = ROWS - 1; scroll_up(); }
}

static void put_printable(char c) {
    if (g_col >= COLS) newline();
    cell_glyph(g_col, g_row, (unsigned char)c);
    g_col++;
}

// Only the colours the prompt actually emits, plus a reset.
static void sgr(int code) {
    switch (code) {
        case 0:  g_fg = 0xB6; break;
        case 90: g_fg = 0x92; break;
        case 93: g_fg = 0xFC; break;
        case 96: g_fg = 0x1F; break;
        case 97: g_fg = 0xFF; break;
        case 91: g_fg = 0xE0; break;
        case 92: g_fg = 0x1C; break;
        default: break;
    }
}

static int seq_number(void) {
    int n = 0, any = 0;
    for (int i = 0; i < g_seq_len; i++) {
        if (g_seq[i] >= '0' && g_seq[i] <= '9') { n = n * 10 + (g_seq[i] - '0'); any = 1; }
        else break;
    }
    return any ? n : -1;
}

static void csi_final(char f) {
    int n = seq_number();
    switch (f) {
        case 'm': {
            // A run of semicolon-separated codes; the prompt sends one.
            int v = 0, seen = 0;
            for (int i = 0; i <= g_seq_len; i++) {
                char c = (i < g_seq_len) ? g_seq[i] : ';';
                if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); seen = 1; }
                else if (c == ';') { sgr(seen ? v : 0); v = 0; seen = 0; }
            }
            if (!g_seq_len) sgr(0);
            break;
        }
        case 'C': g_col += (n > 0 ? n : 1); if (g_col > COLS) g_col = COLS; break;
        case 'D': g_col -= (n > 0 ? n : 1); if (g_col < 0) g_col = 0; break;
        case 'G': g_col = (n > 0 ? n - 1 : 0); break;
        case 'K':
            for (int c = g_col; c < COLS; c++) cell_fill(c, g_row, g_bg);
            break;
        case 'J':
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++) cell_fill(c, r, g_bg);
            g_col = g_row = 0;
            break;
        case 'H': g_col = 0; g_row = 0; break;
        default: break;
    }
}

void fruitjam_con_putc(char c) {
    if (g_esc == 1) {
        g_esc = (c == '[') ? 2 : 0;
        g_seq_len = 0;
        return;
    }
    if (g_esc == 2) {
        if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
            if (g_seq_len < (int)sizeof(g_seq) - 1) g_seq[g_seq_len++] = c;
            return;
        }
        csi_final(c);
        g_esc = 0;
        return;
    }
    switch (c) {
        case 0x1B: g_esc = 1; return;
        case '\r': g_col = 0; return;
        case '\n': newline(); return;
        case '\t': do { put_printable(' '); } while (g_col % 4); return;
        case 8: if (g_col > 0) g_col--; return;
        case 7: return;
        default: break;
    }
    if ((unsigned char)c >= 32) put_printable(c);
}

void fruitjam_con_write(const char* s, int len) {
    g_busy = 1;
    if (g_cursor_on) { cursor_draw(0); g_cursor_on = 0; }
    for (int i = 0; i < len; i++) fruitjam_con_putc(s[i]);
    // Freshly typed text keeps the cursor lit, so it never blinks out
    // from under the character you are looking at.
    cursor_draw(1);
    g_cursor_on = 1;
    g_blink = 0;
    g_busy = 0;
}

// The scanout hands out sixty of these a second, which is the only clock
// this needs. Half a second on, half a second off.
void fruitjam_con_tick(void) {
    if (g_busy) return;
    if (++g_blink < 30) return;
    g_blink = 0;
    g_cursor_on = !g_cursor_on;
    cursor_draw(g_cursor_on);
}

static void con_out_chars(const char* buf, int len) { fruitjam_con_write(buf, len); }

static stdio_driver_t con_driver = {
    .out_chars = con_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

void fruitjam_con_init(void) { stdio_set_driver_enabled(&con_driver, true); }
