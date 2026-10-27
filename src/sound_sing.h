// sound_sing.h - the singing voice of a synth track.
//
// A lyric is a queue of syllables; every note the track plays sings the next
// one. A syllable is written in a small phonetic spelling ("nee", "on",
// "lait", "tu.nait") and becomes a run of sounds: the consonants before the
// vowel, the vowel (or a glide of two, "ai"), the consonants after it. The
// voice is a vocoder: the track's own oscillator and a noise source each run
// through a bank of band-passes, and every band opens as far as the spectrum
// of the sound being sung says, moving smoothly from one sound to the next.
// Voiced sounds shape the oscillator, hissing sounds the noise.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace jdsing {

constexpr int NB = 28;        // bands, 100 Hz to 9 kHz, each two band-passes deep
constexpr int MAX_PH = 12;    // sounds in one syllable
constexpr float RATE = 44100.0f;
constexpr float MAKEUP = 3.0f;

enum Kind : uint8_t { VOWEL, NASAL, GLIDE, VFRIC, FRIC, ASPIR, PLOSIVE };

struct Phone {
    const char* name;
    Kind kind;
    float f1, f2, f3, a1, a2, a3;  // formants of the voiced part, Hz and level
    float voiced, noise;           // levels of the oscillator and noise parts
    float nf, nbw;                 // noise centre in Hz and width in octaves
    float ms;                      // length as a consonant
};

static const Phone PHONES[] = {
    {"a",  VOWEL, 850, 1220, 2810, 1.0f, 0.50f, 0.25f, 1.0f, 0.0f, 0, 0, 0},
    {"e",  VOWEL, 610, 1900, 2600, 1.0f, 0.45f, 0.25f, 1.0f, 0.0f, 0, 0, 0},
    {"i",  VOWEL, 310, 2790, 3310, 1.0f, 0.30f, 0.20f, 1.0f, 0.0f, 0, 0, 0},
    {"o",  VOWEL, 500,  900, 2800, 1.0f, 0.45f, 0.12f, 1.0f, 0.0f, 0, 0, 0},
    {"u",  VOWEL, 350,  800, 2700, 1.0f, 0.25f, 0.06f, 1.0f, 0.0f, 0, 0, 0},
    {"ae", VOWEL, 860, 2050, 2850, 1.0f, 0.50f, 0.25f, 1.0f, 0.0f, 0, 0, 0},
    {"uh", VOWEL, 760, 1400, 2780, 1.0f, 0.45f, 0.20f, 1.0f, 0.0f, 0, 0, 0},
    {"ih", VOWEL, 430, 2480, 3070, 1.0f, 0.35f, 0.20f, 1.0f, 0.0f, 0, 0, 0},
    {"er", VOWEL, 500, 1350, 1700, 1.0f, 0.50f, 0.40f, 1.0f, 0.0f, 0, 0, 0},
    {"aw", VOWEL, 600,  900, 2700, 1.0f, 0.50f, 0.12f, 1.0f, 0.0f, 0, 0, 0},
    {"m",  NASAL, 280, 1100, 2300, 1.0f, 0.08f, 0.03f, 0.7f, 0.0f, 0, 0, 70},
    {"n",  NASAL, 280, 1700, 2600, 1.0f, 0.10f, 0.04f, 0.7f, 0.0f, 0, 0, 65},
    {"ng", NASAL, 280, 2300, 2800, 1.0f, 0.10f, 0.04f, 0.7f, 0.0f, 0, 0, 70},
    {"l",  GLIDE, 380, 1100, 2900, 1.0f, 0.25f, 0.10f, 0.85f, 0.0f, 0, 0, 60},
    {"r",  GLIDE, 430, 1300, 1650, 1.0f, 0.40f, 0.35f, 0.85f, 0.0f, 0, 0, 60},
    {"w",  GLIDE, 330,  700, 2300, 1.0f, 0.25f, 0.05f, 0.8f, 0.0f, 0, 0, 55},
    {"y",  GLIDE, 300, 2300, 3000, 1.0f, 0.35f, 0.15f, 0.8f, 0.0f, 0, 0, 55},
    {"v",  VFRIC, 300, 1400, 2400, 1.0f, 0.10f, 0.05f, 0.45f, 0.25f, 5000, 1.2f, 70},
    {"dh", VFRIC, 300, 1400, 2400, 1.0f, 0.10f, 0.05f, 0.45f, 0.20f, 4000, 1.2f, 60},
    {"z",  VFRIC, 300, 1400, 2400, 1.0f, 0.10f, 0.05f, 0.40f, 0.55f, 6500, 0.6f, 80},
    {"zh", VFRIC, 300, 1400, 2400, 1.0f, 0.10f, 0.05f, 0.40f, 0.50f, 3000, 0.7f, 80},
    {"f",  FRIC,  0, 0, 0, 0, 0, 0, 0.0f, 0.35f, 5500, 1.4f, 90},
    {"th", FRIC,  0, 0, 0, 0, 0, 0, 0.0f, 0.25f, 5000, 1.4f, 80},
    {"s",  FRIC,  0, 0, 0, 0, 0, 0, 0.0f, 0.70f, 7000, 0.5f, 100},
    {"sh", FRIC,  0, 0, 0, 0, 0, 0, 0.0f, 0.60f, 3000, 0.6f, 100},
    {"h",  ASPIR, 0, 0, 0, 0, 0, 0, 0.0f, 0.35f, 0, 0, 60},
    {"b",  PLOSIVE, 250, 900, 2400, 1.0f, 0.05f, 0.02f, 0.30f, 0.50f, 1000, 1.0f, 60},
    {"d",  PLOSIVE, 250, 900, 2400, 1.0f, 0.05f, 0.02f, 0.30f, 0.50f, 4000, 0.9f, 60},
    {"g",  PLOSIVE, 250, 900, 2400, 1.0f, 0.05f, 0.02f, 0.30f, 0.50f, 2000, 0.7f, 65},
    {"p",  PLOSIVE, 0, 0, 0, 0, 0, 0, 0.0f, 0.60f, 1000, 1.0f, 75},
    {"t",  PLOSIVE, 0, 0, 0, 0, 0, 0, 0.0f, 0.60f, 4500, 0.8f, 70},
    {"k",  PLOSIVE, 0, 0, 0, 0, 0, 0, 0.0f, 0.60f, 2200, 0.7f, 75},
};
constexpr int NPH = (int)(sizeof(PHONES) / sizeof(PHONES[0]));

// Spellings and the sounds they stand for; the longest match wins
struct Spelling { const char* text; const char* sounds[2]; };
static const Spelling SPELLINGS[] = {
    {"ee", {"i", nullptr}}, {"oo", {"u", nullptr}}, {"ah", {"a", nullptr}},
    {"eh", {"e", nullptr}}, {"oh", {"o", nullptr}},
    {"ai", {"a", "ih"}}, {"ei", {"e", "ih"}}, {"oi", {"aw", "ih"}},
    {"au", {"a", "u"}}, {"ou", {"o", "u"}},
    {"ch", {"t", "sh"}}, {"j", {"d", "zh"}}, {"x", {"k", "s"}},
    {"c", {"k", nullptr}}, {"q", {"k", nullptr}},
};

struct Syllable {
    uint8_t ph[MAX_PH] = {};
    uint8_t n = 0;
    uint8_t nuc0 = 0, nuc1 = 0;  // the vowels: ph[nuc0] up to ph[nuc1]
};

struct Tables {
    float centre[NB];
    float g[NB], k[NB];
    float voiced[NPH][NB], noise[NPH][NB];
    float smooth_v, smooth_n;
    Tables() {
        for (int b = 0; b < NB; b++) {
            centre[b] = 100.0f * std::pow(9000.0f / 100.0f, (float)b / (NB - 1));
            g[b] = std::tan(3.14159265f * centre[b] / RATE);
            k[b] = 1.0f / 5.0f;
        }
        for (int p = 0; p < NPH; p++) {
            const Phone& ph = PHONES[p];
            const float fs[3] = {ph.f1, ph.f2, ph.f3}, as[3] = {ph.a1, ph.a2, ph.a3};
            for (int b = 0; b < NB; b++) {
                float f = centre[b], v = 0.0f, nz = 0.0f;
                if (ph.voiced > 0.0f) {
                    for (int i = 0; i < 3; i++) {
                        float bw = 50.0f + 0.06f * fs[i];
                        float d = (f - fs[i]) / bw;
                        v += as[i] / (1.0f + d * d);
                    }
                    // an oscillator's harmonics thin out upwards, so the upper
                    // bands open further, as the pre-emphasis of a vocoder does
                    float tilt = std::clamp(std::sqrt(f / 400.0f), 0.7f, 3.5f);
                    v = std::min(1.0f, v + 0.008f) * ph.voiced * tilt;
                }
                if (ph.noise > 0.0f && ph.nf > 0.0f) {
                    float o = std::log2(f / ph.nf) / ph.nbw;
                    nz = ph.noise * std::exp(-0.5f * o * o);
                }
                voiced[p][b] = v;
                noise[p][b] = nz;
            }
        }
        // voiced sounds glide into each other as the mouth moves, noise
        // starts and stops at once
        smooth_v = 1.0f - std::exp(-1.0f / (0.012f * RATE));
        smooth_n = 1.0f - std::exp(-1.0f / (0.003f * RATE));
    }
};

inline const Tables& tables() {
    static const Tables t;
    return t;
}

inline int phone_index(const std::string& name) {
    for (int i = 0; i < NPH; i++)
        if (name == PHONES[i].name) return i;
    return -1;
}

inline bool is_vowel(int p) { return PHONES[p].kind == VOWEL; }

// The sounds of one written syllable, longest spelling first
inline Syllable parse_syllable(const std::string& word) {
    Syllable s;
    std::string w;
    for (char c : word) {
        if (c == '.' || c == '\'') { w += '.'; continue; }
        w += (char)std::tolower((unsigned char)c);
    }
    auto push = [&](int p) {
        if (s.n >= MAX_PH) throw std::runtime_error("syllable '" + word + "' has too many sounds");
        s.ph[s.n++] = (uint8_t)p;
    };
    size_t i = 0;
    while (i < w.size()) {
        if (w[i] == '.') { i++; continue; }
        size_t used = 0;
        for (size_t len = 2; len >= 1 && used == 0; len--) {
            if (i + len > w.size()) continue;
            std::string part = w.substr(i, len);
            for (const Spelling& sp : SPELLINGS) {
                if (part != sp.text) continue;
                for (const char* snd : sp.sounds)
                    if (snd) push(phone_index(snd));
                used = len;
                break;
            }
            if (used == 0) {
                int p = phone_index(part);
                if (p >= 0) { push(p); used = len; }
            }
        }
        if (used == 0)
            throw std::runtime_error(std::string("'") + w[i] + "' in syllable '" + word + "' is no sound");
        i += used;
    }
    int first = -1, last = -1;
    for (int j = 0; j < s.n; j++)
        if (is_vowel(s.ph[j])) { if (first < 0) first = j; last = j; }
    if (first < 0) { s.nuc0 = 0; s.nuc1 = s.n; }
    else {
        // the vowels of the syllable and the glides between them
        s.nuc0 = (uint8_t)first;
        s.nuc1 = (uint8_t)(last + 1);
    }
    return s;
}

// A lyric: syllables split by spaces. "-" holds the vowel before over one
// more note (the consonants that end the syllable move to the last note),
// "_" is a note sung silent.
inline std::vector<Syllable> parse_lyric(const std::string& text) {
    std::vector<Syllable> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace((unsigned char)text[i])) i++;
        size_t j = i;
        while (j < text.size() && !std::isspace((unsigned char)text[j])) j++;
        if (j == i) break;
        std::string tok = text.substr(i, j - i);
        i = j;
        if (tok == "_") { out.push_back(Syllable{}); continue; }
        if (tok == "-") {
            Syllable h;
            if (out.empty() || out.back().n == 0) { h.ph[0] = (uint8_t)phone_index("u"); h.n = 1; h.nuc1 = 1; }
            else {
                Syllable& prev = out.back();
                h.ph[h.n++] = prev.ph[prev.nuc1 - 1];
                for (int c = prev.nuc1; c < prev.n; c++) h.ph[h.n++] = prev.ph[c];
                h.nuc0 = 0;
                h.nuc1 = 1;
                prev.n = prev.nuc1;
            }
            out.push_back(h);
            continue;
        }
        out.push_back(parse_syllable(tok));
    }
    return out;
}

// What one sounding voice is singing, and its filter bank
struct VoiceState {
    bool on = false;
    Syllable syl;
    int64_t t = 0, len = 0;
    int64_t onset_end[MAX_PH] = {};   // frames: the end of each onset sound
    int64_t onset_total = 0, coda_start = 0;
    int64_t coda_end[MAX_PH] = {};
    float gv[NB] = {}, gn[NB] = {};
    float sv[2][NB][4] = {}, sn[NB][4] = {};
    int cur = -1;  // the sound now sung, an index into PHONES
};

inline int64_t ms_frames(float ms) { return (int64_t)(ms * 0.001f * RATE); }

// Lays the syllable out over a note of len frames (unknown: below 0)
inline void start(VoiceState& s, const Syllable& syl, int64_t len) {
    s.on = true;
    s.syl = syl;
    s.t = 0;
    s.len = len > 0 ? len : (int64_t)(0.4f * RATE);
    int64_t on = 0, cd = 0;
    for (int i = 0; i < syl.nuc0; i++) on += ms_frames(PHONES[syl.ph[i]].ms);
    for (int i = syl.nuc1; i < syl.n; i++) cd += ms_frames(PHONES[syl.ph[i]].ms);
    // consonants take at most 60 percent of a short note
    double fit = (on + cd) > 0 ? std::min(1.0, 0.6 * (double)s.len / (double)(on + cd)) : 1.0;
    int64_t acc = 0;
    for (int i = 0; i < syl.nuc0; i++) {
        acc += (int64_t)(ms_frames(PHONES[syl.ph[i]].ms) * fit);
        s.onset_end[i] = acc;
    }
    s.onset_total = acc;
    int64_t coda_len = (int64_t)(cd * fit);
    s.coda_start = std::max(s.onset_total, s.len - coda_len);
    acc = s.coda_start;
    for (int i = syl.nuc1; i < syl.n; i++) {
        acc += (int64_t)(ms_frames(PHONES[syl.ph[i]].ms) * fit);
        s.coda_end[i] = acc;
    }
}

inline float smoothstep01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// One frame: the oscillator's l and r in, the sung l and r out
inline void process(VoiceState& s, float& l, float& r) {
    const Tables& T = tables();
    const Syllable& syl = s.syl;
    float tv[NB], tn[NB];
    std::fill(tv, tv + NB, 0.0f);
    std::fill(tn, tn + NB, 0.0f);
    int vowel = syl.nuc1 > syl.nuc0 ? syl.ph[syl.nuc0] : -1;

    // a consonant, with a plosive shut first and bursting at its end
    auto consonant = [&](int p, float frac) {
        const Phone& ph = PHONES[p];
        s.cur = p;
        if (ph.kind == PLOSIVE) {
            // shut (a murmur when voiced), the burst, then for p t k a puff
            // of breath coloured like the vowel, for b d g the voice back
            int shape = vowel >= 0 ? vowel : phone_index("uh");
            bool voiced = ph.voiced > 0.0f;
            if (frac < 0.45f) {
                if (voiced)
                    for (int b = 0; b < NB; b++) tv[b] = T.voiced[p][b] * 0.5f;
            } else if (frac < 0.65f) {
                float burst = 1.0f - (frac - 0.45f) / 0.2f;
                for (int b = 0; b < NB; b++) tn[b] = T.noise[p][b] * (0.5f + 0.5f * burst) * (voiced ? 0.7f : 1.0f);
            } else if (!voiced) {
                for (int b = 0; b < NB; b++) tn[b] = T.voiced[shape][b] * 0.3f;
            } else {
                for (int b = 0; b < NB; b++) tv[b] = T.voiced[shape][b] * 0.6f;
            }
            return;
        }
        if (ph.kind == ASPIR) {
            int shape = vowel >= 0 ? vowel : phone_index("uh");
            for (int b = 0; b < NB; b++) tn[b] = T.voiced[shape][b] * ph.noise;
            return;
        }
        for (int b = 0; b < NB; b++) { tv[b] = T.voiced[p][b]; tn[b] = T.noise[p][b]; }
    };

    if (syl.n == 0) {
        s.cur = -1;
    } else if (s.t < s.onset_total) {
        int i = 0;
        while (i < syl.nuc0 - 1 && s.t >= s.onset_end[i]) i++;
        int64_t seg0 = i == 0 ? 0 : s.onset_end[i - 1];
        float frac = (float)(s.t - seg0) / (float)std::max<int64_t>(1, s.onset_end[i] - seg0);
        consonant(syl.ph[i], frac);
    } else if (s.t >= s.coda_start && syl.n > syl.nuc1) {
        int i = syl.nuc1;
        while (i < syl.n - 1 && s.t >= s.coda_end[i]) i++;
        if (s.t >= s.coda_end[syl.n - 1]) {
            s.cur = -1;
        } else {
            int64_t seg0 = i == syl.nuc1 ? s.coda_start : s.coda_end[i - 1];
            float frac = (float)(s.t - seg0) / (float)std::max<int64_t>(1, s.coda_end[i] - seg0);
            consonant(syl.ph[i], frac);
        }
    } else {
        // the vowels: the first held, then a glide to each next one
        int k = syl.nuc1 - syl.nuc0;
        int64_t span = std::max<int64_t>(1, (syl.n > syl.nuc1 ? s.coda_start : s.len) - s.onset_total);
        float pos = std::min(1.0f, (float)(s.t - s.onset_total) / (float)span);
        int a = 0;
        float w = 0.0f;
        if (k > 1) {
            float x = pos * (float)(k - 1) * 1.6f - 0.3f;
            a = std::clamp((int)std::floor(std::max(0.0f, x)), 0, k - 2);
            w = smoothstep01(x - (float)a);
        }
        int pa = syl.ph[syl.nuc0 + a];
        int pb = syl.ph[syl.nuc0 + std::min(a + 1, k - 1)];
        s.cur = w < 0.5f ? pa : pb;
        for (int b = 0; b < NB; b++) {
            tv[b] = T.voiced[pa][b] * (1.0f - w) + T.voiced[pb][b] * w;
            tn[b] = T.noise[pa][b] * (1.0f - w) + T.noise[pb][b] * w;
        }
    }
    s.t++;

    float nz = ((float)std::rand() / RAND_MAX) * 2.0f - 1.0f;
    float in[2] = {l, r};
    float out[2] = {0.0f, 0.0f}, hiss = 0.0f;
    for (int b = 0; b < NB; b++) {
        s.gv[b] += (tv[b] - s.gv[b]) * T.smooth_v;
        s.gn[b] += (tn[b] - s.gn[b]) * T.smooth_n;
        float g = T.g[b], k = T.k[b];
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        // two band-passes in a row: skirts twice as steep, so a band
        // passes little of its neighbours' harmonics
        auto stage = [&](float x, float* st) {
            float v3 = x - st[1];
            float v1 = a1 * st[0] + a2 * v3;
            float v2 = st[1] + a2 * st[0] + a3 * v3;
            st[0] = 2.0f * v1 - st[0];
            st[1] = 2.0f * v2 - st[1];
            return k * v1;
        };
        auto band = [&](float x, float* st) { return stage(stage(x, st), st + 2); };
        float bl = band(in[0], s.sv[0][b]);
        float br = band(in[1], s.sv[1][b]);
        out[0] += bl * s.gv[b];
        out[1] += br * s.gv[b];
        hiss += band(nz, s.sn[b]) * s.gn[b];
    }
    l = (out[0] + hiss) * MAKEUP;
    r = (out[1] + hiss) * MAKEUP;
}

}  // namespace jdsing
