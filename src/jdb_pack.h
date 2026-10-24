// jdb_pack.h - the PACK$ / UNPACK format both runtimes share.
//
// A format is a run of codes, each with an optional repeat count in front,
// and < or > setting the byte order for the codes after it; it starts
// little-endian. Spaces between codes are allowed.
//   b s i   unsigned integers of 1, 2 and 4 bytes
//   c h n   the signed readings of the same widths
//   l       a signed 8 byte integer
//   f d     float and double
//   a       a fixed string, the count is its length, NUL padded or cut
//   x       a zero pad byte
// The functions here parse the format and move the bytes; which value feeds
// a code, and what a decoded number becomes, is the caller's business.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace jdb_pack {

struct Field {
    char code;
    int count;
    bool big_endian;
};

// The byte width of one code, 0 for a character that is no code.
inline int width(char code) {
    switch (code) {
        case 'b': case 'c': case 'a': case 'x': return 1;
        case 's': case 'h': return 2;
        case 'i': case 'n': case 'f': return 4;
        case 'l': case 'd': return 8;
    }
    return 0;
}

inline bool is_float(char code) { return code == 'f' || code == 'd'; }

// The fields of a format. who names the builtin in the error a bad format
// raises.
inline std::vector<Field> parse(const std::string& fmt, const char* who) {
    std::vector<Field> fields;
    bool big_endian = false;
    int64_t count = -1;
    for (char c : fmt) {
        if (c == ' ' || c == '\t') continue;
        if (c >= '0' && c <= '9') {
            count = (count < 0 ? 0 : count) * 10 + (c - '0');
            if (count > 100000000)
                throw std::runtime_error(std::string(who) + ": repeat count too large");
            continue;
        }
        if (count >= 0 && (c == '<' || c == '>'))
            throw std::runtime_error(std::string(who) + ": a count must be followed by a code");
        if (c == '<') { big_endian = false; continue; }
        if (c == '>') { big_endian = true; continue; }
        char lc = (char)std::tolower((unsigned char)c);
        if (width(lc) == 0)
            throw std::runtime_error(std::string(who) + ": unknown format char '" + c + "'");
        fields.push_back({lc, count < 0 ? 1 : (int)count, big_endian});
        count = -1;
    }
    if (count >= 0)
        throw std::runtime_error(std::string(who) + ": a count must be followed by a code");
    return fields;
}

// The number of bytes the fields take.
inline size_t size(const std::vector<Field>& fields) {
    size_t total = 0;
    for (auto& f : fields) total += (size_t)width(f.code) * (size_t)f.count;
    return total;
}

// Appends the low width(f) bytes of bits in the field's byte order.
inline void put(std::string& out, const Field& f, uint64_t bits) {
    int n = width(f.code);
    for (int i = 0; i < n; i++) {
        int shift = f.big_endian ? 8 * (n - 1 - i) : 8 * i;
        out += (char)(uint8_t)(bits >> shift);
    }
}

// Appends a string field: the text cut or NUL padded to the field's count.
inline void put_text(std::string& out, const Field& f, std::string s) {
    s.resize((size_t)f.count, '\0');
    out += s;
}

// The bits a float or double field carries for the value.
inline uint64_t float_bits(const Field& f, double v) {
    if (f.code == 'f') {
        float x = (float)v;
        uint32_t b;
        std::memcpy(&b, &x, 4);
        return b;
    }
    uint64_t b;
    std::memcpy(&b, &v, 8);
    return b;
}

// The width(f) bytes at pos, read in the field's byte order. The caller has
// checked that the data reaches that far.
inline uint64_t get(const std::string& data, size_t pos, const Field& f) {
    int w = width(f.code);
    uint64_t u = 0;
    for (int j = 0; j < w; j++) {
        uint8_t byte = (uint8_t)(f.big_endian ? data[pos + w - 1 - j] : data[pos + j]);
        u |= (uint64_t)byte << (8 * j);
    }
    return u;
}

// The integer a decoded field means: signed for c, h, n and l.
inline int64_t as_int(const Field& f, uint64_t u) {
    switch (f.code) {
        case 'c': return (int8_t)(uint8_t)u;
        case 'h': return (int16_t)(uint16_t)u;
        case 'n': return (int32_t)(uint32_t)u;
        default: return (int64_t)u;
    }
}

// The number a decoded float or double field means.
inline double as_double(const Field& f, uint64_t u) {
    if (f.code == 'f') {
        uint32_t b = (uint32_t)u;
        float x;
        std::memcpy(&x, &b, 4);
        return x;
    }
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}

} // namespace jdb_pack
