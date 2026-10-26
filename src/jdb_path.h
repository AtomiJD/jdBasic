// jdb_path.h - path text and file metadata both runtimes share.
//
// The text functions read either separator and write the platform's own;
// they never touch the file system. The three readers at the end do, and
// answer what FILE.EXISTS, FILE.SIZE and FILE.ISDIR say.
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace jdb_path {

#ifdef _WIN32
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

inline bool is_sep(char c) { return c == '/' || c == '\\'; }

// The part after the last separator; the whole path when there is none.
inline std::string basename(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

// The extension with its dot, "" when the last segment has none.
inline std::string ext(const std::string& p) {
    size_t slash = p.find_last_of("/\\");
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return std::string();
    return p.substr(dot);
}

// Everything before the last separator: "" for a bare name, the separator
// itself for a path directly under the root.
inline std::string dirname(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    if (pos == std::string::npos) return std::string();
    if (pos == 0) return p.substr(0, 1);
    return p.substr(0, pos);
}

// The parts joined with the platform separator; a part that already ends in
// a separator gets none added, an empty part adds nothing.
inline std::string join(const std::vector<std::string>& parts) {
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0 && !result.empty() && !is_sep(result.back())) result += kSep;
        result += parts[i];
    }
    return result;
}

// The path with every separator the platform's own, repeated separators
// folded, "." dropped and ".." resolved against the segment before it. A
// Windows drive letter stays in front; ".." above the root of a rooted
// path is dropped, above the start of a relative one it is kept. The empty
// path is ".".
inline std::string normalize(const std::string& p) {
    std::string s;
    s.reserve(p.size());
    for (char c : p) s.push_back(is_sep(c) ? kSep : c);
    std::string prefix;
    size_t fixed_parts = 0;   // leading parts ".." cannot remove
    std::string body = s;
#ifdef _WIN32
    if (body.size() >= 2 && std::isalpha((unsigned char)body[0]) && body[1] == ':') {
        prefix = body.substr(0, 2);
        body = body.substr(2);
    } else if (body.size() >= 2 && body[0] == kSep && body[1] == kSep) {
        // A UNC path keeps its two leading separators: one would name a
        // path on the current drive.
        prefix.assign(1, kSep);
        fixed_parts = 2;   // the server and the share are the root of a UNC path
        body = body.substr(1);
    }
#endif
    bool rooted = !body.empty() && body[0] == kSep;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && body[i] == kSep) i++;
        size_t j = i;
        while (j < body.size() && body[j] != kSep) j++;
        if (i < j) parts.push_back(body.substr(i, j - i));
        i = j;
    }
    std::vector<std::string> out;
    for (auto& part : parts) {
        if (part == ".") continue;
        if (part == "..") {
            if (out.size() > fixed_parts && out.back() != "..") out.pop_back();
            else if (!rooted) out.push_back("..");
        } else {
            out.push_back(part);
        }
    }
    std::string res = prefix;
    if (rooted) res.push_back(kSep);
    for (size_t k = 0; k < out.size(); k++) {
        if (k) res.push_back(kSep);
        res += out[k];
    }
    if (res.empty()) res = ".";
    return res;
}

// Whether the path names a place on its own, without a current directory
// to read it against: on Windows a drive with a separator, a UNC share or
// a path rooted on the current drive; elsewhere a path starting at /.
inline bool is_abs(const std::string& p) {
    if (p.empty()) return false;
#ifdef _WIN32
    if (p.size() >= 3 && p[1] == ':' && is_sep(p[2])) return true;
    return p.size() >= 2 && is_sep(p[0]);
#else
    return p[0] == '/';
#endif
}

// Whether something, file or directory, is at the path.
inline bool exists(const std::string& p) {
#ifdef _WIN32
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0;
#endif
}

// Whether the path is a directory.
inline bool is_dir(const std::string& p) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(p.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// The size of the file in bytes; -1 for a directory or a path with nothing
// at it.
inline int64_t size(const std::string& p) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fad)) return -1;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return ((int64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
#else
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return -1;
    return (int64_t)st.st_size;
#endif
}

} // namespace jdb_path
