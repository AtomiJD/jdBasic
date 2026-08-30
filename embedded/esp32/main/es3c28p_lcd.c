// The 2.8 inch panel of the ES3C28P: an ILI9341V, 240 by 320, on FSPI,
// turned to landscape and drawn out of a framebuffer that lives in
// PSRAM. The panel's reset line is tied to the chip's own EN pin, so
// the only reset available from software is command 0x01.
//
// The PicoCalc draws straight into display RAM and scrolls with the
// panel's own registers, because 264 KB of SRAM has no room for a
// frame. Here 8 MB does: 320 by 240 at two bytes is 150 KB, so every
// primitive writes memory and one flush sends it. No scroll ring, no
// dirty rows.
//
// The bytes reach the panel through esp_lcd's SPI panel-IO layer rather
// than through spi_master directly. That layer carries the data/command
// line inside the transaction instead of as a GPIO written beside it,
// and it owns chip select, which is the part a hand-rolled transport
// gets wrong in ways that report success and show nothing.

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "../../pico/picocalc_font.h"

#define PIN_SCLK    12
#define PIN_MOSI    11
#define PIN_MISO    13
#define PIN_CS      10
#define PIN_DC      46
#define PIN_BL      45

#define LCD_W       320
#define LCD_H       240

// The datasheet gives the serial clock as 100 ns minimum for a write,
// which is 10 MHz.
#define SPI_HZ_WRITE (10 * 1000 * 1000)

// One transfer is a band of rows staged in internal, DMA-capable memory.
#define BAND_ROWS   16

static esp_lcd_panel_io_handle_t g_io;
static uint16_t* g_fb;                 // PSRAM, stored in the panel's byte order
static uint16_t* g_band;               // internal, DMA-capable
static SemaphoreHandle_t g_band_free;
static uint16_t g_color = 0xE0FF;      // white, byte-swapped
static int g_ready = 0;

// What the transport actually did. A driver that throws its return codes
// away cannot tell "the panel ignored us" from "nothing was ever sent",
// which is exactly the question that costs an evening.
static int g_tx_n = 0;
static int g_tx_fail = 0;
static int g_last_err = 0;

void es3c28p_lcd_diag(int* sent, int* failed, int* last) {
    *sent = g_tx_n; *failed = g_tx_fail; *last = g_last_err;
}

static bool band_done(esp_lcd_panel_io_handle_t io,
                      esp_lcd_panel_io_event_data_t* ev, void* ctx) {
    (void)io; (void)ev; (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(g_band_free, &woken);
    return woken == pdTRUE;
}

static void note(esp_err_t e) {
    g_tx_n++;
    if (e != ESP_OK) { g_tx_fail++; g_last_err = (int)e; }
}

static void cmd(int c) {
    note(esp_lcd_panel_io_tx_param(g_io, c, NULL, 0));
}

static void cmd_p(int c, const uint8_t* p, size_t n) {
    note(esp_lcd_panel_io_tx_param(g_io, c, p, n));
}

static void cmd_p1(int c, uint8_t p) {
    note(esp_lcd_panel_io_tx_param(g_io, c, &p, 1));
}

// The panel wants each pixel most significant byte first, and the fast
// path for a flush is to hand the buffer over as it stands. So the
// framebuffer holds the bytes the way the wire wants them and every
// colour is swapped once, here.
static uint16_t rgb565_be(int r, int g, int b) {
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

void es3c28p_lcd_backlight(int on) {
    gpio_set_level((gpio_num_t)PIN_BL, on ? 1 : 0);
}

int es3c28p_lcd_ready(void) { return g_ready; }
int es3c28p_lcd_width(void)  { return LCD_W; }
int es3c28p_lcd_height(void) { return LCD_H; }

// Which pins the panel is holding, so the GPIO verbs can refuse them
// while it is running rather than blank the screen mid-program.
int es3c28p_lcd_uses_pin(int pin) {
    if (!g_ready) return 0;
    return pin == PIN_SCLK || pin == PIN_MOSI || pin == PIN_MISO ||
           pin == PIN_CS   || pin == PIN_DC   || pin == PIN_BL;
}

// The frame cannot go out in one piece: handing the SPI driver a buffer
// in PSRAM makes it ask for a bounce buffer of the same size in internal
// memory, and 150 KB of that does not exist here. So the frame goes as
// bands staged in internal memory - the first as a memory write, the
// rest as memory-write-continue, which is what that command is for.
//
// One staging buffer means waiting for each band before refilling it.
// The transfer is queued, not synchronous, so the wait is the callback.
// A band of rows and nothing else. The console redraws a text line as
// eight pixel rows, which is 5 KB; sending the whole frame for one
// character would be thirty times that.
void es3c28p_lcd_blit_rows(int y0, int rows) {
    if (!g_ready || rows <= 0) return;
    if (y0 < 0) { rows += y0; y0 = 0; }
    if (y0 + rows > LCD_H) rows = LCD_H - y0;
    if (rows <= 0) return;

    uint8_t w[4];
    w[0] = 0; w[1] = 0; w[2] = (LCD_W - 1) >> 8; w[3] = (LCD_W - 1) & 0xFF;
    cmd_p(0x2A, w, 4);
    w[0] = y0 >> 8; w[1] = y0 & 0xFF;
    w[2] = (y0 + rows - 1) >> 8; w[3] = (y0 + rows - 1) & 0xFF;
    cmd_p(0x2B, w, 4);

    for (int y = 0; y < rows; y += BAND_ROWS) {
        int n = (y + BAND_ROWS <= rows) ? BAND_ROWS : (rows - y);
        size_t bytes = (size_t)n * LCD_W * 2;
        memcpy(g_band, g_fb + (size_t)(y0 + y) * LCD_W, bytes);
        esp_err_t e = esp_lcd_panel_io_tx_color(g_io, y == 0 ? 0x2C : 0x3C,
                                                g_band, bytes);
        note(e);
        if (e != ESP_OK) return;
        xSemaphoreTake(g_band_free, portMAX_DELAY);
    }
}

// Where a pixel row lives, for a caller that draws into the frame and
// then asks for just that part to be sent.
uint16_t* es3c28p_lcd_row(int y) {
    if (!g_ready || y < 0 || y >= LCD_H) return NULL;
    return g_fb + (size_t)y * LCD_W;
}

uint16_t es3c28p_lcd_encode(int r, int g, int b) { return rgb565_be(r, g, b); }

void es3c28p_lcd_flip(void) {
    if (!g_ready) return;
    uint8_t w[4];
    w[0] = 0; w[1] = 0; w[2] = (LCD_W - 1) >> 8; w[3] = (LCD_W - 1) & 0xFF;
    cmd_p(0x2A, w, 4);
    w[0] = 0; w[1] = 0; w[2] = (LCD_H - 1) >> 8; w[3] = (LCD_H - 1) & 0xFF;
    cmd_p(0x2B, w, 4);

    for (int y = 0; y < LCD_H; y += BAND_ROWS) {
        int rows = (y + BAND_ROWS <= LCD_H) ? BAND_ROWS : (LCD_H - y);
        size_t bytes = (size_t)rows * LCD_W * 2;
        memcpy(g_band, g_fb + (size_t)y * LCD_W, bytes);
        esp_err_t e = esp_lcd_panel_io_tx_color(g_io, y == 0 ? 0x2C : 0x3C,
                                                g_band, bytes);
        note(e);
        if (e != ESP_OK) return;
        xSemaphoreTake(g_band_free, portMAX_DELAY);
    }
}

int es3c28p_lcd_init(void) {
    if (g_ready) return 0;

    g_fb = (uint16_t*)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
    if (!g_fb) return -1;
    memset(g_fb, 0, LCD_W * LCD_H * 2);
    g_band = (uint16_t*)heap_caps_malloc(LCD_W * BAND_ROWS * 2,
                                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!g_band) { heap_caps_free(g_fb); g_fb = NULL; return -2; }
    g_band_free = xSemaphoreCreateBinary();
    if (!g_band_free) return -6;

    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << PIN_BL);
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)PIN_BL, 0);

    spi_bus_config_t bus = {0};
    bus.mosi_io_num = PIN_MOSI;
    bus.miso_io_num = PIN_MISO;
    bus.sclk_io_num = PIN_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = LCD_W * BAND_ROWS * 2;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return -3;

    esp_lcd_panel_io_spi_config_t iocfg = {0};
    iocfg.cs_gpio_num = PIN_CS;
    iocfg.dc_gpio_num = PIN_DC;
    iocfg.spi_mode = 0;
    iocfg.pclk_hz = SPI_HZ_WRITE;
    iocfg.trans_queue_depth = 4;
    iocfg.lcd_cmd_bits = 8;
    iocfg.lcd_param_bits = 8;
    iocfg.on_color_trans_done = band_done;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                 &iocfg, &g_io) != ESP_OK) return -4;

    // No reset pin of its own: it hangs on EN, so the chip and the panel
    // come out of reset together and this is the only one left.
    cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    { uint8_t d[] = {0x00,0xC1,0x30}; cmd_p(0xCF, d, 3); }
    { uint8_t d[] = {0x64,0x03,0x12,0x81}; cmd_p(0xED, d, 4); }
    { uint8_t d[] = {0x85,0x00,0x78}; cmd_p(0xE8, d, 3); }
    { uint8_t d[] = {0x39,0x2C,0x00,0x34,0x02}; cmd_p(0xCB, d, 5); }
    cmd_p1(0xF7, 0x20);
    { uint8_t d[] = {0x00,0x00}; cmd_p(0xEA, d, 2); }
    cmd_p1(0xC0, 0x23);
    cmd_p1(0xC1, 0x10);
    { uint8_t d[] = {0x3E,0x28}; cmd_p(0xC5, d, 2); }
    cmd_p1(0xC7, 0x86);
    // MV for the long edge across, which is how every other jdBasic screen
    // is shaped, and BGR because the panel is wired that way: sending red
    // without it comes out blue.
    cmd_p1(0x36, 0x28);
    cmd_p1(0x3A, 0x55);
    { uint8_t d[] = {0x00,0x18}; cmd_p(0xB1, d, 2); }
    { uint8_t d[] = {0x08,0x82,0x27}; cmd_p(0xB6, d, 3); }
    cmd_p1(0xF2, 0x00);
    cmd_p1(0x26, 0x01);
    { uint8_t d[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                     0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
      cmd_p(0xE0, d, 15); }
    { uint8_t d[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                     0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
      cmd_p(0xE1, d, 15); }
    // Inversion on. That reads backwards until you remember this is an IPS
    // panel: with it off, black comes out white. It is also a state the
    // panel keeps across a warm restart, so it gets set rather than
    // inherited.
    cmd(0x21);
    cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    g_ready = 1;
    es3c28p_lcd_flip();
    es3c28p_lcd_backlight(1);
    return 0;
}

void es3c28p_lcd_color(int r, int g, int b) { g_color = rgb565_be(r, g, b); }

void es3c28p_lcd_pset(int x, int y) {
    if (!g_ready || x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
    g_fb[(size_t)y * LCD_W + x] = g_color;
}

// What a pixel holds, in the caller's colour order rather than the
// wire's. This reads the framebuffer, not the panel.
void es3c28p_lcd_peek(int x, int y, int* r, int* g, int* b) {
    *r = *g = *b = 0;
    if (!g_ready || x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return;
    uint16_t v = g_fb[(size_t)y * LCD_W + x];
    v = (uint16_t)((v >> 8) | (v << 8));
    *r = ((v >> 11) & 0x1F) << 3;
    *g = ((v >> 5)  & 0x3F) << 2;
    *b = (v & 0x1F) << 3;
}

static void hspan(int x, int y, int w) {
    if (y < 0 || y >= LCD_H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (w <= 0) return;
    uint16_t* p = g_fb + (size_t)y * LCD_W + x;
    for (int i = 0; i < w; i++) p[i] = g_color;
}

void es3c28p_lcd_rect(int x, int y, int w, int h, int fill) {
    if (!g_ready || w <= 0 || h <= 0) return;
    if (fill) {
        for (int i = 0; i < h; i++) hspan(x, y + i, w);
        return;
    }
    hspan(x, y, w);
    hspan(x, y + h - 1, w);
    for (int i = 0; i < h; i++) {
        es3c28p_lcd_pset(x, y + i);
        es3c28p_lcd_pset(x + w - 1, y + i);
    }
}

void es3c28p_lcd_clear(void) {
    if (!g_ready) return;
    uint16_t* p = g_fb;
    for (size_t i = 0; i < (size_t)LCD_W * LCD_H; i++) p[i] = g_color;
}

void es3c28p_lcd_line(int x1, int y1, int x2, int y2) {
    if (!g_ready) return;
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        es3c28p_lcd_pset(x1, y1);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void es3c28p_lcd_circle(int cx, int cy, int rad, int fill) {
    if (!g_ready || rad < 0) return;
    int x = rad, y = 0, err = 1 - rad;
    while (x >= y) {
        if (fill) {
            hspan(cx - x, cy + y, 2 * x + 1);
            hspan(cx - x, cy - y, 2 * x + 1);
            hspan(cx - y, cy + x, 2 * y + 1);
            hspan(cx - y, cy - x, 2 * y + 1);
        } else {
            es3c28p_lcd_pset(cx + x, cy + y); es3c28p_lcd_pset(cx - x, cy + y);
            es3c28p_lcd_pset(cx + x, cy - y); es3c28p_lcd_pset(cx - x, cy - y);
            es3c28p_lcd_pset(cx + y, cy + x); es3c28p_lcd_pset(cx - y, cy + x);
            es3c28p_lcd_pset(cx + y, cy - x); es3c28p_lcd_pset(cx - y, cy - x);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

// The same 8x8 the PicoCalc console draws with, so a listing looks the
// same on either board.
void es3c28p_lcd_text(int x, int y, const char* s, int scale) {
    if (!g_ready || !s) return;
    if (scale < 1) scale = 1;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned ch = *p;
        if (ch < 32 || ch > 151) ch = 32;
        for (int line = 0; line < 8; line++) {
            uint8_t bits = jdos_font8x8_c64[(ch - 32) * 8 + line];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) continue;
                if (scale == 1) {
                    es3c28p_lcd_pset(x + col, y + line);
                } else {
                    for (int sy = 0; sy < scale; sy++)
                        hspan(x + col * scale, y + line * scale + sy, scale);
                }
            }
        }
        x += 8 * scale;
        if (x >= LCD_W) return;
    }
}

// Raw bytes from any read command, for finding out what the wire is
// actually carrying. esp_lcd sends the command and reads straight after
// it with no dummy clock, and the ILI9341 asks for one, so what arrives
// may be the truth shifted by a bit - which is worth looking at rather
// than guessing about.
int es3c28p_lcd_reg(int cmd, uint8_t* out, int n) {
    if (!g_ready) return -1;
    if (n < 1) return -3;
    if (n > 8) return -3;
    memset(out, 0, n);
    esp_err_t e = esp_lcd_panel_io_rx_param(g_io, cmd, out, n);
    note(e);
    return e == ESP_OK ? 0 : -2;
}

// The same, after pointing the panel at one pixel, so a memory read can
// be looked at raw while the answer's shape is still being worked out.
int es3c28p_lcd_reg_at(int cmd, int x, int y, uint8_t* out, int n) {
    if (!g_ready) return -1;
    if (n < 1) return -3;
    if (n > 8) return -3;
    uint8_t w[4];
    w[0] = x >> 8; w[1] = x & 0xFF; w[2] = x >> 8; w[3] = x & 0xFF;
    cmd_p(0x2A, w, 4);
    w[0] = y >> 8; w[1] = y & 0xFF; w[2] = y >> 8; w[3] = y & 0xFF;
    cmd_p(0x2B, w, 4);
    return es3c28p_lcd_reg(cmd, out, n);
}

// What the panel says about itself. Not its identity: the ID registers
// (0x04 and 0xD3) read back as zeros on this one, so a check against
// 0x00 0x93 0x41 would fail on a perfectly good panel. The power mode at
// 0x0A does answer, and answers something checkable - a live display
// reports 0x9C, which is booster on, sleep out, normal mode, display on.
int es3c28p_lcd_panel_state(int* mode, int* awake, int* displaying) {
    uint8_t rx[2] = {0, 0};
    *mode = *awake = *displaying = -1;
    if (!g_ready) return -1;
    esp_err_t e = esp_lcd_panel_io_rx_param(g_io, 0x0A, rx, 2);
    note(e);
    if (e != ESP_OK) return -2;
    *mode = rx[0];
    *awake = (rx[0] & 0x10) ? 1 : 0;
    *displaying = (rx[0] & 0x04) ? 1 : 0;
    return 0;
}

// One pixel out of the panel's own memory, so a test can ask the glass
// what it is showing instead of asking the framebuffer what it was told.
int es3c28p_lcd_readback(int x, int y, int* r, int* g, int* b) {
    *r = *g = *b = -1;
    if (!g_ready || x < 0 || y < 0 || x >= LCD_W || y >= LCD_H) return -1;

    uint8_t w[4];
    w[0] = x >> 8; w[1] = x & 0xFF; w[2] = x >> 8; w[3] = x & 0xFF;
    cmd_p(0x2A, w, 4);
    w[0] = y >> 8; w[1] = y & 0xFF; w[2] = y >> 8; w[3] = y & 0xFF;
    cmd_p(0x2B, w, 4);

    // One dummy byte, then six bits a channel left-aligned in a byte.
    // Measured against five known screens: red answers 0, 252, 0, 0 and
    // blue answers 0, 0, 0, 252, so the dummy is real and the order is
    // plain RGB regardless of the BGR bit in MADCTL.
    uint8_t rx[4] = {0, 0, 0, 0};
    esp_err_t e = esp_lcd_panel_io_rx_param(g_io, 0x2E, rx, 4);
    note(e);
    if (e != ESP_OK) return -2;
    *r = rx[1]; *g = rx[2]; *b = rx[3];
    return 0;
}
