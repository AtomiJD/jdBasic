// The drawing family on the DVI framebuffer: 320 by 240, one byte per
// pixel, straight into the buffer the scanout reads. There is no flip
// and no dirty box - the picture on the wire is the picture in memory.
//
// Colours are named two ways. A sixteen-entry palette keeps the console
// vocabulary the other boards use, and any RGB triple can be set
// directly; both land in the same RGB332 byte.

#include <stdint.h>
#include <stdlib.h>
#include "picocalc_font.h"

void    fruitjam_dvi_pset(int x, int y, uint8_t rgb332);
void    fruitjam_dvi_hline(int x, int y, int w, uint8_t rgb332);
void    fruitjam_dvi_clear(uint8_t rgb332);
int     fruitjam_dvi_width(void);
int     fruitjam_dvi_height(void);

#define GFX_W 320
#define GFX_H 240

static uint8_t rgb332_of(int r, int g, int b) {
    return (uint8_t)((r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6));
}

static uint8_t g_pal[16] = {
    0x00,  //  0 black
    0x1C,  //  1 green, the console default
    0xFF,  //  2 white
    0xFC,  //  3 yellow
    0x1F,  //  4 cyan
    0x92,  //  5 gray
    0xE0,  //  6 red
    0xE3,  //  7 magenta
    0x03,  //  8 blue
    0xF0,  //  9 orange
    0x64,  // 10 brown
    0x49,  // 11 dark gray
    0xB6,  // 12 light gray
    0x14,  // 13 dark green
    0x80,  // 14 dark red
    0x02,  // 15 dark blue
};

static uint8_t g_ink = 0x1C;

void fruitjam_gfx_palette(int i, int r, int g, int b) {
    if (i < 0 || i > 15) return;
    g_pal[i] = rgb332_of(r, g, b);
}

void fruitjam_gfx_color(int r, int g, int b) { g_ink = rgb332_of(r, g, b); }

void fruitjam_gfx_color_index(int i) {
    if (i >= 0 && i <= 15) g_ink = g_pal[i];
}

void fruitjam_gfx_clear(int index) {
    fruitjam_dvi_clear(g_pal[(index < 0 || index > 15) ? 0 : index]);
}

void fruitjam_gfx_pset(int x, int y) { fruitjam_dvi_pset(x, y, g_ink); }

void fruitjam_gfx_line(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fruitjam_dvi_pset(x1, y1, g_ink);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void fruitjam_gfx_rect(int x, int y, int w, int h, int fill) {
    if (w <= 0 || h <= 0) return;
    if (fill) {
        for (int r = 0; r < h; r++) fruitjam_dvi_hline(x, y + r, w, g_ink);
        return;
    }
    fruitjam_dvi_hline(x, y, w, g_ink);
    fruitjam_dvi_hline(x, y + h - 1, w, g_ink);
    for (int r = 0; r < h; r++) {
        fruitjam_dvi_pset(x, y + r, g_ink);
        fruitjam_dvi_pset(x + w - 1, y + r, g_ink);
    }
}

void fruitjam_gfx_circle(int cx, int cy, int rad, int fill) {
    if (rad < 0) return;
    int x = rad, y = 0, err = 1 - rad;
    while (x >= y) {
        if (fill) {
            fruitjam_dvi_hline(cx - x, cy + y, 2 * x + 1, g_ink);
            fruitjam_dvi_hline(cx - x, cy - y, 2 * x + 1, g_ink);
            fruitjam_dvi_hline(cx - y, cy + x, 2 * y + 1, g_ink);
            fruitjam_dvi_hline(cx - y, cy - x, 2 * y + 1, g_ink);
        } else {
            fruitjam_dvi_pset(cx + x, cy + y, g_ink);
            fruitjam_dvi_pset(cx - x, cy + y, g_ink);
            fruitjam_dvi_pset(cx + x, cy - y, g_ink);
            fruitjam_dvi_pset(cx - x, cy - y, g_ink);
            fruitjam_dvi_pset(cx + y, cy + x, g_ink);
            fruitjam_dvi_pset(cx - y, cy + x, g_ink);
            fruitjam_dvi_pset(cx + y, cy - x, g_ink);
            fruitjam_dvi_pset(cx - y, cy - x, g_ink);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void fruitjam_gfx_text(int x, int y, const char* s, int scale) {
    if (scale < 1) scale = 1;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 32 || c > 151) c = 32;
        const uint8_t* gl = &jdos_font8x8_c64[(c - 32) * 8];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = gl[row];
            for (int col = 0; col < 8; col++) {
                if (!(bits & (0x80 >> col))) continue;
                if (scale == 1) {
                    fruitjam_dvi_pset(x + col, y + row, g_ink);
                } else {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            fruitjam_dvi_pset(x + col * scale + sx,
                                              y + row * scale + sy, g_ink);
                }
            }
        }
        x += 8 * scale;
    }
}
