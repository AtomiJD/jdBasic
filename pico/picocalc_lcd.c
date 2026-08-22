// The PicoCalc's screen as a text console: an ILI9488-class panel,
// 320 by 320, on spi1, drawn with the 8x8 C64 font from the old jdos.
// 40 columns by 40 rows, green on black. Scrolling turns the panel's
// own vertical-scroll registers over a 60-row ring in display RAM, so
// a scroll costs one row, not a repaint. Drawing is lazy: characters
// mark rows dirty and a flush sends them, one SPI burst per row.

#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "picocalc_font.h"

#define LCD_SPI      spi1
#define LCD_SPI_HZ   25000000
#define PIN_SCK      10
#define PIN_MOSI     11
#define PIN_MISO     12
#define PIN_CS       13
#define PIN_DC       14
#define PIN_RST      15

#define LCD_W        320
#define LCD_H        320
#define RAM_H        480
#define COLS         40
#define ROWS         40
#define RING_ROWS    (RAM_H / 8)

// 18-bit interface: every pixel travels as three bytes.
#define FG_R 0x30
#define FG_G 0xFC
#define FG_B 0x30
#define BG_R 0x00
#define BG_G 0x00
#define BG_B 0x00

static char g_text[ROWS][COLS];
static uint8_t g_dirty[ROWS];
static int g_cx = 0, g_cy = 0;
static int g_scroll = 0;          // ring offset in text rows, 0..RING_ROWS-1
static int g_cursor_on = 1;

static void cs(int v)  { gpio_put(PIN_CS, v); }
static void dc(int v)  { gpio_put(PIN_DC, v); }

static void wr_cmd(uint8_t c) {
    dc(0); cs(0);
    spi_write_blocking(LCD_SPI, &c, 1);
    cs(1);
}

static void wr_data(uint8_t d) {
    dc(1); cs(0);
    spi_write_blocking(LCD_SPI, &d, 1);
    cs(1);
}

static void wr_burst(const uint8_t* buf, size_t n) {
    dc(1); cs(0);
    spi_write_blocking(LCD_SPI, buf, n);
    cs(1);
}

static void set_window(int x0, int y0, int x1, int y1) {
    uint8_t b[4];
    wr_cmd(0x2A);
    b[0] = x0 >> 8; b[1] = x0 & 0xFF; b[2] = x1 >> 8; b[3] = x1 & 0xFF;
    wr_burst(b, 4);
    wr_cmd(0x2B);
    b[0] = y0 >> 8; b[1] = y0 & 0xFF; b[2] = y1 >> 8; b[3] = y1 & 0xFF;
    wr_burst(b, 4);
    wr_cmd(0x2C);
}

static void set_scroll(int px) {
    uint8_t b[2] = { (uint8_t)(px >> 8), (uint8_t)(px & 0xFF) };
    wr_cmd(0x37);
    wr_burst(b, 2);
}

// One text row rendered as pixels: 320 wide, 8 tall, 3 bytes a pixel.
static uint8_t g_rowbuf[LCD_W * 8 * 3];

static void draw_row(int row) {
    uint8_t* p = g_rowbuf;
    for (int line = 0; line < 8; line++) {
        for (int col = 0; col < COLS; col++) {
            unsigned ch = (unsigned char)g_text[row][col];
            if (ch < 32 || ch > 151) ch = 32;
            uint8_t bits = jdos_font8x8_c64[(ch - 32) * 8 + line];
            int inv = g_cursor_on && row == g_cy && col == g_cx;
            for (int px = 0; px < 8; px++) {
                int on = (bits & (0x80 >> px)) != 0;
                if (inv) on = !on;
                if (on) {
                    *p++ = FG_R; *p++ = FG_G; *p++ = FG_B;
                } else {
                    *p++ = BG_R; *p++ = BG_G; *p++ = BG_B;
                }
            }
        }
    }
    int ram_row = (g_scroll + row) % RING_ROWS;
    set_window(0, ram_row * 8, LCD_W - 1, ram_row * 8 + 7);
    wr_burst(g_rowbuf, sizeof g_rowbuf);
}

void picocalc_lcd_flush(void) {
    for (int r = 0; r < ROWS; r++) {
        if (g_dirty[r]) {
            draw_row(r);
            g_dirty[r] = 0;
        }
    }
}

static void clear_screen(void) {
    memset(g_text, ' ', sizeof g_text);
    memset(g_dirty, 1, sizeof g_dirty);
    g_cx = 0; g_cy = 0;
    picocalc_lcd_flush();
}

static void scroll_up(void) {
    memmove(g_text[0], g_text[1], (ROWS - 1) * COLS);
    memset(g_text[ROWS - 1], ' ', COLS);
    // The dirty flags ride along with their text: a row written but not
    // yet flushed keeps its claim at its new place, or the ring shows
    // whatever it held twenty scrolls ago.
    memmove(g_dirty, g_dirty + 1, ROWS - 1);
    g_scroll = (g_scroll + 1) % RING_ROWS;
    set_scroll((g_scroll * 8) % RAM_H);
    g_dirty[ROWS - 1] = 1;
}

// A small ANSI escape parser: cursor position (H and f), clear screen
// (2J), clear to end of line (K). Enough for a full-screen editor to
// speak the same stream to this panel and to a USB terminal.
static int g_esc = 0;          // 0 plain, 1 after ESC, 2 in CSI
static int g_par[4];
static int g_parn = 0;

static int ansi_step(char c) {
    if (g_esc == 0) {
        if (c == 0x1B) { g_esc = 1; return 1; }
        return 0;
    }
    if (g_esc == 1) {
        if (c == '[') {
            g_esc = 2;
            g_parn = 0;
            memset(g_par, 0, sizeof g_par);
            return 1;
        }
        g_esc = 0;
        return 1;
    }
    // inside CSI
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
        for (int x = g_cx; x < COLS; x++) g_text[g_cy][x] = ' ';
        g_dirty[g_cy] = 1;
    }
    return 1;
}

void picocalc_lcd_putc(char c) {
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
            g_dirty[g_cy] = 1;
        }
        return;
    }
    if (c == '\f') { clear_screen(); return; }
    if ((unsigned char)c < 32) return;

    g_text[g_cy][g_cx] = c;
    g_dirty[g_cy] = 1;
    if (++g_cx >= COLS) {
        g_cx = 0;
        if (++g_cy >= ROWS) { g_cy = ROWS - 1; scroll_up(); }
    }
    if (g_cy != prev_cy) g_dirty[prev_cy] = 1;
}

// The panel's init words, as the PicoCalc wants them: gamma, power,
// VCOM, 18-bit pixels, inversion on, and the full RAM height declared
// as the scroll area.
void picocalc_lcd_init(void) {
    spi_init(LCD_SPI, LCD_SPI_HZ);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);  gpio_set_dir(PIN_CS, 1);  cs(1);
    gpio_init(PIN_DC);  gpio_set_dir(PIN_DC, 1);  dc(1);
    gpio_init(PIN_RST); gpio_set_dir(PIN_RST, 1);

    gpio_put(PIN_RST, 1); sleep_ms(10);
    gpio_put(PIN_RST, 0); sleep_ms(20);
    gpio_put(PIN_RST, 1); sleep_ms(120);

    static const uint8_t pgam[] = {0x00,0x03,0x09,0x08,0x16,0x0A,0x3F,0x78,0x4C,0x09,0x0A,0x08,0x16,0x1A,0x0F};
    static const uint8_t ngam[] = {0x00,0x16,0x19,0x03,0x0F,0x05,0x32,0x45,0x46,0x04,0x0E,0x0D,0x35,0x37,0x0F};

    wr_cmd(0xE0); wr_burst(pgam, sizeof pgam);
    wr_cmd(0xE1); wr_burst(ngam, sizeof ngam);
    wr_cmd(0xC0); wr_data(0x17); wr_data(0x15);
    wr_cmd(0xC1); wr_data(0x41);
    wr_cmd(0xC5); wr_data(0x00); wr_data(0x12); wr_data(0x80);
    wr_cmd(0x36); wr_data(0x48);
    wr_cmd(0x3A); wr_data(0x66);
    wr_cmd(0xB0); wr_data(0x00);
    wr_cmd(0xB1); wr_data(0xA0);
    wr_cmd(0x21);
    wr_cmd(0xB4); wr_data(0x02);
    wr_cmd(0xB6); wr_data(0x02); wr_data(0x02); wr_data(0x3B);
    wr_cmd(0xB7); wr_data(0xC6);
    wr_cmd(0xE9); wr_data(0x00);
    wr_cmd(0xF7); wr_data(0xA9); wr_data(0x51); wr_data(0x2C); wr_data(0x82);
    wr_cmd(0x11); sleep_ms(120);
    wr_cmd(0x29); sleep_ms(20);

    // Vertical scroll over the whole RAM: no fixed bands.
    wr_cmd(0x33);
    {
        uint8_t b[6] = {0, 0, (uint8_t)(RAM_H >> 8), (uint8_t)(RAM_H & 0xFF), 0, 0};
        wr_burst(b, 6);
    }
    set_scroll(0);

    // The ring behind the visible screen starts known-black.
    memset(g_rowbuf, 0, sizeof g_rowbuf);
    for (int r = 0; r < RING_ROWS; r++) {
        set_window(0, r * 8, LCD_W - 1, r * 8 + 7);
        wr_burst(g_rowbuf, sizeof g_rowbuf);
    }

    clear_screen();
}

// One text row as characters, for diagnosis from the prompt.
void picocalc_lcd_row(int row, char* out, int cap) {
    if (row < 0 || row >= ROWS || cap < COLS + 1) { if (cap > 0) out[0] = 0; return; }
    memcpy(out, g_text[row], COLS);
    out[COLS] = 0;
}

// Scroll ring and cursor state, for diagnosis from the prompt.
void picocalc_lcd_stat(int* scroll, int* cx, int* cy) {
    *scroll = g_scroll;
    *cx = g_cx;
    *cy = g_cy;
}
