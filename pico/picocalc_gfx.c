// The drawing family on the panel. Two modes share one set of shapes:
// straight into display RAM, or through an offscreen buffer that a flip
// sends across in one go.
//
// The buffer covers a rectangle, not the whole screen, because there is
// no room for the whole screen: the interpreter has already taken most
// of the RAM by the time a program runs, leaving around 56 KB. Four
// bits per pixel against a sixteen-entry palette, so a 160 by 160 patch
// costs 12 KB. Ask SYS.FREE before reaching for a big one.
//
// The panel wants three bytes per pixel over SPI, so a flip sends only
// the part of the buffer that changed, addressed once for the whole
// run. That is what makes animation possible here.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "picocalc_font.h"

void picocalc_lcd_fill_rect(int x, int y, int w, int h,
                            uint8_t r, uint8_t g, uint8_t b);
void picocalc_lcd_pset(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void picocalc_lcd_blit_begin(int x, int ram_y, int w, int h);
void picocalc_lcd_blit_row(const uint8_t* rgb3, int w);
void picocalc_lcd_blit_end(void);
int  picocalc_lcd_ram_row(int y);
int  picocalc_lcd_ram_height(void);

#define SCR_W 320
#define SCR_H 320

static uint8_t* g_fb = NULL;
static int g_bx, g_by, g_bw, g_bh;       // the buffered rectangle on screen
static int g_dx0, g_dy0, g_dx1, g_dy1;   // dirty box in buffer coords, x1/y1 exclusive

static uint8_t g_pal[16][3] = {
    { 0x00, 0x00, 0x00 },   //  0 black
    { 0x30, 0xFC, 0x30 },   //  1 green, the console default
    { 0xF8, 0xF8, 0xF8 },   //  2 white
    { 0xF8, 0xE8, 0x40 },   //  3 yellow
    { 0x50, 0xD8, 0xF8 },   //  4 cyan
    { 0x90, 0x90, 0x90 },   //  5 gray
    { 0xF8, 0x50, 0x50 },   //  6 red
    { 0xE0, 0x60, 0xE0 },   //  7 magenta
    { 0x60, 0x80, 0xF8 },   //  8 blue
    { 0xF8, 0x90, 0x30 },   //  9 orange
    { 0x60, 0x40, 0x20 },   // 10 brown
    { 0x40, 0x40, 0x40 },   // 11 dark gray
    { 0xC0, 0xC0, 0xC0 },   // 12 light gray
    { 0x30, 0x80, 0x30 },   // 13 dark green
    { 0x80, 0x30, 0x30 },   // 14 dark red
    { 0x30, 0x30, 0x80 },   // 15 dark blue
};

static uint8_t g_r = 0x30, g_g = 0xFC, g_b = 0x30;
static int g_index = 1;

static void dirty_reset(void) { g_dx0 = g_bw; g_dy0 = g_bh; g_dx1 = 0; g_dy1 = 0; }

// The palette entry closest to a colour, so DRAWCOLOR keeps meaning the
// same thing in both modes.
static int nearest_index(uint8_t r, uint8_t g, uint8_t b) {
    int best = 0;
    long best_d = 1L << 30;
    for (int i = 0; i < 16; i++) {
        long dr = (long)r - g_pal[i][0];
        long dg = (long)g - g_pal[i][1];
        long db = (long)b - g_pal[i][2];
        long d = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// Returns the bytes taken, 0 when switched off, -1 when there was not
// enough room.
int picocalc_gfx_buffer(int x, int y, int w, int h) {
    if (g_fb) { free(g_fb); g_fb = NULL; }
    if (w <= 0 || h <= 0) return 0;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return 0;

    size_t bytes = ((size_t)w * h + 1) / 2;
    g_fb = (uint8_t*)calloc(1, bytes);
    if (!g_fb) return -1;
    g_bx = x; g_by = y; g_bw = w; g_bh = h;
    dirty_reset();
    return (int)bytes;
}

int picocalc_gfx_buffered(void) { return g_fb != NULL; }

void picocalc_gfx_palette(int i, int r, int g, int b) {
    if (i < 0 || i > 15) return;
    g_pal[i][0] = (uint8_t)r;
    g_pal[i][1] = (uint8_t)g;
    g_pal[i][2] = (uint8_t)b;
    g_index = nearest_index(g_r, g_g, g_b);
}

void picocalc_gfx_color(int r, int g, int b) {
    g_r = (uint8_t)r; g_g = (uint8_t)g; g_b = (uint8_t)b;
    g_index = nearest_index(g_r, g_g, g_b);
}

// Screen coordinates throughout: a point inside the buffered rectangle
// lands in the buffer, anything else goes straight to the panel.
void picocalc_gfx_pset(int x, int y) {
    if (g_fb) {
        int bxp = x - g_bx, byp = y - g_by;
        if (bxp >= 0 && byp >= 0 && bxp < g_bw && byp < g_bh) {
            size_t o = ((size_t)byp * g_bw + bxp) >> 1;
            if (bxp & 1) g_fb[o] = (uint8_t)((g_fb[o] & 0xF0) | g_index);
            else         g_fb[o] = (uint8_t)((g_fb[o] & 0x0F) | (g_index << 4));
            if (bxp < g_dx0) g_dx0 = bxp;
            if (byp < g_dy0) g_dy0 = byp;
            if (bxp + 1 > g_dx1) g_dx1 = bxp + 1;
            if (byp + 1 > g_dy1) g_dy1 = byp + 1;
            return;
        }
    }
    picocalc_lcd_pset(x, y, g_r, g_g, g_b);
}

static void span_fill(int x, int y, int w) {
    if (!g_fb) { picocalc_lcd_fill_rect(x, y, w, 1, g_r, g_g, g_b); return; }
    for (int i = 0; i < w; i++) picocalc_gfx_pset(x + i, y);
}

void picocalc_gfx_clear(int index) {
    if (index < 0 || index > 15) index = 0;
    if (!g_fb) {
        picocalc_lcd_fill_rect(0, 0, SCR_W, SCR_H,
                               g_pal[index][0], g_pal[index][1], g_pal[index][2]);
        return;
    }
    memset(g_fb, (index << 4) | index, ((size_t)g_bw * g_bh + 1) / 2);
    g_dx0 = 0; g_dy0 = 0; g_dx1 = g_bw; g_dy1 = g_bh;
}

// Send the changed part of the buffer. The scroll ring can put the box
// across the wrap in display RAM, so it goes as one or two runs, each
// addressed once.
void picocalc_gfx_flip(void) {
    if (!g_fb || g_dx1 <= g_dx0 || g_dy1 <= g_dy0) return;
    static uint8_t row[SCR_W * 3];
    int w = g_dx1 - g_dx0;
    int y = g_dy0;
    while (y < g_dy1) {
        int screen_y = g_by + y;
        int ram_y = picocalc_lcd_ram_row(screen_y);
        int run = g_dy1 - y;
        int room = picocalc_lcd_ram_height() - ram_y;
        if (run > room) run = room;

        picocalc_lcd_blit_begin(g_bx + g_dx0, ram_y, w, run);
        for (int r = 0; r < run; r++) {
            uint8_t* p = row;
            for (int bxp = g_dx0; bxp < g_dx1; bxp++) {
                size_t o = ((size_t)(y + r) * g_bw + bxp) >> 1;
                int idx = (bxp & 1) ? (g_fb[o] & 0x0F) : (g_fb[o] >> 4);
                *p++ = g_pal[idx][0];
                *p++ = g_pal[idx][1];
                *p++ = g_pal[idx][2];
            }
            picocalc_lcd_blit_row(row, w);
        }
        picocalc_lcd_blit_end();
        y += run;
    }
    dirty_reset();
}

void picocalc_gfx_line(int x1, int y1, int x2, int y2) {
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        picocalc_gfx_pset(x1, y1);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void picocalc_gfx_rect(int x, int y, int w, int h, int fill) {
    if (fill) {
        for (int i = 0; i < h; i++) span_fill(x, y + i, w);
        return;
    }
    span_fill(x, y, w);
    span_fill(x, y + h - 1, w);
    for (int i = 0; i < h; i++) { picocalc_gfx_pset(x, y + i); picocalc_gfx_pset(x + w - 1, y + i); }
}

void picocalc_gfx_circle(int cx, int cy, int rad, int fill) {
    int x = rad, y = 0, err = 1 - rad;
    while (x >= y) {
        if (fill) {
            span_fill(cx - x, cy + y, 2 * x + 1);
            span_fill(cx - x, cy - y, 2 * x + 1);
            span_fill(cx - y, cy + x, 2 * y + 1);
            span_fill(cx - y, cy - x, 2 * y + 1);
        } else {
            picocalc_gfx_pset(cx + x, cy + y); picocalc_gfx_pset(cx - x, cy + y);
            picocalc_gfx_pset(cx + x, cy - y); picocalc_gfx_pset(cx - x, cy - y);
            picocalc_gfx_pset(cx + y, cy + x); picocalc_gfx_pset(cx - y, cy + x);
            picocalc_gfx_pset(cx + y, cy - x); picocalc_gfx_pset(cx - y, cy - x);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

// The console font at any pixel position, scaled by whole steps.
void picocalc_gfx_text(int x, int y, const char* s, int scale) {
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
                    picocalc_gfx_pset(x + col, y + row);
                } else {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            picocalc_gfx_pset(x + col * scale + sx, y + row * scale + sy);
                }
            }
        }
        x += 8 * scale;
    }
}

// A sprite frame from an RGBA source onto the panel. Colours snap to the
// sixteen-entry palette; a pixel is skipped when it is transparent
// enough to be meant as background, so sprites keep their shape without
// a mask. Alpha below the same line hides the whole sprite rather than
// blending, which this depth cannot do honestly.
void picocalc_gfx_blit_rgba(int dst_x, int dst_y, const uint8_t* rgba,
                            int src_w, int sx, int sy, int w, int h,
                            int flip_h, int flip_v, int alpha) {
    if (!rgba || w <= 0 || h <= 0 || alpha < 128) return;
    int saved = g_index;
    for (int row = 0; row < h; row++) {
        int src_row = flip_v ? (h - 1 - row) : row;
        const uint8_t* line = rgba + ((size_t)(sy + src_row) * src_w + sx) * 4;
        for (int col = 0; col < w; col++) {
            int src_col = flip_h ? (w - 1 - col) : col;
            const uint8_t* p = line + (size_t)src_col * 4;
            if (p[3] < 128) continue;
            g_index = nearest_index(p[0], p[1], p[2]);
            picocalc_gfx_pset(dst_x + col, dst_y + row);
        }
    }
    g_index = saved;
}
