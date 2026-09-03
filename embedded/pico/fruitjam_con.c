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
static uint8_t g_fg = 0x3C;     // green, the resting colour
static uint8_t g_bg = 0x00;

// Escape sequence state: 0 idle, 1 saw ESC, 2 inside a CSI.
static int  g_esc = 0;
static char g_seq[16];
static int  g_seq_len = 0;

static int g_cursor_on = 0;
static int g_reverse = 0;
static volatile int g_busy = 0;
static int g_blink = 0;
// A program that wants the whole screen turns the console off; the
// prompt keeps running over the serial line while it does.
static int g_enabled = 1;

static void cell_fill(int col, int row, uint8_t colour) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb) return;
    size_t stride = fruitjam_dvi_stride();
    for (int y = 0; y < CH_H; y++)
        memset(fb + (size_t)(row * CH_H + y) * stride + (size_t)col * CELL_BYTES,
               colour, CELL_BYTES);
}

// Glyphs the font has no byte for, reached through their code point:
// the German letters, the section and degree signs and the euro. In the
// style of the C64 face, two dots over the base letter.
struct extra_glyph { uint32_t cp; uint8_t rows[8]; };
static const struct extra_glyph g_extra[] = {
    { 0x00E4, { 0x66, 0x00, 0x3c, 0x06, 0x3e, 0x66, 0x3e, 0x00 } },   // ae
    { 0x00F6, { 0x66, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x3c, 0x00 } },   // oe
    { 0x00FC, { 0x66, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3e, 0x00 } },   // ue
    { 0x00C4, { 0x66, 0x00, 0x18, 0x3c, 0x66, 0x7e, 0x66, 0x00 } },   // AE
    { 0x00D6, { 0x66, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x3c, 0x00 } },   // OE
    { 0x00DC, { 0x66, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00 } },   // UE
    { 0x00DF, { 0x3c, 0x66, 0x66, 0x6c, 0x66, 0x66, 0x6c, 0x60 } },   // sharp s
    { 0x00A7, { 0x3c, 0x60, 0x3c, 0x66, 0x3c, 0x06, 0x3c, 0x00 } },   // section
    { 0x00B0, { 0x18, 0x24, 0x24, 0x18, 0x00, 0x00, 0x00, 0x00 } },   // degree
    { 0x00B4, { 0x0c, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 } },   // acute
    { 0x20AC, { 0x1c, 0x30, 0x7c, 0x30, 0x7c, 0x30, 0x1c, 0x00 } },   // euro
};
static const uint8_t g_unknown_glyph[8] = { 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x00 };

static const uint8_t* glyph_for_byte(unsigned char c) {
    if (c < 32) c = 32;
    return &jdos_font8x8_c64[(c - 32) * 8];
}

static const uint8_t* glyph_for_cp(uint32_t cp) {
    for (size_t i = 0; i < sizeof g_extra / sizeof g_extra[0]; i++)
        if (g_extra[i].cp == cp) return g_extra[i].rows;
    return g_unknown_glyph;
}

static void cell_draw(int col, int row, const uint8_t* gl) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb) return;
    size_t stride = fruitjam_dvi_stride();
    const uint8_t ink   = g_reverse ? g_bg : g_fg;
    const uint8_t paper = g_reverse ? g_fg : g_bg;
    for (int y = 0; y < CH_H; y++) {
        uint8_t bits = gl[y];
        uint8_t* p = fb + (size_t)(row * CH_H + y) * stride + (size_t)col * CELL_BYTES;
        for (int x = 0; x < CH_W; x++) {
            uint8_t v = (bits & (0x80 >> x)) ? ink : paper;
            *p++ = v;
            *p++ = v;
        }
    }
}

// In RAM, and so is everything it reaches: the frame interrupt calls
// this, and the frame interrupt has to finish while the flash is being
// erased. Code fetched from flash stalls until the erase is over, which
// is fifty milliseconds - three frames the monitor does not get, and
// enough for it to let go of the signal and go black for a second. The
// byte loop is here for the same reason: it does not matter where the
// library keeps its memset.
static void __not_in_flash_func(cursor_draw)(int on) {
    uint8_t* fb = fruitjam_dvi_framebuffer();
    if (!fb || g_col >= COLS || g_row >= ROWS) return;
    size_t stride = fruitjam_dvi_stride();
    // An underline, so it marks the place without hiding the character.
    uint8_t* p = fb + (size_t)(g_row * CH_H + CH_H - 1) * stride
                    + (size_t)g_col * CELL_BYTES;
    uint8_t v = on ? g_fg : g_bg;
    for (int i = 0; i < CELL_BYTES; i++) p[i] = v;
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

static void put_glyph(const uint8_t* gl) {
    if (g_col >= COLS) newline();
    cell_draw(g_col, g_row, gl);
    g_col++;
}

// Text arrives as UTF-8. A two or three byte sequence is one code point
// and one cell; any other byte above 127 is a glyph of the font by
// number, which is how the graphics characters are reached.
static uint32_t      g_utf_cp = 0;
static int           g_utf_need = 0;
static unsigned char g_utf_lead = 0;

static void put_printable(char ch) {
    unsigned char c = (unsigned char)ch;
    if (g_utf_need > 0) {
        if ((c & 0xC0) == 0x80) {
            g_utf_cp = (g_utf_cp << 6) | (c & 0x3F);
            if (--g_utf_need == 0) put_glyph(glyph_for_cp(g_utf_cp));
            return;
        }
        put_glyph(glyph_for_byte(g_utf_lead));
        g_utf_need = 0;
    }
    if (c >= 0xC2 && c <= 0xDF) { g_utf_need = 1; g_utf_cp = c & 0x1F; g_utf_lead = c; return; }
    if (c >= 0xE0 && c <= 0xEF) { g_utf_need = 2; g_utf_cp = c & 0x0F; g_utf_lead = c; return; }
    put_glyph(glyph_for_byte(c));
}

// The foreground colours, dull and bright, mapped onto the three bits
// of red, three of green and two of blue this framebuffer has. Blue is
// the one that suffers: two bits is all there is, so it is lifted with
// a little green to stay visible against the dark ground.
static void sgr(int code) {
    switch (code) {
        case 0:  g_fg = 0x3C; g_reverse = 0; break;
        case 7:  g_reverse = 1; break;
        case 27: g_reverse = 0; break;
        case 30: g_fg = 0x00; break;
        case 31: g_fg = 0xA0; break;
        case 32: g_fg = 0x14; break;
        case 33: g_fg = 0xF0; break;
        case 34: g_fg = 0x07; break;
        case 35: g_fg = 0xA2; break;
        case 36: g_fg = 0x16; break;
        case 37: g_fg = 0xB6; break;
        case 90: g_fg = 0x92; break;
        case 91: g_fg = 0xE0; break;
        case 92: g_fg = 0x1C; break;
        case 93: g_fg = 0xFC; break;
        case 94: g_fg = 0x0F; break;
        case 95: g_fg = 0xE3; break;
        case 96: g_fg = 0x1F; break;
        case 97: g_fg = 0xFF; break;
        default: break;
    }
}

// Semicolon-separated numbers, however many the sequence carried. The
// editor positions its cursor with two of them, which is the whole reason
// this has to be a real parser rather than a single number.
#define MAX_PARAMS 4
static int g_param[MAX_PARAMS];
static int g_params = 0;

static void seq_parse(void) {
    g_params = 0;
    int v = 0, seen = 0;
    for (int i = 0; i <= g_seq_len; i++) {
        char c = (i < g_seq_len) ? g_seq[i] : ';';
        if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); seen = 1; }
        else if (c == ';') {
            if (g_params < MAX_PARAMS) g_param[g_params++] = seen ? v : -1;
            v = 0; seen = 0;
        }
    }
}

// Absent parameters mean "the default", which is 1 for movement and 0 for
// the erase and colour codes.
static int param(int i, int dflt) {
    if (i >= g_params || g_param[i] < 0) return dflt;
    return g_param[i];
}

static void erase_row(int row, int from, int to) {
    for (int c = from; c < to; c++) cell_fill(c, row, g_bg);
}

static void csi_final(char f) {
    seq_parse();
    switch (f) {
        case 'm':
            if (g_params == 0) sgr(0);
            for (int i = 0; i < g_params; i++) sgr(param(i, 0));
            break;
        case 'C': g_col += param(0, 1); if (g_col > COLS) g_col = COLS; break;
        case 'D': g_col -= param(0, 1); if (g_col < 0) g_col = 0; break;
        case 'A': g_row -= param(0, 1); if (g_row < 0) g_row = 0; break;
        case 'B': g_row += param(0, 1); if (g_row >= ROWS) g_row = ROWS - 1; break;
        case 'G': g_col = param(0, 1) - 1; break;
        case 'H':
        case 'f':
            g_row = param(0, 1) - 1;
            g_col = param(1, 1) - 1;
            if (g_row < 0) g_row = 0;
            if (g_col < 0) g_col = 0;
            if (g_row >= ROWS) g_row = ROWS - 1;
            if (g_col > COLS) g_col = COLS;
            break;
        case 'K': {
            int mode = param(0, 0);
            if (mode == 0) erase_row(g_row, g_col, COLS);
            else if (mode == 1) erase_row(g_row, 0, g_col + 1);
            else erase_row(g_row, 0, COLS);
            break;
        }
        case 'J': {
            int mode = param(0, 0);
            if (mode == 2 || mode == 3) {
                for (int r = 0; r < ROWS; r++) erase_row(r, 0, COLS);
                g_col = g_row = 0;
            } else if (mode == 0) {
                erase_row(g_row, g_col, COLS);
                for (int r = g_row + 1; r < ROWS; r++) erase_row(r, 0, COLS);
            } else {
                for (int r = 0; r < g_row; r++) erase_row(r, 0, COLS);
                erase_row(g_row, 0, g_col + 1);
            }
            break;
        }
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
void __not_in_flash_func(fruitjam_con_tick)(void) {
    if (g_busy || !g_enabled) return;
    if (++g_blink < 30) return;
    g_blink = 0;
    g_cursor_on = !g_cursor_on;
    cursor_draw(g_cursor_on);
}

int fruitjam_con_enable(int on) {
    if (!on && g_cursor_on) { cursor_draw(0); g_cursor_on = 0; }
    g_enabled = on ? 1 : 0;
    return g_enabled;
}

int fruitjam_con_on(void) { return g_enabled; }

void fruitjam_con_size(int* cols, int* rows) { *cols = COLS; *rows = ROWS; }

static void con_out_chars(const char* buf, int len) {
    if (g_enabled) fruitjam_con_write(buf, len);
}

static stdio_driver_t con_driver = {
    .out_chars = con_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true,
#endif
};

void fruitjam_con_init(void) { stdio_set_driver_enabled(&con_driver, true); }
