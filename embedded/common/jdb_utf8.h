// Stepping through UTF-8 in a byte buffer: one code point is one column
// on the screen, so the editors move the cursor by sequences and count
// columns by lead bytes.
#pragma once

#include <stddef.h>

static inline int utf8_is_cont(unsigned char b) { return (b & 0xC0) == 0x80; }

// Length a lead byte announces, 1 for anything that is not a lead byte.
static inline int utf8_seq_len(unsigned char lead) {
    if (lead >= 0xF0) return 4;
    if (lead >= 0xE0) return 3;
    if (lead >= 0xC0) return 2;
    return 1;
}

// Byte offset of the code point before position i.
static inline int utf8_prev(const char* s, int i) {
    if (i <= 0) return 0;
    i--;
    while (i > 0 && utf8_is_cont((unsigned char)s[i])) i--;
    return i;
}

// Byte offset of the code point after position i.
static inline int utf8_next(const char* s, int len, int i) {
    if (i >= len) return len;
    i++;
    while (i < len && utf8_is_cont((unsigned char)s[i])) i++;
    return i;
}

// Columns between two byte offsets.
static inline int utf8_cols(const char* s, int from, int to) {
    int n = 0;
    for (int i = from; i < to; i++)
        if (!utf8_is_cont((unsigned char)s[i])) n++;
    return n;
}

// Byte offset where column col begins, or len when the line is shorter.
static inline int utf8_byte_at(const char* s, int len, int col) {
    int i = 0, n = 0;
    while (i < len && n < col) { i = utf8_next(s, len, i); n++; }
    return i;
}
