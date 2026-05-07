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
    return -1;
}

// Decode bytes (in `encoding`) -> UTF-8. Empty/utf-8 pass through.
inline std::string decode_to_utf8(const std::string& bytes, const std::string& encoding) {
    int cp = codepage_id(encoding);
    if (cp == -1) throw std::runtime_error("unknown encoding: " + encoding);
    if (cp == 0 || bytes.empty()) return bytes;
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
