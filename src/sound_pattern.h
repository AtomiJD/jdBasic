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

enum class Kind { REST, ATOM, SEQ, STACK, ALT, RAND };

struct Node;
using NodeP = std::shared_ptr<Node>;

struct Node {
    Kind kind = Kind::REST;
    std::string atom;
    std::vector<NodeP> kids;
    std::vector<double> weights;  // SEQ step weights
    double speed = 1.0;           // *N gives N, /N gives 1/N
    float prob = 1.0f;
    uint32_t seed = 0;            // RAND choice stream
};

struct Event {
    double start;  // cycle time of the onset
    double dur;    // in cycles
    std::string note;
    float prob;
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
        auto n = std::make_shared<Node>();
        n->atom = src.substr(b, pos - b);
        n->kind = (n->atom == ".") ? Kind::REST : Kind::ATOM;
        return n;
    }
};

inline NodeP parse(const std::string& s) { return Parser(s).parse(); }

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
            for (double k = std::ceil(a); k < b; k += 1.0)
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
    }
    if (n.prob < 1.0f)
        for (size_t i = first; i < out.size(); i++) out[i].prob *= n.prob;
}

// The events of one whole cycle, onsets relative to the cycle start, sorted.
inline std::vector<Event> query_cycle(const NodeP& n, int64_t cycle) {
    std::vector<Event> out;
    if (!n) return out;
    query(*n, (double)cycle, (double)cycle + 1.0, out);
    for (auto& e : out) e.start -= (double)cycle;
    std::stable_sort(out.begin(), out.end(),
                     [](const Event& x, const Event& y) { return x.start < y.start; });
    return out;
}

} // namespace jdpat
