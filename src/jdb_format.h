// jdb_format.h - the FORMAT$ placeholder language both runtimes render.
//
// A format text holds placeholders {}, {2} or {:>8.2f}; {{ and }} stand for
// a brace. A placeholder's spec reads
//     [[fill]align][sign][#][0][width][.precision][d|f|e|E|g|G|x|X|s|%]
// and is rendered here from a value the caller has already turned into a
// number or a string. The VM (vm_builtins_string.cpp) and the native
// runtime (jdb_runtime.cpp) include this and keep only their own argument
// plumbing.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace jdb_fmt {

struct Spec {
    char fill = ' ';
    char align = 0;      // '<', '>', '^', or 0 for the default by kind
    char sign = 0;       // '+', '-', ' ', or 0
    bool hash = false;   // 0x in front of a hex number
    bool zero = false;   // zeros after the sign up to the width
    int width = 0;
    int prec = -1;
    char type = 0;       // one of d f e E g G x X s %, or 0
};

// The spec text between the colon and the closing brace, parsed. False for
// a spec the language does not have.
inline bool parse_spec(const char* spec, Spec& out) {
    out = Spec{};
    if (!spec) return true;
    if (*spec == ':') spec++;
    int n = (int)std::strlen(spec);
    int p = 0;
    if (n >= 2 && (spec[1] == '<' || spec[1] == '>' || spec[1] == '^')) {
        out.fill = spec[0]; out.align = spec[1]; p = 2;
    } else if (n >= 1 && (spec[0] == '<' || spec[0] == '>' || spec[0] == '^')) {
        out.align = spec[0]; p = 1;
    }
    if (p < n && (spec[p] == '+' || spec[p] == '-' || spec[p] == ' ')) out.sign = spec[p++];
    if (p < n && spec[p] == '#') { out.hash = true; p++; }
    if (p < n && spec[p] == '0') { out.zero = true; p++; }
    while (p < n && spec[p] >= '0' && spec[p] <= '9') out.width = out.width * 10 + (spec[p++] - '0');
    if (p < n && spec[p] == '.') {
        p++; out.prec = 0;
        while (p < n && spec[p] >= '0' && spec[p] <= '9') out.prec = out.prec * 10 + (spec[p++] - '0');
    }
    if (p < n && spec[p] == '#') { out.hash = true; p++; }
    out.type = p < n ? spec[p] : 0;
    bool known = out.type == 0 || std::strchr("dfeEgGxXs%", out.type) != nullptr;
    return known && p + (out.type ? 1 : 0) == n;
}

// The message for a spec parse_spec refused.
inline std::string spec_error(const char* spec) {
    return std::string("FORMAT$: unknown format spec \"{") + (spec ? spec : "") +
           "}\"; use [[fill]align][sign][#][0][width][.precision][d|f|e|g|x|X|s|%]";
}

// Whether the spec asks for a number: a string argument is read as one then.
inline bool wants_number(const Spec& s) {
    return s.type == 'd' || s.type == 'f' || s.type == 'e' || s.type == 'E' ||
           s.type == 'g' || s.type == 'G' || s.type == 'x' || s.type == 'X' || s.type == '%';
}

// A number rendered by the spec's type, before sign, padding and width.
// plain renders a number the way PRINT would when the spec names no type
// and no precision, which is each runtime's own business.
inline std::string number_text(const Spec& s, double val, int64_t ival, const std::string& plain) {
    char buf[320];
    switch (s.type) {
    case 'd':
        std::snprintf(buf, sizeof(buf), "%lld", (long long)ival);
        return buf;
    case 'f':
        std::snprintf(buf, sizeof(buf), "%.*f", s.prec >= 0 ? s.prec : 6, val);
        return buf;
    case 'e': case 'E': case 'g': case 'G': {
        char cfmt[8] = {'%', '.', '*', s.type, 0};
        std::snprintf(buf, sizeof(buf), cfmt, s.prec >= 0 ? s.prec : 6, val);
        return buf;
    }
    case '%':
        std::snprintf(buf, sizeof(buf), "%.*f%%", s.prec >= 0 ? s.prec : 6, val * 100.0);
        return buf;
    case 'x':
        std::snprintf(buf, sizeof(buf), s.hash ? "0x%llx" : "%llx", (long long)ival);
        return buf;
    case 'X':
        std::snprintf(buf, sizeof(buf), s.hash ? "0x%llX" : "%llX", (long long)ival);
        return buf;
    default:
        if (s.prec >= 0) {
            std::snprintf(buf, sizeof(buf), "%.*f", s.prec, val);
            return buf;
        }
        return plain;
    }
}

// The rendered text laid into its field: the sign of a number, zeros after
// it up to the width, then fill and alignment. Text aligns right unless the
// spec says otherwise.
inline std::string lay_out(Spec s, std::string text, bool is_number) {
    if (is_number && (s.sign == '+' || s.sign == ' ') && !text.empty() && text[0] != '-')
        text.insert(text.begin(), s.sign);
    if (s.zero && s.width > 0 && (int)text.size() < s.width) {
        if (!s.align && is_number) {
            size_t at = (!text.empty() && (text[0] == '+' || text[0] == '-' || text[0] == ' ')) ? 1 : 0;
            if (text.compare(at, 2, "0x") == 0) at += 2;
            text.insert(at, std::string(s.width - text.size(), '0'));
        } else if (!s.align || s.fill == ' ') {
            s.fill = '0';
        }
    }
    if (s.width > 0 && (int)text.size() < s.width) {
        int pad = s.width - (int)text.size();
        char align = s.align ? s.align : '>';
        if (align == '<') text += std::string(pad, s.fill);
        else if (align == '>') text = std::string(pad, s.fill) + text;
        else { int l = pad / 2; text = std::string(l, s.fill) + text + std::string(pad - l, s.fill); }
    }
    return text;
}

// Walks the format text and hands every placeholder to render(index, spec,
// out), where index is the argument the placeholder names or the next one
// in turn and spec is the text after the colon (empty for a bare {});
// render answers false to stop. The text outside the placeholders and the
// braces {{ }} stand for go to out as they are.
template <typename Render>
inline bool expand(const char* fmt, std::string& out, Render render) {
    int next = 0;
    for (const char* p = fmt; *p;) {
        if (*p == '{') {
            if (p[1] == '{') { out += '{'; p += 2; continue; }
            const char* end = std::strchr(p, '}');
            if (!end) { out += *p++; continue; }
            std::string spec(p + 1, (size_t)(end - p - 1));
            size_t i = 0;
            while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') i++;
            int index;
            if (i > 0 && (i == spec.size() || spec[i] == ':')) {
                index = std::atoi(spec.c_str());
                spec = i < spec.size() ? spec.substr(i + 1) : std::string();
            } else {
                index = next++;
                if (!spec.empty() && spec[0] == ':') spec.erase(0, 1);
            }
            if (!render(index, spec, out)) return false;
            p = end + 1;
        } else if (*p == '}' && p[1] == '}') {
            out += '}'; p += 2;
        } else {
            out += *p++;
        }
    }
    return true;
}

}  // namespace jdb_fmt
