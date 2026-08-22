// The PicoCalc's screen as a text console: an ILI9488-class panel,
// 320 by 320, on spi1, drawn with the 8x8 C64 font from the old jdos.
// 40 columns by 40 rows, green on black, software scroll.

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
#define COLS         40
#define ROWS         40

// 18-bit interface: every pixel travels as three bytes.
#define FG_R 0x30
#define FG_G 0xFC
#define FG_B 0x30
#define BG_R 0x00
#define BG_G 0x00
#define BG_B 0x00

static char g_text[ROWS][COLS];
static int g_cx = 0, g_cy = 0;

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

// One text row rendered as pixels: 320 wide, 8 tall, 3 bytes a pixel.
static uint8_t g_rowbuf[LCD_W * 8 * 3];

static void draw_row(int row) {
    uint8_t* p = g_rowbuf;
    for (int line = 0; line < 8; line++) {
        for (int col = 0; col < COLS; col++) {
            unsigned ch = (unsigned char)g_text[row][col];
            if (ch < 32 || ch > 151) ch = 32;
            uint8_t bits = jdos_font8x8_c64[(ch - 32) * 8 + line];
            for (int px = 0; px < 8; px++) {
                if (bits & (0x80 >> px)) {
                    *p++ = FG_R; *p++ = FG_G; *p++ = FG_B;
                } else {
                    *p++ = BG_R; *p++ = BG_G; *p++ = BG_B;
                }
            }
        }
    }
    set_window(0, row * 8, LCD_W - 1, row * 8 + 7);
    wr_burst(g_rowbuf, sizeof g_rowbuf);
}

static void clear_screen(void) {
    memset(g_text, ' ', sizeof g_text);
    g_cx = 0; g_cy = 0;
    for (int r = 0; r < ROWS; r++) draw_row(r);
}

static void scroll_up(void) {
    memmove(g_text[0], g_text[1], (ROWS - 1) * COLS);
    memset(g_text[ROWS - 1], ' ', COLS);
    for (int r = 0; r < ROWS; r++) draw_row(r);
}

void picocalc_lcd_putc(char c) {
    if (c == '\r') { g_cx = 0; return; }
    if (c == '\n') {
        g_cx = 0;
        if (++g_cy >= ROWS) { g_cy = ROWS - 1; scroll_up(); }
        return;
    }
    if (c == '\b' || c == 127) {
        if (g_cx > 0) {
            g_cx--;
            g_text[g_cy][g_cx] = ' ';
            draw_row(g_cy);
        }
        return;
    }
    if (c == '\f') { clear_screen(); return; }
    if ((unsigned char)c < 32) return;

    g_text[g_cy][g_cx] = c;
    draw_row(g_cy);
    if (++g_cx >= COLS) {
        g_cx = 0;
        if (++g_cy >= ROWS) { g_cy = ROWS - 1; scroll_up(); }
    }
}

// The panel's init words, as the PicoCalc wants them: gamma, power,
// VCOM, 18-bit pixels, inversion on.
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

    clear_screen();
}
