// jdb_encoding.h — codepage <-> UTF-8 conversion for TXTREADER$/TXTWRITER.
//
// Both vm.cpp (interpreter) and jdb_runtime.cpp (native runtime) include this
// to share one implementation. Empty / "utf-8" passes the bytes through
// unchanged; otherwise we round-trip via UTF-16 on Windows.
//
// Linux/macOS support is intentionally limited to "" / "utf-8" pass-through
// for now — the codepage families that matter (cp1252 / cp850) are
// Windows-legacy formats and Atomi's Linux port doesn't touch VBA sources.
#pragma once

#include <string>
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace jdb_enc {

// Map a user-supplied encoding name to a Windows codepage number.
// Returns 0 for "" / "utf-8" / "utf8" (= no conversion needed).
// Returns -1 for an unknown name (caller should throw).
inline int codepage_id(const std::string& enc) {
    if (enc.empty()) return 0;
    // Lower-case copy, strip '-' and '_' to match common spellings.
    std::string n;
    n.reserve(enc.size());
    for (char c : enc) {
        if (c == '-' || c == '_') continue;
        n.push_back((c >= 'A' && c <= 'Z') ? char(c + 32) : c);
    }
    if (n == "utf8" || n == "ascii" || n == "usascii") return 0;
    if (n == "cp1252" || n == "windows1252" || n == "1252" ||
        n == "iso88591" || n == "latin1") return 1252;
    if (n == "cp850" || n == "ibm850" || n == "850") return 850;
    if (n == "cp437" || n == "ibm437" || n == "437") return 437;
    if (n == "cp1250" || n == "windows1250" || n == "1250") return 1250;
    if (n == "cp1251" || n == "windows1251" || n == "1251") return 1251;
    if (n == "cp936" || n == "gb2312" || n == "936") return 936;
    if (n == "cp932" || n == "shiftjis" || n == "932") return 932;
    // UTF-16: not a Windows codepage - handled by the portable path below.
    if (n == "utf16" || n == "utf16le" || n == "ucs2" || n == "ucs2le" || n == "unicode") return -2;
    if (n == "utf16be" || n == "ucs2be") return -3;
    return -1;
}

// --- UTF-16 <-> UTF-8 (portable, no platform API; also used on Linux/Mac) ---
inline void utf8_append_cp(std::string& out, unsigned cp) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

inline std::string utf16_to_utf8(const std::string& bytes, bool big_endian) {
    std::string out;
    size_t n = bytes.size(), i = 0;
    if (n >= 2) {
        unsigned char b0 = (unsigned char)bytes[0], b1 = (unsigned char)bytes[1];
        if (b0 == 0xFF && b1 == 0xFE) { big_endian = false; i = 2; }  // BOM wins
        else if (b0 == 0xFE && b1 == 0xFF) { big_endian = true; i = 2; }
    }
    out.reserve(n / 2);
    auto rd = [&](size_t p) -> unsigned {
        unsigned char a = (unsigned char)bytes[p], b = (unsigned char)bytes[p + 1];
        return big_endian ? (unsigned)((a << 8) | b) : (unsigned)((b << 8) | a);
    };
    while (i + 1 < n) {
        unsigned u = rd(i); i += 2;
        unsigned cp = u;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n) {  // high surrogate
            unsigned u2 = rd(i);
            if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
                cp = 0x10000 + ((u - 0xD800) << 10) + (u2 - 0xDC00);
                i += 2;
            }
        }
        utf8_append_cp(out, cp);
    }
    return out;
}

inline std::string utf8_to_utf16(const std::string& utf8, bool big_endian) {
    std::string out;
    size_t n = utf8.size(), i = 0;
    out.reserve(utf8.size() * 2);
    auto emit = [&](unsigned u) {
        unsigned char lo = (unsigned char)(u & 0xFF), hi = (unsigned char)((u >> 8) & 0xFF);
        if (big_endian) { out.push_back((char)hi); out.push_back((char)lo); }
        else { out.push_back((char)lo); out.push_back((char)hi); }
    };
    while (i < n) {
        unsigned char c = (unsigned char)utf8[i];
        unsigned cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
        else { cp = 0xFFFD; len = 1; }
        for (int k = 1; k < len && i + (size_t)k < n; k++)
            cp = (cp << 6) | ((unsigned char)utf8[i + k] & 0x3F);
        i += (size_t)len;
        if (cp < 0x10000) emit(cp);
        else { cp -= 0x10000; emit(0xD800 + (cp >> 10)); emit(0xDC00 + (cp & 0x3FF)); }
    }
    return out;
}

// Detect UTF-16 with no BOM. UTF-8/ASCII text never contains 0x00, so a run of
// alternating NUL bytes is a reliable signal. Returns 0=no, 1=LE, 2=BE.
inline int sniff_utf16(const std::string& b) {
    size_t n = b.size();
    if (n >= 2) {
        unsigned char b0 = (unsigned char)b[0], b1 = (unsigned char)b[1];
        if (b0 == 0xFF && b1 == 0xFE) return 1;
        if (b0 == 0xFE && b1 == 0xFF) return 2;
    }
    if (n < 4) return 0;
    size_t lim = (n < 128 ? n : 128) & ~(size_t)1;
    int zero_odd = 0, zero_even = 0, pairs = 0;
    for (size_t i = 0; i + 1 < lim; i += 2) {
        if (b[i] == 0) zero_even++;
        if (b[i + 1] == 0) zero_odd++;
        pairs++;
    }
    if (pairs == 0) return 0;
    if (zero_odd > pairs * 3 / 4 && zero_even == 0) return 1;   // LE: high byte NUL
    if (zero_even > pairs * 3 / 4 && zero_odd == 0) return 2;   // BE: low byte NUL
    return 0;
}

// Decode bytes (in `encoding`) -> UTF-8. Empty/utf-8 pass through.
inline std::string decode_to_utf8(const std::string& bytes, const std::string& encoding) {
    int cp = codepage_id(encoding);
    if (cp == -2) return utf16_to_utf8(bytes, false);
    if (cp == -3) return utf16_to_utf8(bytes, true);
    if (cp == -1) throw std::runtime_error("unknown encoding: " + encoding);
    if (bytes.empty()) return bytes;
    if (cp == 0) {
        // No 8-bit codepage requested: auto-detect UTF-16 (BOM or NUL pattern),
        // otherwise pass the bytes through unchanged as UTF-8 / ASCII.
        int u16 = sniff_utf16(bytes);
        if (u16 == 1) return utf16_to_utf8(bytes, false);
        if (u16 == 2) return utf16_to_utf8(bytes, true);
        return bytes;
    }
#ifdef _WIN32
    int wlen = MultiByteToWideChar(cp, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) throw std::runtime_error("decode failed for encoding: " + encoding);
    std::wstring wbuf((size_t)wlen, L'\0');
    MultiByteToWideChar(cp, 0, bytes.data(), (int)bytes.size(), &wbuf[0], wlen);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) throw std::runtime_error("utf-8 encode failed");
    std::string out((size_t)u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, &out[0], u8len, nullptr, nullptr);
    return out;
#else
    throw std::runtime_error("codepage decoding not supported on this platform: " + encoding);
#endif
}

// Encode UTF-8 -> bytes (in `encoding`). Empty/utf-8 pass through.
inline std::string encode_from_utf8(const std::string& utf8, const std::string& encoding) {
    int cp = codepage_id(encoding);
    if (cp == -2) return utf8_to_utf16(utf8, false);
    if (cp == -3) return utf8_to_utf16(utf8, true);
    if (cp == -1) throw std::runtime_error("unknown encoding: " + encoding);
    if (cp == 0 || utf8.empty()) return utf8;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    if (wlen <= 0) throw std::runtime_error("utf-8 decode failed");
    std::wstring wbuf((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &wbuf[0], wlen);
    // Use '?' for chars that don't fit the target codepage rather than fail —
    // matches the behaviour of Python's encode("cp1252", errors="replace")
    // and is what Excel/Access do when they emit text with no-fit chars.
    BOOL used_default = FALSE;
    int blen = WideCharToMultiByte(cp, 0, wbuf.data(), wlen, nullptr, 0, "?", &used_default);
    if (blen <= 0) throw std::runtime_error("encode failed for encoding: " + encoding);
    std::string out((size_t)blen, '\0');
    WideCharToMultiByte(cp, 0, wbuf.data(), wlen, &out[0], blen, "?", &used_default);
    return out;
#else
    throw std::runtime_error("codepage encoding not supported on this platform: " + encoding);
#endif
}

} // namespace jdb_enc
