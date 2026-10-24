// jdb_number.h - the number arithmetic and the number text both runtimes
// share: a number written the way PRINT and STR$ write it, under the
// punctuation of the locale SETLOCALE chose, the text VAL and the
// conversions read, HEX$, OCT$ and BIN$, ROUND with places, FAC, GCD, LCM,
// the rotations and the small scalar helpers. The VM (value.h,
// vm_builtins_*.cpp) and the native runtime (jdb_runtime.cpp) include this
// and keep only their own value plumbing.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <locale>

namespace jdb_num {

// The punctuation a locale writes numbers with: its decimal mark and the
// separator between groups of three digits, 0 for no grouping.
struct Punct {
    char decimal = '.';
    char group = 0;
};

// The punctuation of a C++ locale. Grouping is read as every three digits,
// which is what the locales SETLOCALE is used for do.
inline Punct punct_of(const std::locale& loc) {
    Punct p;
    const auto& np = std::use_facet<std::numpunct<char>>(loc);
    p.decimal = np.decimal_point();
    p.group = np.grouping().empty() ? 0 : np.thousands_sep();
    return p;
}

// digits, a run of decimal digits, laid into out in groups of three
// separated by group when group is set. Answers the characters written.
inline int group_digits(char* out, int cap, const char* digits, int n, char group) {
    int o = 0;
    for (int i = 0; i < n && o < cap - 1; i++) {
        if (group && i > 0 && (n - i) % 3 == 0) out[o++] = group;
        if (o < cap - 1) out[o++] = digits[i];
    }
    out[o] = '\0';
    return o;
}

// An integer as text.
inline int format_int(char* out, int cap, int64_t v, const Punct& p) {
    char raw[32];
    std::snprintf(raw, sizeof(raw), "%lld", (long long)v);
    const char* digits = raw[0] == '-' ? raw + 1 : raw;
    int o = 0;
    if (raw[0] == '-' && cap > 1) out[o++] = '-';
    return o + group_digits(out + o, cap - o, digits, (int)std::strlen(digits), p.group);
}

// A number as PRINT and STR$ write it: fixed with six decimals, the
// trailing zeros and a bare decimal mark dropped, so 1e8 is 100000000, 1.5
// is 1.5 and 1e-10 is 0; grouped and marked by the punctuation. Whatever
// decimal mark the C library writes under the process locale is read as
// the mark, so the text comes out the same under any setlocale.
inline int format_double(char* out, int cap, double v, const Punct& p) {
    if (cap < 2) { if (cap > 0) out[0] = '\0'; return 0; }
    if (v != v || std::isinf(v)) return std::snprintf(out, cap, "%f", v);
    char raw[400];
    int n = std::snprintf(raw, sizeof(raw), "%.6f", v);
    if (n <= 0 || n >= (int)sizeof(raw)) return std::snprintf(out, cap, "%f", v);
    int start = raw[0] == '-' ? 1 : 0;
    int mark = start;
    while (mark < n && raw[mark] >= '0' && raw[mark] <= '9') mark++;
    int end = n;
    if (mark < n) {
        while (end > mark + 1 && raw[end - 1] == '0') end--;
        if (end == mark + 1) end = mark;
    }
    int o = 0;
    if (start && o < cap - 1) out[o++] = '-';
    o += group_digits(out + o, cap - o, raw + start, mark - start, p.group);
    if (end > mark && o < cap - 1) {
        out[o++] = p.decimal;
        for (int i = mark + 1; i < end && o < cap - 1; i++) out[o++] = raw[i];
    }
    out[o] = '\0';
    return o;
}

// The number a text starts with, the way VAL reads it: leading space
// skipped, the longest prefix strtod accepts, 0 for none.
inline double parse_prefix(const char* s) {
    if (!s) return 0.0;
    char* end = nullptr;
    double d = std::strtod(s, &end);
    return end == s ? 0.0 : d;
}

// A text that is one number with spaces around it at most, the way CINT,
// CLNG, CDBL and CSNG read it. False for anything else, an empty text too.
inline bool parse_whole(const char* s, double& out) {
    if (!s) return false;
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!*p) return false;
    char* end = nullptr;
    double d = std::strtod(p, &end);
    if (end == p) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    if (*end) return false;
    out = d;
    return true;
}

inline int format_hex(char* out, int cap, int64_t v) {
    return std::snprintf(out, cap, "%llX", (unsigned long long)v);
}

inline int format_oct(char* out, int cap, int64_t v) {
    return std::snprintf(out, cap, "%llo", (unsigned long long)v);
}

// The binary digits of v without leading zeros, "0" for zero; a negative
// number shows all 64 bits of its two's complement.
inline int format_bin(char* out, int cap, int64_t v) {
    if (v == 0) { if (cap > 1) { out[0] = '0'; out[1] = '\0'; return 1; } return 0; }
    uint64_t u = (uint64_t)v;
    int bits = 64;
    while (bits > 1 && !(u >> (bits - 1) & 1)) bits--;
    int o = 0;
    for (int i = bits - 1; i >= 0 && o < cap - 1; i--) out[o++] = (u >> i & 1) ? '1' : '0';
    out[o] = '\0';
    return o;
}

// ROUND to places decimals, negative places rounding to tens and hundreds;
// halves go away from zero.
inline double round_places(double v, double places) {
    double m = std::pow(10.0, places);
    return std::round(v * m) / m;
}

inline int64_t gcd(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}

inline int64_t lcm(int64_t a, int64_t b) {
    if (a == 0 || b == 0) return 0;
    int64_t g = gcd(a, b);
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    return (a / g) * b;
}

// n!, exact in whole numbers while it fits (up to 20!) and continued in
// doubles beyond, which reach 170!; 0 for a negative n.
inline double fac(int64_t n) {
    if (n < 0) return 0.0;
    int64_t ir = 1;
    int64_t i = 2;
    for (; i <= n; i++) {
        int64_t nr = (int64_t)((uint64_t)ir * (uint64_t)i);
        if (ir != 0 && nr / ir != i) break;
        ir = nr;
    }
    double r = (double)ir;
    for (; i <= n; i++) r *= (double)i;
    return r;
}

// Whether n! fits a whole number, so the VM may answer an integer.
inline bool fac_is_whole(int64_t n) { return n >= 0 && n <= 20; }

inline bool rotation_width_ok(int w) { return w == 8 || w == 16 || w == 32 || w == 64; }

// x rotated left by n within the low w bits; a negative n rotates right.
inline uint64_t rotl(uint64_t x, int64_t n, int w) {
    uint64_t mask = w == 64 ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1;
    x &= mask;
    n = ((n % w) + w) % w;
    if (n == 0) return x;
    return ((x << n) | (x >> (w - n))) & mask;
}

inline uint64_t rotr(uint64_t x, int64_t n, int w) {
    uint64_t mask = w == 64 ? ~(uint64_t)0 : ((uint64_t)1 << w) - 1;
    x &= mask;
    n = ((n % w) + w) % w;
    if (n == 0) return x;
    return ((x >> n) | (x << (w - n))) & mask;
}

inline int sign(double v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

// CINT keeps the low 32 bits of the whole part, CLNG the whole part, CSNG
// the single precision value.
inline int64_t cint(double v) { return (int64_t)(int32_t)v; }
inline int64_t clng(double v) { return (int64_t)v; }
inline double csng(double v) { return (double)(float)v; }

}  // namespace jdb_num
