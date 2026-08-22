// The drawing family on the panel: the desktop's scalar shapes, drawn
// straight into display RAM under the text console.

#include <stdint.h>
#include <stdlib.h>

void picocalc_lcd_fill_rect(int x, int y, int w, int h,
                            uint8_t r, uint8_t g, uint8_t b);
void picocalc_lcd_pset(int x, int y, uint8_t r, uint8_t g, uint8_t b);

static uint8_t g_r = 0x30, g_g = 0xFC, g_b = 0x30;

void picocalc_gfx_color(int r, int g, int b) {
    g_r = (uint8_t)r; g_g = (uint8_t)g; g_b = (uint8_t)b;
}

void picocalc_gfx_pset(int x, int y) {
    picocalc_lcd_pset(x, y, g_r, g_g, g_b);
}

void picocalc_gfx_line(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        picocalc_lcd_pset(x1, y1, g_r, g_g, g_b);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void picocalc_gfx_rect(int x, int y, int w, int h, int fill) {
    if (fill) {
        picocalc_lcd_fill_rect(x, y, w, h, g_r, g_g, g_b);
        return;
    }
    picocalc_lcd_fill_rect(x, y, w, 1, g_r, g_g, g_b);
    picocalc_lcd_fill_rect(x, y + h - 1, w, 1, g_r, g_g, g_b);
    picocalc_lcd_fill_rect(x, y, 1, h, g_r, g_g, g_b);
    picocalc_lcd_fill_rect(x + w - 1, y, 1, h, g_r, g_g, g_b);
}

void picocalc_gfx_circle(int cx, int cy, int rad, int fill) {
    int x = rad, y = 0, err = 1 - rad;
    while (x >= y) {
        if (fill) {
            picocalc_lcd_fill_rect(cx - x, cy + y, 2 * x + 1, 1, g_r, g_g, g_b);
            picocalc_lcd_fill_rect(cx - x, cy - y, 2 * x + 1, 1, g_r, g_g, g_b);
            picocalc_lcd_fill_rect(cx - y, cy + x, 2 * y + 1, 1, g_r, g_g, g_b);
            picocalc_lcd_fill_rect(cx - y, cy - x, 2 * y + 1, 1, g_r, g_g, g_b);
        } else {
            picocalc_lcd_pset(cx + x, cy + y, g_r, g_g, g_b);
            picocalc_lcd_pset(cx - x, cy + y, g_r, g_g, g_b);
            picocalc_lcd_pset(cx + x, cy - y, g_r, g_g, g_b);
            picocalc_lcd_pset(cx - x, cy - y, g_r, g_g, g_b);
            picocalc_lcd_pset(cx + y, cy + x, g_r, g_g, g_b);
            picocalc_lcd_pset(cx - y, cy + x, g_r, g_g, g_b);
            picocalc_lcd_pset(cx + y, cy - x, g_r, g_g, g_b);
            picocalc_lcd_pset(cx - y, cy - x, g_r, g_g, g_b);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}
