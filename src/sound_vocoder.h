// sound_vocoder.h - a channel vocoder for SOUND.VOCODER.
//
// A recording (the modulator, usually speech) passes a bank of band-passes;
// the level of each band follows it, and the same bands cut from the
// voice's own oscillator (the carrier) are mixed at those levels, so the
// synth note says what the recording says at the pitch of the key. A little
// noise added to the carrier lets s, f and t through, which a harmonic
// carrier has too little energy for.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace jdvoc {

constexpr int MAXB = 32;

// The bands, log-spaced from 120 Hz to 8 kHz, each as wide as the step
// between two centres; envelope rates and the level that makes up for
// the narrow bands
struct Bank {
    int n = 16;
    float g[MAXB] = {}, k[MAXB] = {};
    float att = 0.0f, rel = 0.0f, makeup = 1.0f;
};

inline std::shared_ptr<const Bank> make_bank(int n, float rate) {
    auto b = std::make_shared<Bank>();
    b->n = std::clamp(n, 4, MAXB);
    const double lo = 120.0, hi = 8000.0;
    const double step = std::log2(hi / lo) / (b->n - 1);
    const double q = 1.0 / (std::pow(2.0, step / 2.0) - std::pow(2.0, -step / 2.0));
    for (int i = 0; i < b->n; i++) {
        double fc = lo * std::pow(2.0, i * step);
        b->g[i] = (float)std::tan(3.14159265358979323846 * std::min(fc, 0.45 * rate) / rate);
        b->k[i] = (float)(1.0 / q);
    }
    b->att = 1.0f - std::exp(-1.0f / (0.002f * rate));
    b->rel = 1.0f - std::exp(-1.0f / (0.025f * rate));
    b->makeup = 2.5f * std::sqrt((float)b->n);
    return b;
}

// One voice: where it is in the modulator, the filter states (modulator,
// then carrier left and right) and the band levels
struct State {
    bool on = false;
    double pos = 0.0;
    float m[MAXB][2] = {};
    float c[2][MAXB][2] = {};
    float e[MAXB] = {};
};

// A state-variable band-pass (trapezoidal), unity gain at the centre
inline float band(float x, float g, float k, float* st) {
    float a1 = 1.0f / (1.0f + g * (g + k));
    float a2 = g * a1, a3 = g * a2;
    float v3 = x - st[1];
    float v1 = a1 * st[0] + a2 * v3;
    float v2 = st[1] + a2 * st[0] + a3 * v3;
    st[0] = 2.0f * v1 - st[0];
    st[1] = 2.0f * v2 - st[1];
    return k * v1;
}

// One frame: the carrier l and r in, the vocoded l and r out. mix 0 is the
// plain carrier, 1 the vocoder alone.
inline void process(State& s, const Bank& b, const std::vector<float>& mod, bool loop,
                    float noise, float mix, uint32_t& seed, float& l, float& r) {
    float x = 0.0f;
    if (!mod.empty()) {
        if (loop && s.pos >= (double)mod.size()) s.pos = 0.0;
        if (s.pos < (double)mod.size()) x = mod[(size_t)s.pos];
        s.pos += 1.0;
    }
    seed = seed * 1664525u + 1013904223u;
    float nz = ((float)(seed >> 8) / (float)(1u << 24) * 2.0f - 1.0f) * noise;
    float cl = l + nz, cr = r + nz;
    float ol = 0.0f, orr = 0.0f;
    for (int i = 0; i < b.n; i++) {
        float a = std::fabs(band(x, b.g[i], b.k[i], s.m[i]));
        s.e[i] += (a > s.e[i] ? b.att : b.rel) * (a - s.e[i]);
        ol += band(cl, b.g[i], b.k[i], s.c[0][i]) * s.e[i];
        orr += band(cr, b.g[i], b.k[i], s.c[1][i]) * s.e[i];
    }
    l += (ol * b.makeup - l) * mix;
    r += (orr * b.makeup - r) * mix;
}

}  // namespace jdvoc
