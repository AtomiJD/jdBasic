// jdb_regex.h - the regular expression builtins both runtimes share.
//
// std::regex speaks ECMAScript, which has no inline flags, so a leading (?i)
// is read here and turned into icase, the one flag worth carrying; anywhere
// but the front it still means what ECMAScript says it means. A pattern
// compiles once and stays cached, since compiling is the dominant cost when
// one pattern runs over many lines. A pattern that does not compile, or a
// match that exceeds the engine's limits, finds nothing and replaces nothing.
#pragma once

#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace jdb_regex {

inline std::regex compile(const std::string& pattern) {
    if (pattern.rfind("(?i)", 0) == 0)
        return std::regex(pattern.substr(4), std::regex::icase);
    return std::regex(pattern);
}

// The compiled pattern, nullptr when it does not compile. The map never
// moves its nodes, so the pointer stays good after later insertions.
inline const std::regex* cached(const std::string& pattern) {
    static std::mutex guard;
    static std::unordered_map<std::string, std::regex> cache;
    std::lock_guard<std::mutex> lock(guard);
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        // Past a bound a new pattern is compiled for this call alone, so a
        // program that builds its patterns does not keep every one of them.
        if (cache.size() >= 512) {
            static thread_local std::regex scratch;
            try { scratch = compile(pattern); }
            catch (...) { return nullptr; }
            return &scratch;
        }
        try { it = cache.emplace(pattern, compile(pattern)).first; }
        catch (...) { return nullptr; }
    }
    return &it->second;
}

// Whether the pattern occurs anywhere in text.
inline bool search(const std::string& pattern, const std::string& text) {
    const std::regex* re = cached(pattern);
    if (!re) return false;
    try { return std::regex_search(text, *re); } catch (...) { return false; }
}

// Whether the whole text matches; groups receives the captures when the
// pattern has any.
inline bool match(const std::string& pattern, const std::string& text, std::vector<std::string>& groups) {
    groups.clear();
    const std::regex* re = cached(pattern);
    if (!re) return false;
    try {
        std::smatch m;
        if (!std::regex_match(text, m, *re)) return false;
        for (size_t i = 1; i < m.size(); i++) groups.push_back(m[i].str());
        return true;
    } catch (...) { return false; }
}

// Every whole match in text, in order.
inline std::vector<std::string> matches(const std::string& pattern, const std::string& text) {
    std::vector<std::string> out;
    const std::regex* re = cached(pattern);
    if (!re) return out;
    try {
        for (std::sregex_iterator it(text.begin(), text.end(), *re), end; it != end; ++it)
            out.push_back((*it)[0].str());
    } catch (...) {}
    return out;
}

// Every match in text, one row each: the whole match when the pattern has
// no groups, the groups when it has. has_groups says which shape came back.
inline void find_all(const std::string& pattern, const std::string& text,
                     std::vector<std::vector<std::string>>& rows, bool& has_groups) {
    rows.clear();
    has_groups = false;
    const std::regex* re = cached(pattern);
    if (!re) return;
    try {
        for (std::sregex_iterator it(text.begin(), text.end(), *re), end; it != end; ++it) {
            const std::smatch& m = *it;
            std::vector<std::string> row;
            if (m.size() > 1) {
                has_groups = true;
                for (size_t g = 1; g < m.size(); g++) row.push_back(m[g].str());
            } else {
                row.push_back(m[0].str());
            }
            rows.push_back(std::move(row));
        }
    } catch (...) {}
}

// text with every match replaced; $1, $2 and $& in the replacement refer to
// the match. The text itself when the pattern does not compile.
inline std::string replace(const std::string& pattern, const std::string& text, const std::string& replacement) {
    const std::regex* re = cached(pattern);
    if (!re) return text;
    try { return std::regex_replace(text, *re, replacement); } catch (...) { return text; }
}

} // namespace jdb_regex
