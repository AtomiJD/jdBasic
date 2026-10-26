#pragma once
// Maps that builtins return use lower-case keys. Such a map carries a
// lower_keys mark, and for one release a read of a key it lacks that has
// upper-case letters falls back to the lower-case key, so programs written
// against the old upper-case keys (request{"BODY"}, r{"OUTPUT"}) keep
// running. The first such read prints one warning; tools/jdb-fix.jdb
// rewrites the keys.

#include <atomic>
#include <cstdio>
#include <string>
#include <string_view>

// The key in lower case, or an empty string when it has no upper-case letter.
inline std::string jdb_lowered_key(std::string_view key) {
    bool upper = false;
    for (char c : key) if (c >= 'A' && c <= 'Z') { upper = true; break; }
    if (!upper) return std::string();
    std::string out(key);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

inline void jdb_warn_upper_key(std::string_view old_key, std::string_view new_key) {
    static std::atomic<bool> warned{false};
    if (warned.exchange(true)) return;
    std::fprintf(stderr,
        "warning: maps from builtins use lower-case keys; \"%.*s\" was read as \"%.*s\". "
        "The old keys are read for one release; tools/jdb-fix.jdb rewrites them.\n",
        (int)old_key.size(), old_key.data(), (int)new_key.size(), new_key.data());
}
