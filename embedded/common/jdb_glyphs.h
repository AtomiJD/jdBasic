// What a console needs beyond the 8x8 font's bytes: the glyphs reached
// through a code point (the German letters, the section and degree
// signs, the acute, the euro), and a decoder that turns the UTF-8 the
// interpreter prints into those code points one byte at a time.
//
// A byte above 127 that is not part of a sequence is still a glyph of
// the font by number, which is how the graphics characters are reached;
// the decoder marks such a byte with JDB_RAW_BYTE so a console can tell
// it from a code point of the same value.

#pragma once
#include <stdint.h>
#include <stddef.h>

struct jdb_extra_glyph { uint32_t cp; uint8_t rows[8]; };

// In the style of the C64 face: two dots over the base letter.
static const struct jdb_extra_glyph jdb_extra_glyphs[] = {
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
#define JDB_EXTRA_N ((int)(sizeof jdb_extra_glyphs / sizeof jdb_extra_glyphs[0]))

// A box, for a code point the font cannot show.
static const uint8_t jdb_unknown_glyph[8] = { 0x7e, 0x42, 0x42, 0x42, 0x42, 0x42, 0x7e, 0x00 };

// Index into jdb_extra_glyphs, or -1.
static inline int jdb_extra_index(uint32_t cp) {
    for (int i = 0; i < JDB_EXTRA_N; i++)
        if (jdb_extra_glyphs[i].cp == cp) return i;
    return -1;
}

// A cell console keeps one byte per cell. Values below 32 never hold
// text, so they carry the extra glyphs: 1..JDB_EXTRA_N is an extra
// glyph, JDB_CELL_UNKNOWN the box, anything else below 32 a blank.
#define JDB_CELL_UNKNOWN 31

static inline uint8_t jdb_cell_for_cp(uint32_t cp) {
    int i = jdb_extra_index(cp);
    return i >= 0 ? (uint8_t)(i + 1) : (uint8_t)JDB_CELL_UNKNOWN;
}

// The eight rows for a cell byte, given the font (first glyph is 32).
static inline const uint8_t* jdb_cell_rows(const uint8_t* font, uint8_t cell) {
    if (cell >= 32) return font + (cell - 32) * 8;
    if (cell >= 1 && cell <= JDB_EXTRA_N) return jdb_extra_glyphs[cell - 1].rows;
    if (cell == JDB_CELL_UNKNOWN) return jdb_unknown_glyph;
    return font;
}

#define JDB_RAW_BYTE 0x10000000u

struct jdb_utf8_dec { uint32_t cp; int need; unsigned char lead; };

// Feeds one byte. Returns how many values are ready in out: a code
// point, or a byte flagged JDB_RAW_BYTE. A lead byte whose sequence
// breaks off comes back as a raw byte first.
static inline int jdb_utf8_feed(struct jdb_utf8_dec* d, unsigned char c, uint32_t out[2]) {
    int n = 0;
    if (d->need > 0) {
        if ((c & 0xC0) == 0x80) {
            d->cp = (d->cp << 6) | (c & 0x3F);
            if (--d->need == 0) out[n++] = d->cp;
            return n;
        }
        out[n++] = JDB_RAW_BYTE | d->lead;
        d->need = 0;
    }
    if (c >= 0xC2 && c <= 0xDF) { d->need = 1; d->cp = c & 0x1F; d->lead = c; return n; }
    if (c >= 0xE0 && c <= 0xEF) { d->need = 2; d->cp = c & 0x0F; d->lead = c; return n; }
    out[n++] = c < 0x80 ? c : (JDB_RAW_BYTE | c);
    return n;
}

// The cell byte a decoded value lands in.
static inline uint8_t jdb_cell_for(uint32_t v) {
    if (v & JDB_RAW_BYTE) return (uint8_t)(v & 0xFF);
    if (v < 0x80) return (uint8_t)v;
    return jdb_cell_for_cp(v);
}
