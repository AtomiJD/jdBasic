#pragma once
// Old names that still resolve, each to the name that replaced it. The lexer
// rewrites an old name to its new one, so every backend sees only new names,
// and records the use; --lint, jdb_check and -c report it, and tools/jdb-fix
// reads the same table through JDB.ALIASES() to rewrite source files.
//
// An old name is rewritten where it is called or taken as a funcref, that is
// when "(" or "@" follows it, or anywhere when `bare` is set (a builtin used
// without parentheses). A name right after a "." is a member and is left
// alone. Sorted by old_name; lookup is a binary search.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

struct NameAlias {
    const char* old_name;
    const char* new_name;
    bool bare;
};

inline constexpr NameAlias kNameAliases[] = {
    {"CHR",         "CHR$",       false},
    {"INSTR$",      "INSTR",      false},
    {"JSON.PARSE$", "JSON.PARSE", false},
    {"LCASE",       "LCASE$",     false},
    {"OS.GETOS",    "OS.GETOS$",  false},
    {"STR",         "STR$",       false},
    {"TRIM",        "TRIM$",      false},
    {"UCASE",       "UCASE$",     false},
};

inline constexpr size_t kNameAliasCount = sizeof(kNameAliases) / sizeof(kNameAliases[0]);

constexpr bool name_aliases_sorted() {
    for (size_t i = 1; i < kNameAliasCount; ++i)
        if (!(std::string_view(kNameAliases[i - 1].old_name) <
              std::string_view(kNameAliases[i].old_name)))
            return false;
    return true;
}
static_assert(name_aliases_sorted(), "kNameAliases must be sorted by old_name");

// The alias for an upper-case name, or nullptr.
inline const NameAlias* jdb_name_alias(std::string_view upper) {
    const NameAlias* end = kNameAliases + kNameAliasCount;
    const NameAlias* it = std::lower_bound(kNameAliases, end, upper,
        [](const NameAlias& a, std::string_view n) { return std::string_view(a.old_name) < n; });
    return (it != end && upper == it->old_name) ? it : nullptr;
}

// One place a program used an old name.
struct AliasUse {
    std::string file;   // empty for the main program
    int line;
    std::string old_name;
    std::string new_name;
};
