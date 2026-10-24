// jdb_text.h - the string operations both runtimes share.
//
// A string is a run of bytes that may hold a NUL, UTF-8 where the text
// came from source or a file; positions and lengths count bytes, which is
// what LEN, MID$ and INSTR answer, except where a function says it walks
// characters (REVERSE$, the character split of unary minus, CHR$). Every
// function takes its input as (pointer, length) and answers a std::string;
// the VM hands that back as a Value, the native runtime copies it into a
// buffer of its own, so its ownership rules are untouched.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace jdb_text {

struct View {
    const char* p = "";
    size_t n = 0;
    View() = default;
    View(const char* s, size_t len) : p(s ? s : ""), n(s ? len : 0) {}
    View(const std::string& s) : p(s.data()), n(s.size()) {}
    std::string str() const { return std::string(p, n); }
    std::string sub(size_t at, size_t len) const {
        if (at > n) return std::string();
        if (len > n - at) len = n - at;
        return std::string(p + at, len);
    }
};

// Whether s holds the bytes of needle at position at.
inline bool has_at(View s, View needle, size_t at) {
    return at + needle.n <= s.n && (needle.n == 0 || std::memcmp(s.p + at, needle.p, needle.n) == 0);
}

// The position of needle in s at or after start, -1 for none; an empty
// needle is found at start when start is within s.
inline int64_t find(View s, View needle, int64_t start = 0) {
    if (start < 0) start = 0;
    if ((size_t)start > s.n) return -1;
    if (needle.n == 0) return start;
    for (size_t i = (size_t)start; i + needle.n <= s.n; i++)
        if (std::memcmp(s.p + i, needle.p, needle.n) == 0) return (int64_t)i;
    return -1;
}

// The first n bytes; n is clamped to the string.
inline std::string left(View s, int64_t n) {
    if (n < 0) n = 0;
    return s.sub(0, (size_t)n);
}

// The last n bytes; n is clamped to the string.
inline std::string right(View s, int64_t n) {
    if (n < 0) n = 0;
    if ((size_t)n >= s.n) return s.str();
    return s.sub(s.n - (size_t)n, (size_t)n);
}

// Whether a start position names a byte of s or its end.
inline bool in_range(View s, int64_t start) { return start >= 0 && (size_t)start <= s.n; }

// len bytes from start; a start before the string reads from its front,
// one past its end reads nothing, a negative len reads to the end.
inline std::string mid(View s, int64_t start, int64_t len) {
    if (start < 0) start = 0;
    if ((size_t)start > s.n) return std::string();
    size_t avail = s.n - (size_t)start;
    size_t take = len < 0 || (size_t)len > avail ? avail : (size_t)len;
    return s.sub((size_t)start, take);
}

// n / "str" answers the first n bytes, "str" / n the last n.
inline std::string slice(View s, int64_t n, bool from_left) {
    return from_left ? left(s, n) : right(s, n);
}

inline std::string upper(View s) {
    std::string r = s.str();
    for (auto& c : r) c = (char)toupper((unsigned char)c);
    return r;
}

inline std::string lower(View s) {
    std::string r = s.str();
    for (auto& c : r) c = (char)tolower((unsigned char)c);
    return r;
}

// The blanks TRIM$ removes: space, tab, carriage return and line feed.
inline bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

inline std::string ltrim(View s) {
    size_t a = 0;
    while (a < s.n && is_blank(s.p[a])) a++;
    return s.sub(a, s.n - a);
}

inline std::string rtrim(View s) {
    size_t b = s.n;
    while (b > 0 && is_blank(s.p[b - 1])) b--;
    return s.sub(0, b);
}

inline std::string trim(View s) {
    size_t a = 0, b = s.n;
    while (a < b && is_blank(s.p[a])) a++;
    while (b > a && is_blank(s.p[b - 1])) b--;
    return s.sub(a, b - a);
}

// Every occurrence of find replaced by rep, left to right without
// overlap; an empty find leaves the text as it is.
inline std::string replace_all(View s, View find_text, View rep) {
    if (find_text.n == 0) return s.str();
    std::string r;
    r.reserve(s.n);
    size_t i = 0;
    while (i < s.n) {
        if (has_at(s, find_text, i)) { r.append(rep.p, rep.n); i += find_text.n; }
        else r.push_back(s.p[i++]);
    }
    return r;
}

// The text with every occurrence of part removed, left to right.
inline std::string remove_all(View s, View part) {
    return replace_all(s, part, View("", 0));
}

// The parts between occurrences of delim, handed to emit in order; an
// empty delimiter or none found gives the whole text as the one part.
template <typename Emit>
inline void split(View s, View delim, Emit emit) {
    if (delim.n == 0) { emit(s.str()); return; }
    size_t pos = 0;
    for (;;) {
        int64_t at = find(s, delim, (int64_t)pos);
        if (at < 0) { emit(s.sub(pos, s.n - pos)); return; }
        emit(s.sub(pos, (size_t)at - pos));
        pos = (size_t)at + delim.n;
    }
}

inline std::string repeat(View s, int64_t n) {
    std::string r;
    if (n <= 0 || s.n == 0) return r;
    r.reserve(s.n * (size_t)n);
    for (int64_t i = 0; i < n; i++) r.append(s.p, s.n);
    return r;
}

inline std::string spaces(int64_t n) {
    return n > 0 ? std::string((size_t)n, ' ') : std::string();
}

// The pad text repeated and cut to fill the text up to n bytes, on the
// left for lpad and on the right for rpad; an empty pad is a space.
inline std::string fill(View pad, size_t need) {
    View p = pad.n ? pad : View(" ", 1);
    std::string f;
    f.reserve(need);
    for (size_t i = 0; i < need; i++) f.push_back(p.p[i % p.n]);
    return f;
}

inline std::string lpad(View s, int64_t n, View pad) {
    if (n <= 0 || (size_t)n <= s.n) return s.str();
    return fill(pad, (size_t)n - s.n) + s.str();
}

inline std::string rpad(View s, int64_t n, View pad) {
    if (n <= 0 || (size_t)n <= s.n) return s.str();
    return s.str() + fill(pad, (size_t)n - s.n);
}

// ins laid into s at byte position pos, clamped to the string.
inline std::string insert(View s, View ins, int64_t pos) {
    if (pos < 0) pos = 0;
    if ((size_t)pos > s.n) pos = (int64_t)s.n;
    std::string r = s.sub(0, (size_t)pos);
    r.append(ins.p, ins.n);
    r.append(s.p + pos, s.n - (size_t)pos);
    return r;
}

inline bool starts_with(View s, View prefix) { return has_at(s, prefix, 0); }

inline bool ends_with(View s, View suffix) {
    return suffix.n <= s.n && has_at(s, suffix, s.n - suffix.n);
}

// Byte-wise order of two strings, as unsigned bytes: -1, 0 or 1.
inline int compare(View a, View b) {
    size_t n = a.n < b.n ? a.n : b.n;
    int c = n ? std::memcmp(a.p, b.p, n) : 0;
    if (c != 0) return c < 0 ? -1 : 1;
    return a.n == b.n ? 0 : (a.n < b.n ? -1 : 1);
}

inline bool equal(View a, View b) {
    return a.n == b.n && (a.n == 0 || std::memcmp(a.p, b.p, a.n) == 0);
}

// The byte length of the UTF-8 character that starts with lead; a lead
// the text cannot complete is cut at the text's end.
inline size_t utf8_len(View s, size_t at) {
    unsigned char c = (unsigned char)s.p[at];
    size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (at + len > s.n) len = s.n - at;
    return len;
}

// The UTF-8 characters of s, each handed to emit as its own string.
template <typename Emit>
inline void chars(View s, Emit emit) {
    for (size_t i = 0; i < s.n;) {
        size_t len = utf8_len(s, i);
        emit(std::string(s.p + i, len));
        i += len;
    }
}

// The text with its UTF-8 characters in the opposite order.
inline std::string reverse(View s) {
    std::string r;
    r.reserve(s.n);
    for (size_t i = 0; i < s.n;) {
        size_t len = utf8_len(s, i);
        r.insert(0, s.p + i, len);
        i += len;
    }
    return r;
}

// CHR$: a code up to 255 is that one byte, a code above it that Unicode
// character in UTF-8; a code past the Unicode range is its low byte.
inline std::string chr(int64_t code) {
    if (code <= 255 || code > 0x10FFFF) return std::string(1, (char)code);
    std::string s;
    if (code < 0x800) {
        s += (char)(0xC0 | (code >> 6));
    } else if (code < 0x10000) {
        s += (char)(0xE0 | (code >> 12));
        s += (char)(0x80 | ((code >> 6) & 0x3F));
    } else {
        s += (char)(0xF0 | (code >> 18));
        s += (char)(0x80 | ((code >> 12) & 0x3F));
        s += (char)(0x80 | ((code >> 6) & 0x3F));
    }
    s += (char)(0x80 | (code & 0x3F));
    return s;
}

// ASC: the first byte, 0 for an empty text.
inline int64_t asc(View s) { return s.n ? (unsigned char)s.p[0] : 0; }

}  // namespace jdb_text
