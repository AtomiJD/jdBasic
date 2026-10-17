#pragma once
// Mini-notation patterns for the live-coding sequencer, a TidalCycles subset.
//
//   a b c      sequence, each step one equal slice of the cycle
//   [a b]      group into one step          a, b     stack (play together)
//   <a b c>    one per cycle, in turn       a | b    random choice per cycle
//   ~  .       rest                         _        hold the previous step
//   a*2  a/2   faster / slower              a!3  !   repeat as separate steps
//   a@3        step weight                  a?  a?0.3  play with probability
//   a(3,8,2)   euclidean rhythm, optional rotation
//
// Transforms wrap a pattern, the pattern always last:
//   rev(p) palindrome(p) degrade(p)       fast(2, p) slow(2, p) ply(2, p)
//   iter(4, p) add(12, p) degradeBy(0.3, p)  arp(up|down|updown|downup, p)
//   every(4, F, p) off(0.125, F, p) jux(F, p) juxBy(0.5, F, p)
//   sometimes(F, p) often rarely almostNever almostAlways sometimesBy(0.3, F, p)
//   gain(v, p) legato(v, p) pan(v, p)   v a number or a number pattern: [1 0.6]*4
//   swing(n, p) swingBy(x, n, p)        late second half of each of n slices
// F is a transform without its pattern: rev, add(7), fast(2), every(2, rev).
//
// A pattern is parsed once into a tree and queried one cycle at a time;
// the query returns the event onsets that fall inside that cycle.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace jdpat {

enum class Kind { REST, ATOM, SEQ, STACK, ALT, RAND, FN };

struct Node;
using NodeP = std::shared_ptr<Node>;

struct Node {
    Kind kind = Kind::REST;
    std::string atom;
    std::vector<NodeP> kids;
    std::vector<double> weights;  // SEQ step weights
    double speed = 1.0;           // *N gives N, /N gives 1/N
    float prob = 1.0f;
    uint32_t seed = 0;            // RAND choice stream, FN random stream
    // FN: transform name, its number arguments and arp mode. kids[0] is the
    // pattern, kids[1] the pattern with the function argument applied.
    std::string fn;
    std::vector<double> nums;
    std::string word;
    NodeP value;  // FN: a number pattern argument (gain, legato, pan)
};

struct Event {
    double start;  // cycle time of the onset
    double dur;    // in cycles
    std::string note;
    float prob;
    float pan = -1.0f;  // below 0: the track pan
    float gain = 1.0f;
    float legato = -1.0f;  // gate as a share of the event; below 0: the default
};

inline uint32_t hash32(uint32_t a, int64_t b) {
    uint64_t x = ((uint64_t)a << 32) ^ (uint64_t)b;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (uint32_t)x;
}

// Bjorklund's algorithm, in the order TidalCycles uses.
inline std::vector<bool> euclid(int k, int n, int rot) {
    std::vector<bool> out;
    if (n <= 0) return out;
    k = std::clamp(k, 0, n);
    std::vector<std::vector<bool>> x(k, std::vector<bool>{true});
    std::vector<std::vector<bool>> y(n - k, std::vector<bool>{false});
    while (std::min(x.size(), y.size()) > 1) {
        if (x.size() > y.size()) {
            size_t j = y.size();
            std::vector<std::vector<bool>> nx(x.begin(), x.begin() + j);
            for (size_t i = 0; i < j; i++) nx[i].insert(nx[i].end(), y[i].begin(), y[i].end());
            std::vector<std::vector<bool>> ny(x.begin() + j, x.end());
            x.swap(nx); y.swap(ny);
        } else {
            size_t i0 = x.size();
            for (size_t i = 0; i < i0; i++) x[i].insert(x[i].end(), y[i].begin(), y[i].end());
            y.erase(y.begin(), y.begin() + i0);
        }
    }
    for (auto& v : x) out.insert(out.end(), v.begin(), v.end());
    for (auto& v : y) out.insert(out.end(), v.begin(), v.end());
    std::vector<bool> r(n);
    int s = ((rot % n) + n) % n;
    for (int i = 0; i < n; i++) r[i] = out[(i + s) % n];
    return r;
}

// MIDI number of a note name like c4, c#4, eb3 (C4 = 60); -1 if it is none.
inline int note_midi(const std::string& s) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // a..g
    if (s.empty()) return -1;
    char l = (char)tolower((unsigned char)s[0]);
    if (l < 'a' || l > 'g') return -1;
    size_t i = 1;
    int acc = 0;
    if (i < s.size() && s[i] == '#') { acc = 1; i++; }
    else if (i < s.size() && (s[i] == 'b' || s[i] == 'B')) { acc = -1; i++; }
    if (i >= s.size() || i + 1 < s.size() || !isdigit((unsigned char)s[i])) return -1;
    return (s[i] - '0' + 1) * 12 + base[l - 'a'] + acc;
}

inline std::string midi_name(int m) {
    static const char* names[12] = {"c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"};
    m = std::clamp(m, 12, 119);
    return std::string(names[m % 12]) + std::to_string(m / 12 - 1);
}

// A note moved by d semitones; an integer (a scale degree) moved by d degrees.
inline std::string transpose(const std::string& s, double d) {
    long step = std::lround(d);
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!s.empty() && *end == '\0') return std::to_string(v + step);
    int m = note_midi(s);
    if (m < 0) return s;
    return midi_name(m + (int)step);
}

class Parser {
public:
    explicit Parser(const std::string& s) : src(s) {}

    NodeP parse() {
        NodeP n = parse_choice(0, false);
        skip_ws();
        if (pos < src.size())
            fail(std::string("unexpected '") + src[pos] + "'");
        return n;
    }

private:
    const std::string& src;
    size_t pos = 0;
    uint32_t next_seed = 1;

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("pattern: " + msg + " at position " + std::to_string(pos));
    }
    void skip_ws() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
                                    src[pos] == '\n' || src[pos] == '\r')) pos++;
    }
    bool at(char c) { skip_ws(); return pos < src.size() && src[pos] == c; }
    static bool word_char(char c) {
        return !(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '[' || c == ']' ||
                 c == '<' || c == '>' || c == ',' || c == '|' || c == '*' || c == '/' ||
                 c == '!' || c == '@' || c == '?' || c == '(' || c == ')' || c == '~' ||
                 c == '_');
    }
    double number() {
        skip_ws();
        size_t b = pos;
        if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) pos++;
        while (pos < src.size() && (isdigit((unsigned char)src[pos]) || src[pos] == '.')) pos++;
        if (b == pos) fail("number expected");
        return std::stod(src.substr(b, pos - b));
    }
    bool number_follows() {
        return pos < src.size() && (isdigit((unsigned char)src[pos]) || src[pos] == '.');
    }

    // choice := stack ('|' stack)*      stack := steps (',' steps)*
    // Inside <...> each step of a sequence becomes one alternative.
    NodeP parse_choice(char close, bool alt) {
        std::vector<NodeP> options;
        options.push_back(parse_stack(close, alt));
        while (at('|')) { pos++; options.push_back(parse_stack(close, alt)); }
        if (options.size() == 1) return options[0];
        auto n = std::make_shared<Node>();
        n->kind = Kind::RAND; n->kids = options; n->seed = next_seed++;
        return n;
    }
    NodeP parse_stack(char close, bool alt) {
        std::vector<NodeP> layers;
        layers.push_back(parse_steps(close, alt));
        while (at(',')) { pos++; layers.push_back(parse_steps(close, alt)); }
        if (layers.size() == 1) return layers[0];
        auto n = std::make_shared<Node>();
        n->kind = Kind::STACK; n->kids = layers;
        return n;
    }
    NodeP parse_steps(char close, bool alt) {
        std::vector<NodeP> steps;
        std::vector<double> weights;
        while (true) {
            skip_ws();
            if (pos >= src.size()) {
                if (close) fail(std::string("missing '") + close + "'");
                break;
            }
            char c = src[pos];
            if (c == ',' || c == '|') break;
            if (c == close) break;
            if (c == ']' || c == '>' || c == ')') fail(std::string("unexpected '") + c + "'");
            if (c == '_') {
                pos++;
                if (weights.empty()) fail("'_' needs a step before it");
                weights.back() += 1.0;
                continue;
            }
            if (c == '!' ) {
                pos++;
                if (steps.empty()) fail("'!' needs a step before it");
                steps.push_back(steps.back()); weights.push_back(weights.back());
                continue;
            }
            double w = 1.0;
            int reps = 1;
            NodeP s = parse_step(w, reps);
            for (int r = 0; r < reps; r++) { steps.push_back(s); weights.push_back(w); }
        }
        auto n = std::make_shared<Node>();
        if (steps.empty()) return n;
        if (alt) {
            n->kind = Kind::ALT; n->kids = steps;
            return n;
        }
        if (steps.size() == 1 && weights[0] == 1.0) return steps[0];
        n->kind = Kind::SEQ; n->kids = steps; n->weights = weights;
        return n;
    }

    // A copy, so a modifier never changes a node that `!` shares.
    NodeP wrap(NodeP inner) { return std::make_shared<Node>(*inner); }

    NodeP parse_step(double& weight, int& reps) {
        NodeP n = parse_term();
        while (pos < src.size()) {
            char c = src[pos];
            if (c == '*' || c == '/') {
                pos++;
                double f = number();
                if (f <= 0) fail("speed factor must be positive");
                n = wrap(n);
                if (c == '*') n->speed *= f; else n->speed /= f;
                n = group(n);
            } else if (c == '!' && pos + 1 < src.size() && isdigit((unsigned char)src[pos + 1])) {
                pos++;
                reps = std::max(1, (int)number());
            } else if (c == '@') {
                pos++;
                weight = number();
                if (weight <= 0) fail("weight must be positive");
            } else if (c == '?') {
                pos++;
                float p = 0.5f;
                if (number_follows()) p = (float)number();
                n = wrap(n); n->prob *= p;
            } else if (c == '(') {
                pos++;
                int k = (int)number();
                if (!at(',')) fail("',' expected in euclid");
                pos++;
                int len = (int)number();
                int rot = 0;
                if (at(',')) { pos++; rot = (int)number(); }
                if (!at(')')) fail("')' expected in euclid");
                pos++;
                if (len <= 0) fail("euclid needs at least one step");
                auto e = std::make_shared<Node>();
                e->kind = Kind::SEQ;
                auto rest = std::make_shared<Node>();
                for (bool hit : euclid(k, len, rot)) {
                    e->kids.push_back(hit ? n : rest);
                    e->weights.push_back(1.0);
                }
                n = e;
            } else {
                break;
            }
        }
        return n;
    }

    // Speed and probability sit on the node itself; a following modifier
    // applies to that result, so it gets a fresh group around it.
    NodeP group(NodeP inner) {
        auto g = std::make_shared<Node>();
        g->kind = Kind::SEQ; g->kids = {inner}; g->weights = {1.0};
        return g;
    }

    NodeP parse_term() {
        skip_ws();
        if (pos >= src.size()) fail("step expected");
        char c = src[pos];
        if (c == '[') {
            pos++;
            NodeP n = parse_choice(']', false);
            pos++;
            return group(n);
        }
        if (c == '<') {
            pos++;
            NodeP n = parse_choice('>', true);
            pos++;
            return group(n);
        }
        if (c == '~') { pos++; return std::make_shared<Node>(); }
        size_t b = pos;
        while (pos < src.size() && word_char(src[pos])) pos++;
        if (b == pos) fail(std::string("unexpected '") + c + "'");
        std::string word = src.substr(b, pos - b);
        if (pos < src.size() && src[pos] == '(' && fn_sig(lower(word)))
            return parse_fn(lower(word), true);
        auto n = std::make_shared<Node>();
        n->atom = word;
        n->kind = (n->atom == ".") ? Kind::REST : Kind::ATOM;
        return n;
    }

    static std::string lower(std::string s) {
        for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    }

    // The arguments a transform takes before its pattern:
    // n a number, f a function, m an arp mode, v a number pattern.
    // nullptr for no transform.
    static const char* fn_sig(const std::string& name) {
        static const std::pair<const char*, const char*> table[] = {
            {"rev", ""}, {"palindrome", ""}, {"degrade", ""},
            {"fast", "n"}, {"slow", "n"}, {"ply", "n"}, {"iter", "n"}, {"add", "n"},
            {"degradeby", "n"}, {"every", "nf"}, {"off", "nf"}, {"sometimesby", "nf"},
            {"juxby", "nf"}, {"jux", "f"}, {"sometimes", "f"}, {"often", "f"},
            {"rarely", "f"}, {"almostnever", "f"}, {"almostalways", "f"}, {"arp", "m"},
            {"gain", "v"}, {"velocity", "v"}, {"legato", "v"}, {"clip", "v"}, {"pan", "v"},
            {"swing", "n"}, {"swingby", "nn"},
        };
        for (auto& e : table)
            if (name == e.first) return e.second;
        return nullptr;
    }

    std::string name_at() {
        skip_ws();
        size_t b = pos;
        while (pos < src.size() && isalpha((unsigned char)src[pos])) pos++;
        return lower(src.substr(b, pos - b));
    }

    // name(args..., pattern). Without the pattern the result is a template
    // that apply() later completes, the form a function argument takes.
    NodeP parse_fn(const std::string& name, bool with_pattern) {
        const char* sig = fn_sig(name);
        auto t = std::make_shared<Node>();
        t->kind = Kind::FN;
        t->fn = name;
        bool open = at('(');
        if (open) pos++;
        if (*sig && !open) fail("'(' expected after " + name);
        for (const char* s = sig; *s; s++) {
            if (s != sig) {
                if (!at(',')) fail("',' expected in " + name);
                pos++;
            }
            if (*s == 'n') {
                t->nums.push_back(number());
            } else if (*s == 'v') {
                t->value = parse_steps(')', false);
            } else if (*s == 'm') {
                t->word = name_at();
                if (t->word != "up" && t->word != "down" && t->word != "updown" && t->word != "downup")
                    fail("arp mode is up, down, updown or downup");
            } else {
                std::string f = name_at();
                if (!fn_sig(f)) fail("transform expected in " + name);
                t->kids.push_back(parse_fn(f, false));
            }
        }
        if (!with_pattern) {
            if (open) {
                if (!at(')')) fail("')' expected after " + name);
                pos++;
            }
            return t;
        }
        if (*sig) {
            if (!at(',')) fail("pattern expected in " + name);
            pos++;
        }
        NodeP p = parse_choice(')', false);
        pos++;
        return apply(t, p);
    }

    NodeP apply(const NodeP& t, NodeP p) {
        const std::string& fn = t->fn;
        if (fn == "fast" || fn == "slow") {
            double x = t->nums[0];
            if (x <= 0) fail(fn + " needs a positive factor");
            NodeP w = wrap(p);
            w->speed *= (fn == "fast") ? x : 1.0 / x;
            return group(w);
        }
        auto n = std::make_shared<Node>();
        n->kind = Kind::FN;
        n->fn = fn;
        n->nums = t->nums;
        n->word = t->word;
        n->value = t->value;
        n->seed = next_seed++;
        if (fn == "velocity") n->fn = "gain";
        if (fn == "clip") n->fn = "legato";
        if (fn == "swing") { n->fn = "swingby"; n->nums = {1.0 / 3.0, t->nums[0]}; }
        if (n->fn == "swingby" && n->nums[1] < 1) fail("swing needs at least 1 slice");
        static const std::pair<const char*, double> chance[] = {
            {"sometimes", 0.5}, {"often", 0.75}, {"rarely", 0.25},
            {"almostnever", 0.1}, {"almostalways", 0.9},
        };
        for (auto& c : chance)
            if (fn == c.first) { n->fn = "sometimesby"; n->nums = {c.second}; }
        if (fn == "degrade") { n->fn = "degradeby"; n->nums = {0.5}; }
        if (fn == "jux") { n->fn = "juxby"; n->nums = {1.0}; }
        if ((n->fn == "every" || n->fn == "ply" || n->fn == "iter") && n->nums[0] < 1)
            fail(fn + " needs a count of at least 1");
        n->kids.push_back(p);
        if (!t->kids.empty()) n->kids.push_back(apply(t->kids[0], p));
        return n;
    }
};

inline NodeP parse(const std::string& s) { return Parser(s).parse(); }

inline void query(const Node& n, double a, double b, std::vector<Event>& out);

// 0..1, fixed for a node's random stream and an onset time
inline double event_rand(uint32_t seed, double start) {
    return hash32(seed, (int64_t)std::llround(start * 1048576.0)) / 4294967296.0;
}

// Cycle k of `p` played backwards
inline void reversed_cycle(const Node& p, double k, std::vector<Event>& out) {
    size_t first = out.size();
    query(p, k, k + 1.0, out);
    for (size_t i = first; i < out.size(); i++) {
        double end = std::min(out[i].start + out[i].dur, k + 1.0);
        out[i].dur = end - out[i].start;
        out[i].start = 2.0 * k + 1.0 - end;
    }
}

// The number a value pattern holds at time t; false where it has none
inline bool value_at(const Node& v, double t, double& out) {
    std::vector<Event> evs;
    double k = std::floor(t + 1e-9);
    query(v, k - 1.0, k + 1.0, evs);
    for (auto it = evs.rbegin(); it != evs.rend(); ++it) {
        if (it->start <= t + 1e-9 && t < it->start + it->dur - 1e-9) {
            char* end = nullptr;
            double x = std::strtod(it->note.c_str(), &end);
            if (end == it->note.c_str() || *end != '\0') return false;
            out = x;
            return true;
        }
    }
    return false;
}

inline void query_fn(const Node& n, double a, double b, std::vector<Event>& out) {
    const Node& p = *n.kids[0];
    const std::string& fn = n.fn;
    if (fn == "gain" || fn == "legato" || fn == "pan") {
        size_t first = out.size();
        query(p, a, b, out);
        for (size_t i = first; i < out.size(); i++) {
            double x;
            if (!value_at(*n.value, out[i].start, x)) continue;
            if (fn == "gain") out[i].gain *= (float)std::max(0.0, x);
            else if (fn == "legato") out[i].legato = (float)std::max(0.0, x);
            else out[i].pan = (float)std::clamp(x, 0.0, 1.0);
        }
        return;
    }
    if (fn == "swingby") {
        // Within each of n slices the first half plays as written; the second
        // half plays the pattern x slices late (TidalCycles swingBy).
        double x = n.nums[0], slices = n.nums[1], shift = x / slices;
        auto late_half = [&](double t) {
            double s = (t - std::floor(t + 1e-9)) * slices;
            return s - std::floor(s + 1e-9) >= 0.5 - 1e-9;
        };
        std::vector<Event> evs;
        query(p, a, b, evs);
        for (auto& e : evs)
            if (!late_half(e.start)) out.push_back(e);
        evs.clear();
        query(p, a - shift, b - shift, evs);
        for (auto& e : evs) {
            e.start += shift;
            if (late_half(e.start)) out.push_back(e);
        }
        return;
    }
    auto keep = [&](std::vector<Event>& evs) {
        for (auto& e : evs)
            if (e.start >= a - 1e-9 && e.start < b - 1e-9) out.push_back(e);
    };
    if (fn == "every") {
        int64_t cnt = (int64_t)n.nums[0];
        for (double k = std::floor(a); k < b; k += 1.0) {
            int64_t ki = (int64_t)k;
            const Node& src = (((ki % cnt) + cnt) % cnt == 0) ? *n.kids[1] : p;
            query(src, std::max(a, k), std::min(b, k + 1.0), out);
        }
    } else if (fn == "off") {
        double t = n.nums[0];
        query(p, a, b, out);
        size_t first = out.size();
        query(*n.kids[1], a - t, b - t, out);
        for (size_t i = first; i < out.size(); i++) out[i].start += t;
    } else if (fn == "juxby") {
        float w = (float)n.nums[0];
        size_t first = out.size();
        query(p, a, b, out);
        size_t mid = out.size();
        query(*n.kids[1], a, b, out);
        for (size_t i = first; i < out.size(); i++)
            out[i].pan = std::clamp(i < mid ? 0.5f - w / 2 : 0.5f + w / 2, 0.0f, 1.0f);
    } else if (fn == "sometimesby" || fn == "degradeby") {
        double pr = n.nums[0];
        std::vector<Event> evs;
        query(p, a, b, evs);
        for (auto& e : evs)
            if (event_rand(n.seed, e.start) >= pr) out.push_back(e);
        if (fn == "sometimesby") {
            evs.clear();
            query(*n.kids[1], a, b, evs);
            for (auto& e : evs)
                if (event_rand(n.seed, e.start) < pr) out.push_back(e);
        }
    } else if (fn == "add") {
        size_t first = out.size();
        query(p, a, b, out);
        for (size_t i = first; i < out.size(); i++) out[i].note = transpose(out[i].note, n.nums[0]);
    } else if (fn == "rev" || fn == "palindrome") {
        for (double k = std::floor(a); k < b; k += 1.0) {
            std::vector<Event> evs;
            if (fn == "rev" || ((int64_t)k & 1)) reversed_cycle(p, k, evs);
            else query(p, k, k + 1.0, evs);
            keep(evs);
        }
    } else if (fn == "iter") {
        double cnt = std::floor(n.nums[0]);
        for (double k = std::floor(a); k < b; k += 1.0) {
            double s = std::fmod(std::fmod(k, cnt) + cnt, cnt) / cnt;
            std::vector<Event> evs;
            query(p, k + s, k + 1.0 + s, evs);
            for (auto& e : evs) e.start -= s;
            keep(evs);
        }
    } else if (fn == "ply" || fn == "arp") {
        // whole cycles, one before the window too: its events may spread into it
        for (double k = std::floor(a) - 1.0; k < b; k += 1.0) {
            std::vector<Event> evs, spread;
            query(p, k, k + 1.0, evs);
            std::stable_sort(evs.begin(), evs.end(),
                             [](const Event& x, const Event& y) { return x.start < y.start; });
            for (size_t i = 0; i < evs.size();) {
                size_t j = i + 1;
                if (fn == "arp")
                    while (j < evs.size() && std::abs(evs[j].start - evs[i].start) < 1e-9) j++;
                std::vector<Event> grp(evs.begin() + i, evs.begin() + j);
                if (fn == "ply") {
                    int m = (int)n.nums[0];
                    for (int r = 1; r < m; r++) grp.push_back(grp[0]);
                } else {
                    std::vector<Event> down(grp.rbegin(), grp.rend());
                    if (n.word == "down") grp = down;
                    else if (n.word == "updown" && grp.size() > 2) grp.insert(grp.end(), down.begin() + 1, down.end() - 1);
                    else if (n.word == "downup" && grp.size() > 2) {
                        std::vector<Event> up = grp;
                        grp = down;
                        grp.insert(grp.end(), up.begin() + 1, up.end() - 1);
                    }
                }
                double d = evs[i].dur / grp.size();
                for (size_t r = 0; r < grp.size(); r++) {
                    grp[r].start = evs[i].start + r * d;
                    grp[r].dur = d;
                    spread.push_back(grp[r]);
                }
                i = j;
            }
            keep(spread);
        }
    }
}

// Events of `n` whose onset lies in [a, b), in the node's own time.
inline void query(const Node& n, double a, double b, std::vector<Event>& out) {
    if (b <= a) return;
    if (n.speed != 1.0) {
        Node inner = n;
        inner.speed = 1.0;
        size_t first = out.size();
        query(inner, a * n.speed, b * n.speed, out);
        for (size_t i = first; i < out.size(); i++) {
            out[i].start /= n.speed;
            out[i].dur /= n.speed;
        }
        return;
    }
    size_t first = out.size();
    switch (n.kind) {
        case Kind::REST: break;
        case Kind::ATOM:
            // the tolerance absorbs rounding in the window a SEQ maps here
            for (double k = std::ceil(a - 1e-9); k < b - 1e-9; k += 1.0)
                out.push_back({k, 1.0, n.atom, 1.0f});
            break;
        case Kind::STACK:
            for (auto& kid : n.kids) query(*kid, a, b, out);
            break;
        case Kind::SEQ: {
            double total = 0;
            for (double w : n.weights) total += w;
            for (double k = std::floor(a); k < b; k += 1.0) {
                double off = 0;
                for (size_t i = 0; i < n.kids.size(); i++) {
                    double w = n.weights[i];
                    double ss = k + off / total, se = k + (off + w) / total;
                    off += w;
                    double sa = std::max(a, ss), sb = std::min(b, se);
                    if (sb <= sa) continue;
                    double scale = total / w;
                    size_t f = out.size();
                    query(*n.kids[i], k + (sa - ss) * scale, k + (sb - ss) * scale, out);
                    for (size_t j = f; j < out.size(); j++) {
                        out[j].start = ss + (out[j].start - k) / scale;
                        out[j].dur /= scale;
                    }
                }
            }
            break;
        }
        case Kind::ALT: {
            int64_t cnt = (int64_t)n.kids.size();
            for (double k = std::floor(a); k < b; k += 1.0) {
                int64_t ki = (int64_t)k;
                int64_t j = ((ki % cnt) + cnt) % cnt;
                int64_t turn = (ki - j) / cnt;
                double shift = (double)turn - k;
                double sa = std::max(a, k), sb = std::min(b, k + 1.0);
                size_t f = out.size();
                query(*n.kids[(size_t)j], sa + shift, sb + shift, out);
                for (size_t e = f; e < out.size(); e++) out[e].start -= shift;
            }
            break;
        }
        case Kind::RAND: {
            for (double k = std::floor(a); k < b; k += 1.0) {
                uint32_t j = hash32(n.seed, (int64_t)k) % (uint32_t)n.kids.size();
                query(*n.kids[j], std::max(a, k), std::min(b, k + 1.0), out);
            }
            break;
        }
        case Kind::FN:
            query_fn(n, a, b, out);
            break;
    }
    if (n.prob < 1.0f)
        for (size_t i = first; i < out.size(); i++) out[i].prob *= n.prob;
}

// The events of one whole cycle, onsets relative to the cycle start, sorted.
inline std::vector<Event> query_cycle(const NodeP& n, int64_t cycle) {
    std::vector<Event> out;
    if (!n) return out;
    query(*n, (double)cycle, (double)cycle + 1.0, out);
    for (auto& e : out) e.start = std::max(0.0, e.start - (double)cycle);
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Event& e) { return e.start >= 1.0 - 1e-9; }),
              out.end());
    std::stable_sort(out.begin(), out.end(),
                     [](const Event& x, const Event& y) { return x.start < y.start; });
    return out;
}

} // namespace jdpat
