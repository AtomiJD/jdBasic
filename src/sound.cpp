#include "sound.h"
#include "vm.h"
#include "errors.h"

#ifndef GFX
// Stub when SDL3 not available
void sound_shutdown() {}
void register_sound_builtins(VM& vm) {
    vm.register_native("SOUND.INIT", [](const std::vector<Value>&) -> Value {
        throw jdError(ErrCode::RUNTIME_ERROR, "SOUND requires GFX build (build.bat GFX)");
    });
}
#else // GFX

#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <atomic>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr int SAMPLE_RATE = 44100;
static constexpr int NUM_TRACKS = 8;
static constexpr int MAX_VOICES = 64;
static constexpr int MAX_SFX_CHANNELS = 16;

// ── Note Map ────────────────────────────────────────────────────

static std::unordered_map<std::string, int> g_note_map;

static void init_note_map() {
    if (!g_note_map.empty()) return;
    const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const char* flats[] = {"C","DB","D","EB","E","F","GB","G","AB","A","BB","B"};
    for (int oct = 0; oct <= 8; oct++) {
        for (int n = 0; n < 12; n++) {
            int midi = (oct + 1) * 12 + n;
            g_note_map[std::string(names[n]) + std::to_string(oct)] = midi;
            g_note_map[std::string(flats[n]) + std::to_string(oct)] = midi;
        }
    }
}

static float midi_to_freq(int midi) {
    return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
}

static int note_name_to_midi(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    auto it = g_note_map.find(upper);
    if (it != g_note_map.end()) return it->second;
    return -1;
}

static float resolve_note(const std::string& s) {
    int midi = note_name_to_midi(s);
    if (midi >= 0) return midi_to_freq(midi);
    try { return std::stof(s); } catch (...) { return 0; }
}

// ── Scale Library ───────────────────────────────────────────────

struct Scale {
    std::vector<int> intervals;
};

static const std::unordered_map<std::string, std::vector<int>> g_scales = {
    {"CHROMATIC", {0,1,2,3,4,5,6,7,8,9,10,11}},
    {"MAJOR", {0,2,4,5,7,9,11}}, {"MINOR", {0,2,3,5,7,8,10}},
    {"DORIAN", {0,2,3,5,7,9,10}}, {"PHRYGIAN", {0,1,3,5,7,8,10}},
    {"LYDIAN", {0,2,4,6,7,9,11}}, {"MIXOLYDIAN", {0,2,4,5,7,9,10}},
    {"LOCRIAN", {0,1,3,5,6,8,10}},
    {"PENT_MAJ", {0,2,4,7,9}}, {"PENT_MIN", {0,3,5,7,10}},
    {"BLUES", {0,3,5,6,7,10}}, {"ARABIC", {0,1,4,5,7,8,11}},
};

// ── Waveforms ───────────────────────────────────────────────────

enum class Waveform { SINE, SQUARE, SAW, TRIANGLE, NOISE, SAMPLE };

static Waveform parse_waveform(const std::string& s) {
    if (s == "SINE") return Waveform::SINE;
    if (s == "SQUARE") return Waveform::SQUARE;
    if (s == "SAW") return Waveform::SAW;
    if (s == "TRIANGLE") return Waveform::TRIANGLE;
    if (s == "NOISE") return Waveform::NOISE;
    if (s == "SAMPLE") return Waveform::SAMPLE;
    return Waveform::SINE;
}

static float poly_blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ── ADSR Envelope ───────────────────────────────────────────────

enum class EnvState { OFF, ATTACK, DECAY, SUSTAIN, RELEASE };

struct Envelope {
    float attack = 0.01f, decay = 0.1f, sustain = 0.7f, release = 0.3f;
    EnvState state = EnvState::OFF;
    float level = 0.0f;

    void gate_on() { state = EnvState::ATTACK; }
    void gate_off() { if (state != EnvState::OFF) state = EnvState::RELEASE; }

    float process(float dt) {
        switch (state) {
            case EnvState::ATTACK:
                level += dt / std::max(attack, 0.001f);
                if (level >= 1.0f) { level = 1.0f; state = EnvState::DECAY; }
                break;
            case EnvState::DECAY:
                level -= dt / std::max(decay, 0.001f);
                if (level <= sustain) {
                    level = sustain;
                    // Percussive / one-shot envelopes (sustain ≈ 0) should
                    // self-terminate so the voice slot can be reused. With
                    // sustain > 0 we hold for gate_off.
                    if (sustain <= 0.0001f) state = EnvState::OFF;
                    else state = EnvState::SUSTAIN;
                }
                break;
            case EnvState::SUSTAIN: break;
            case EnvState::RELEASE:
                level -= dt / std::max(release, 0.001f);
                if (level <= 0.0f) { level = 0.0f; state = EnvState::OFF; }
                break;
            case EnvState::OFF: break;
        }
        return level;
    }
};

// ── Sound Chunk (loaded WAV) ────────────────────────────────────

struct SoundChunk {
    std::vector<float> data; // interleaved stereo
    int sample_rate = SAMPLE_RATE;
};

// ── SFX Channel ─────────────────────────────────────────────────

struct SfxChannel {
    int sample_id = -1;
    size_t position = 0;
    bool active = false;
    bool looping = false;
};

// ── Voice (active sound generator) ──────────────────────────────

struct Voice {
    int track = -1;
    float frequency = 440.0f;
    float phase = 0.0f;
    Envelope env;
    // FM
    float fm_phase = 0.0f;
    // Unison phases
    float unison_phases[16] = {};
    // LFO
    float lfo_phase = 0.0f;
    // Filter state
    float filter_l = 0.0f, filter_r = 0.0f;
    // EQ state
    float eq_lo_l = 0.0f, eq_lo_r = 0.0f;
    float eq_hi_l = 0.0f, eq_hi_r = 0.0f;
    // Ring mod
    float ring_phase = 0.0f;
    // Bitcrush
    float crush_counter = 0.0f;
    float crush_hold_l = 0.0f, crush_hold_r = 0.0f;
    // Sample playback
    int sample_id = -1;
    float sample_rate_ratio = 1.0f;
    float sample_pos = 0.0f;
    bool sample_loop = false;
    // Sequencer gate-off (per-voice duration tracking)
    bool seq_controlled = false;
    double seq_remaining = 0.0;
};

// ── Track Parameters ────────────────────────────────────────────

struct TrackParams {
    Waveform waveform = Waveform::SINE;
    float attack = 0.01f, decay = 0.1f, sustain = 0.7f, release = 0.3f;
    float gain = 1.0f, pan = 0.5f;
    float filter_cutoff = 20000.0f;
    float eq_low = 1.0f, eq_mid = 1.0f, eq_high = 1.0f;
    float lfo_freq = 0.0f, lfo_depth = 0.0f;
    float fm_amount = 0.0f, fm_ratio = 1.0f;
    int unison_voices = 1;
    float unison_detune = 0.0f, unison_spread = 0.5f;
    float crush_bits = 16.0f, crush_rate = 1.0f;
    float ring_freq = 0.0f, ring_mix = 0.0f;
    float reverb_send = 0.0f, delay_send = 0.0f;
    int sidechain_source = -1;
    float sidechain_amount = 0.0f;
    // Scale
    int scale_root_midi = 48; // C3
    std::vector<int> scale_intervals = {0,2,4,5,7,9,11}; // Major
    // Sample
    int sample_id = -1;
    float sample_base_freq = 261.63f; // C4
    bool sample_loop = false;
};

// ── Sequencer Event ─────────────────────────────────────────────

struct SeqEvent {
    double start_phase;  // 0..total_length within cycle
    double duration;     // in phase units
    std::string note;    // note name or frequency
    float probability = 1.0f;
    bool triggered = false;
};

struct SeqLayer {
    std::vector<SeqEvent> events;
    Waveform waveform = Waveform::SINE;
    bool use_voice = false; // true = use track's voice settings
    double remaining = 0.0;  // gate-off timer
    int active_voice_idx = -1;
    // Linear (SOUND.NOTE) mode
    bool is_linear = false;
    bool looping = false;
    double total_length = 1.0;
    double linear_cursor = 0.0;
};

// ── Reverb Module (Schroeder) ───────────────────────────────────

struct CombFilter {
    std::vector<float> buffer;
    int pos = 0;
    float filter_state = 0.0f;
    void init(int len) { buffer.resize(len, 0.0f); pos = 0; filter_state = 0.0f; }
    float process(float input, float feedback, float damping) {
        float delayed = buffer[pos];
        filter_state = delayed * (1.0f - damping) + filter_state * damping;
        buffer[pos] = input + filter_state * feedback;
        pos = (pos + 1) % (int)buffer.size();
        return delayed;
    }
};

struct AllpassFilter {
    std::vector<float> buffer;
    int pos = 0;
    void init(int len) { buffer.resize(len, 0.0f); pos = 0; }
    float process(float input, float feedback = 0.5f) {
        float delayed = buffer[pos];
        float feed = input + delayed * feedback;
        buffer[pos] = feed;
        pos = (pos + 1) % (int)buffer.size();
        return delayed - feed * feedback;
    }
};

struct ReverbModule {
    CombFilter combs_l[4], combs_r[4];
    AllpassFilter allpass_l[2], allpass_r[2];
    float room_size = 0.7f, damping = 0.5f, width = 1.0f, wet = 0.3f;

    void init() {
        const int comb_lens[] = {1687, 1601, 2053, 2251};
        const int ap_lens[] = {225, 341};
        for (int i = 0; i < 4; i++) {
            combs_l[i].init(comb_lens[i]);
            combs_r[i].init(comb_lens[i] + 23); // stereo spread
        }
        for (int i = 0; i < 2; i++) {
            allpass_l[i].init(ap_lens[i]);
            allpass_r[i].init(ap_lens[i] + 23);
        }
    }

    void process(float input, float& out_l, float& out_r) {
        float cl = 0, cr = 0;
        for (int i = 0; i < 4; i++) {
            cl += combs_l[i].process(input, room_size, damping);
            cr += combs_r[i].process(input, room_size, damping);
        }
        cl /= 4; cr /= 4;
        for (int i = 0; i < 2; i++) {
            cl = allpass_l[i].process(cl);
            cr = allpass_r[i].process(cr);
        }
        float w1 = 0.5f * (1.0f + width), w2 = 0.5f * (1.0f - width);
        out_l = cl * w1 + cr * w2;
        out_r = cr * w1 + cl * w2;
    }
};

// ── Compressor ──────────────────────────────────────────────────

struct Compressor {
    float threshold = 0.6f, ratio = 4.0f;
    float attack_ms = 10.0f, release_ms = 100.0f;
    float makeup = 1.2f;
    float env = 0.0f;

    void process(float& l, float& r) {
        float peak = std::max(std::abs(l), std::abs(r));
        float a_att = std::exp(-1.0f / (0.001f * attack_ms * SAMPLE_RATE));
        float a_rel = std::exp(-1.0f / (0.001f * release_ms * SAMPLE_RATE));
        env = (peak > env) ? a_att * env + (1 - a_att) * peak
                           : a_rel * env + (1 - a_rel) * peak;
        if (env > threshold && env > 0.0001f) {
            float over_db = 20.0f * std::log10(env / threshold);
            float red_db = over_db * (1.0f - 1.0f / ratio);
            float gain = std::pow(10.0f, -red_db / 20.0f) * makeup;
            l *= gain; r *= gain;
        } else {
            l *= makeup; r *= makeup;
        }
        l = std::max(-1.0f, std::min(1.0f, l));
        r = std::max(-1.0f, std::min(1.0f, r));
    }
};

// ── Sound System ────────────────────────────────────────────────

static struct SoundSystem {
    bool initialized = false;
    SDL_AudioStream* stream = nullptr;
    // Push-mode stream for SOUND.PLAYBUFFER: scripts queue raw float32
    // samples directly, the SDL audio mixer combines this stream's
    // output with the synth's `stream` on the same device.
    SDL_AudioStream* pcm_stream = nullptr;
    int pcm_sample_rate = SAMPLE_RATE;
    int pcm_channels = 1;

    // Voices
    Voice voices[MAX_VOICES];
    TrackParams tracks[NUM_TRACKS];
    float track_envelopes[NUM_TRACKS] = {};

    // SFX
    std::unordered_map<int, SoundChunk> samples;
    SfxChannel sfx_channels[MAX_SFX_CHANNELS];
    int music_channel = -1;

    // Sequencer
    float bpm = 120.0f;
    double seq_phase = 0.0;
    SeqLayer seq_layers[NUM_TRACKS];
    int dbg_triggers = 0;
    int dbg_alloc_fail = 0;

    // Effects
    ReverbModule reverb;
    Compressor compressor;
    bool compressor_active = false;
    // Delay
    std::vector<float> delay_buffer;
    int delay_head = 0;
    float delay_time = 0.3f, delay_feedback = 0.3f, delay_mix = 0.3f;
    bool delay_active = false;
    // Distortion
    float distortion_amount = 0.0f;

    // Visualization
    std::vector<float> wave_buffer;
    std::vector<float> reverb_bus_wave;
    std::vector<float> delay_bus_wave;

    int allocate_voice(int track) {
        // Find free voice
        for (int i = 0; i < MAX_VOICES; i++) {
            if (voices[i].env.state == EnvState::OFF) {
                voices[i] = Voice{};
                voices[i].track = track;
                return i;
            }
        }
        // Steal quietest releasing voice
        int best = -1; float best_level = 999.0f;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (voices[i].env.state == EnvState::RELEASE && voices[i].env.level < best_level) {
                best = i; best_level = voices[i].env.level;
            }
        }
        if (best >= 0) { voices[best] = Voice{}; voices[best].track = track; }
        return best;
    }

    float generate_waveform(Waveform wf, float phase, float dt) {
        switch (wf) {
            case Waveform::SINE: return std::sin(phase * 2.0f * (float)M_PI);
            case Waveform::SAW: {
                float t = std::fmod(phase, 1.0f); if (t < 0) t += 1.0f;
                return (2.0f * t - 1.0f) - poly_blep(t, dt);
            }
            case Waveform::SQUARE: {
                float t = std::fmod(phase, 1.0f); if (t < 0) t += 1.0f;
                float v = (t < 0.5f) ? 1.0f : -1.0f;
                v += poly_blep(t, dt);
                v -= poly_blep(std::fmod(t + 0.5f, 1.0f), dt);
                return v;
            }
            case Waveform::TRIANGLE: {
                float t = std::fmod(phase, 1.0f); if (t < 0) t += 1.0f;
                return 2.0f * std::abs(2.0f * t - 1.0f) - 1.0f;
            }
            case Waveform::NOISE: return ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            default: return 0.0f;
        }
    }

    void generate_voice(Voice& v, float& out_l, float& out_r) {
        auto& tp = tracks[v.track];
        float dt = 1.0f / SAMPLE_RATE;
        float dt_freq = v.frequency / SAMPLE_RATE;

        // LFO
        float freq = v.frequency;
        if (tp.lfo_freq > 0 && tp.lfo_depth > 0) {
            float lfo = std::sin(v.lfo_phase * 2.0f * (float)M_PI);
            freq += lfo * tp.lfo_depth;
            v.lfo_phase += tp.lfo_freq * dt;
        }

        // FM
        float fm_mod = 0.0f;
        if (tp.fm_amount > 0) {
            fm_mod = std::sin(v.fm_phase * 2.0f * (float)M_PI) * tp.fm_amount / (2.0f * (float)M_PI);
            v.fm_phase += freq * tp.fm_ratio * dt;
        }

        float sample_l = 0, sample_r = 0;

        // Sample playback
        if (tp.waveform == Waveform::SAMPLE && v.sample_id >= 0) {
            auto sit = samples.find(v.sample_id);
            if (sit != samples.end()) {
                auto& chunk = sit->second;
                float rate = freq / tp.sample_base_freq;
                if (v.sample_pos < chunk.data.size() / 2) {
                    size_t idx = (size_t)v.sample_pos * 2;
                    sample_l = (idx < chunk.data.size()) ? chunk.data[idx] : 0;
                    sample_r = (idx + 1 < chunk.data.size()) ? chunk.data[idx + 1] : sample_l;
                    v.sample_pos += rate;
                    if (v.sample_pos >= chunk.data.size() / 2) {
                        if (v.sample_loop) v.sample_pos = 0;
                        else v.env.state = EnvState::OFF;
                    }
                }
            }
        }
        // Unison synthesis
        else if (tp.unison_voices > 1) {
            float norm = 1.0f / std::sqrt((float)tp.unison_voices);
            for (int u = 0; u < tp.unison_voices && u < 16; u++) {
                float ratio = (tp.unison_voices == 1) ? 0 :
                    ((float)u / (tp.unison_voices - 1) - 0.5f);
                float det = 1.0f + ratio * tp.unison_detune * 0.1f;
                float pan_u = 0.5f + ratio * tp.unison_spread;
                float f = freq * det / SAMPLE_RATE;
                v.unison_phases[u] += f;
                float s = generate_waveform(tp.waveform, v.unison_phases[u] + fm_mod, f);
                sample_l += s * (1.0f - pan_u) * norm;
                sample_r += s * pan_u * norm;
            }
        }
        // Normal mono synthesis
        else {
            float f = freq / SAMPLE_RATE;
            v.phase += f;
            float s = generate_waveform(tp.waveform, v.phase + fm_mod, f);
            sample_l = s;
            sample_r = s;
        }

        // Sidechain ducking
        if (tp.sidechain_source >= 0 && tp.sidechain_source < NUM_TRACKS) {
            float duck = 1.0f - track_envelopes[tp.sidechain_source] * tp.sidechain_amount;
            sample_l *= duck; sample_r *= duck;
        }

        // Low-pass filter (1-pole RC)
        if (tp.filter_cutoff < 19999.0f) {
            float rc = 1.0f / (2.0f * (float)M_PI * tp.filter_cutoff);
            float alpha = dt / (rc + dt);
            v.filter_l += alpha * (sample_l - v.filter_l); sample_l = v.filter_l;
            v.filter_r += alpha * (sample_r - v.filter_r); sample_r = v.filter_r;
        }

        // Ring modulator
        if (tp.ring_freq > 0 && tp.ring_mix > 0) {
            float carrier = std::sin(v.ring_phase * 2.0f * (float)M_PI);
            v.ring_phase += tp.ring_freq * dt;
            float wet_l = sample_l * carrier, wet_r = sample_r * carrier;
            sample_l = sample_l * (1.0f - tp.ring_mix) + wet_l * tp.ring_mix;
            sample_r = sample_r * (1.0f - tp.ring_mix) + wet_r * tp.ring_mix;
        }

        // Bitcrusher
        if (tp.crush_bits < 15.9f) {
            float steps = std::pow(2.0f, tp.crush_bits);
            v.crush_counter += tp.crush_rate;
            if (v.crush_counter >= 1.0f) {
                v.crush_counter -= 1.0f;
                v.crush_hold_l = std::floor(sample_l * steps) / steps;
                v.crush_hold_r = std::floor(sample_r * steps) / steps;
            }
            sample_l = v.crush_hold_l; sample_r = v.crush_hold_r;
        }

        // 3-band EQ
        if (tp.eq_low != 1.0f || tp.eq_mid != 1.0f || tp.eq_high != 1.0f) {
            auto eq_band = [&](float in, float& lo_state, float& hi_state) {
                float a_lo = dt / (1.0f / (2.0f * (float)M_PI * 250.0f) + dt);
                float a_hi = dt / (1.0f / (2.0f * (float)M_PI * 3000.0f) + dt);
                lo_state += a_lo * (in - lo_state);
                hi_state += a_hi * (in - hi_state);
                float lo = lo_state, hi = in - hi_state, mid = in - lo - hi;
                return lo * tp.eq_low + mid * tp.eq_mid + hi * tp.eq_high;
            };
            sample_l = eq_band(sample_l, v.eq_lo_l, v.eq_hi_l);
            sample_r = eq_band(sample_r, v.eq_lo_r, v.eq_hi_r);
        }

        // Envelope
        float env = v.env.process(dt);

        out_l = sample_l * env * tp.gain * (1.0f - tp.pan) * 2.0f;
        out_r = sample_r * env * tp.gain * tp.pan * 2.0f;
    }

    // Scale degree to frequency
    float degree_to_freq(int track, int degree) {
        auto& tp = tracks[track];
        int n = (int)tp.scale_intervals.size();
        if (n == 0) return 440.0f;
        int oct = (degree >= 0) ? degree / n : (degree - n + 1) / n;
        int idx = ((degree % n) + n) % n;
        int midi = tp.scale_root_midi + oct * 12 + tp.scale_intervals[idx];
        return midi_to_freq(midi);
    }

    // ── Sequencer pattern parser ────────────────────────────────

    // Tokenize: split by spaces at bracket depth 0, keep brackets in tokens
    void parse_pattern(const std::string& pattern, std::vector<SeqEvent>& events,
                       double start, double duration) {
        std::vector<std::string> tokens;
        std::string current;
        int depth = 0;
        for (char c : pattern) {
            if (c == '[') depth++;
            if (c == ']') depth--;
            if (c == ' ' && depth == 0) {
                if (!current.empty()) tokens.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) tokens.push_back(current);
        if (tokens.empty()) return;

        double step_dur = duration / tokens.size();
        for (size_t i = 0; i < tokens.size(); i++) {
            double s = start + i * step_dur;
            std::string tok = tokens[i];

            // Trim whitespace
            while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
            while (!tok.empty() && tok.back() == ' ') tok.pop_back();
            if (tok.empty()) continue;

            // Rest
            if (tok == "~" || tok == ".") continue;

            // Check for repeat *N at top-level depth
            int repeats = 1;
            size_t star_pos = std::string::npos;
            { int d = 0;
              for (size_t k = 0; k < tok.size(); k++) {
                  if (tok[k] == '[') d++;
                  else if (tok[k] == ']') d--;
                  else if (tok[k] == '*' && d == 0) star_pos = k;
              }
            }
            if (star_pos != std::string::npos && star_pos < tok.size() - 1) {
                try { repeats = std::stoi(tok.substr(star_pos + 1)); } catch (...) {}
                tok = tok.substr(0, star_pos);
                if (repeats < 1) repeats = 1;
            }

            double sub_dur = step_dur / repeats;
            for (int r = 0; r < repeats; r++) {
                double sub_start = s + r * sub_dur;

                // Subdivision: [...]
                if (tok.front() == '[' && tok.back() == ']') {
                    parse_pattern(tok.substr(1, tok.size() - 2), events, sub_start, sub_dur);
                }
                // Probability: note?0.5
                else if (tok.find('?') != std::string::npos) {
                    size_t q = tok.find('?');
                    float prob = 0.5f;
                    try { prob = std::stof(tok.substr(q + 1)); } catch (...) {}
                    SeqEvent ev; ev.start_phase = sub_start; ev.duration = sub_dur * 0.8f;
                    ev.note = tok.substr(0, q); ev.probability = prob;
                    events.push_back(ev);
                }
                // Chord: c3,e3,g3
                else if (tok.find(',') != std::string::npos) {
                    size_t pos = 0;
                    while (pos < tok.size()) {
                        size_t comma = tok.find(',', pos);
                        std::string n = (comma == std::string::npos) ? tok.substr(pos) : tok.substr(pos, comma - pos);
                        SeqEvent ev; ev.start_phase = sub_start; ev.duration = sub_dur * 0.8f; ev.note = n;
                        events.push_back(ev);
                        pos = (comma == std::string::npos) ? tok.size() : comma + 1;
                    }
                }
                // Single note
                else {
                    SeqEvent ev; ev.start_phase = sub_start; ev.duration = sub_dur * 0.8f; ev.note = tok;
                    events.push_back(ev);
                }
            }
        }
    }

    int count_steps(const std::string& pat) {
        int count = 0, depth = 0;
        bool in_token = false;
        for (char c : pat) {
            if (c == '[') depth++;
            if (c == ']') depth--;
            if (depth == 0 && c != ' ') { if (!in_token) { count++; in_token = true; } }
            else if (depth == 0 && c == ' ') in_token = false;
        }
        return count > 0 ? count : 1;
    }

    // Parse "< pattern1 , pattern2 >" into multiple tracks
    void parse_note_pattern(const std::string& full_pattern, int track_offset, bool loop) {
        // Clean up
        std::string clean;
        for (char c : full_pattern) {
            if (c == '\n' || c == '\r') clean += ' '; else clean += c;
        }
        // Trim
        size_t s0 = clean.find_first_not_of(" ");
        size_t s1 = clean.find_last_not_of(" ");
        if (s0 != std::string::npos) clean = clean.substr(s0, s1 - s0 + 1);

        // Remove < > if present
        if (!clean.empty() && clean.front() == '<' && clean.back() == '>') {
            clean = clean.substr(1, clean.size() - 2);
        }

        // Split by comma at depth 0
        std::vector<std::string> parts;
        std::string cur;
        int depth = 0;
        for (char c : clean) {
            if (c == '[') depth++;
            if (c == ']') depth--;
            if (c == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        parts.push_back(cur);

        // Parse each part into its track
        for (int i = 0; i < (int)parts.size(); i++) {
            int t = i + track_offset;
            if (t >= NUM_TRACKS) break;
            auto& layer = seq_layers[t];
            // Release any currently playing voice on this track
            if (layer.active_voice_idx >= 0 && layer.active_voice_idx < MAX_VOICES)
                voices[layer.active_voice_idx].env.gate_off();
            layer.events.clear();
            layer.active_voice_idx = -1;
            layer.remaining = 0;
            layer.use_voice = true;
            layer.is_linear = true;
            layer.looping = loop;

            int steps = count_steps(parts[i]);
            layer.total_length = (double)steps;
            parse_pattern(parts[i], layer.events, 0.0, layer.total_length);
            layer.linear_cursor = 0.0; // start from the beginning
        }
    }

    // ── Audio callback ──────────────────────────────────────────

    static void audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len) {
        (void)total_len;
        auto* sys = (SoundSystem*)userdata;
        int num_floats = additional_len / sizeof(float);
        int num_frames = num_floats / 2;

        std::vector<float> buffer(num_floats, 0.0f);
        double dt = 1.0 / SAMPLE_RATE;

        // Sequencer phase
        double cps = sys->bpm / 60.0 / 4.0; // cycles per second
        double phase_inc = dt * cps;
        double prev_phase = sys->seq_phase;

        // Process sequencer events
        double buffer_dur = num_frames * phase_inc;

        for (int t = 0; t < NUM_TRACKS; t++) {
            auto& layer = sys->seq_layers[t];
            if (layer.events.empty()) continue;

            double start_window, end_window;
            bool layer_wrapped = false;

            if (layer.is_linear) {
                start_window = layer.linear_cursor;
                end_window = start_window + buffer_dur;
                layer.linear_cursor += buffer_dur;

                if (end_window >= layer.total_length) {
                    if (layer.looping) {
                        layer.linear_cursor -= layer.total_length;
                        layer_wrapped = true;
                    } else {
                        if (start_window >= layer.total_length) {
                            layer.events.clear();
                            continue;
                        }
                    }
                }
            } else {
                start_window = prev_phase;
                end_window = start_window + buffer_dur;
            }

            // Trigger events in window
            for (auto& ev : layer.events) {
                bool in_window = false;

                if (layer.is_linear) {
                    if (layer_wrapped) {
                        bool tail = (ev.start_phase >= start_window && ev.start_phase < layer.total_length);
                        double head_end = end_window - layer.total_length;
                        // Use <= for head to include events at exactly 0.0
                        bool head = (ev.start_phase >= 0.0 && ev.start_phase <= head_end);
                        in_window = tail || head;
                    } else {
                        in_window = (ev.start_phase >= start_window && ev.start_phase < end_window);
                    }
                } else {
                    if (end_window > 1.0) {
                        bool tail = (ev.start_phase >= start_window && ev.start_phase < 1.0);
                        double head_end = end_window - 1.0;
                        bool head = (ev.start_phase >= 0.0 && ev.start_phase <= head_end);
                        in_window = tail || head;
                    } else {
                        in_window = (ev.start_phase >= start_window && ev.start_phase < end_window);
                    }
                }

                if (!ev.triggered && in_window) {
                    ev.triggered = true;

                    if (ev.probability < 1.0f && ((float)rand() / RAND_MAX) > ev.probability)
                        continue;

                    // The previous voice will gate-off on its own via seq_remaining

                    float freq = 0;
                    int midi = note_name_to_midi(ev.note);
                    if (midi >= 0) {
                        freq = midi_to_freq(midi);
                    } else {
                        try { int deg = std::stoi(ev.note); freq = sys->degree_to_freq(t, deg); }
                        catch (...) { try { freq = std::stof(ev.note); } catch (...) { continue; } }
                    }

                    int vi = sys->allocate_voice(t);
                    if (vi < 0) { sys->dbg_alloc_fail++; continue; }
                    sys->dbg_triggers++;
                    auto& v = sys->voices[vi];
                    auto& tp = sys->tracks[t];
                    v.frequency = freq;
                    v.phase = 0.0f;
                    v.env = Envelope{tp.attack, tp.decay, tp.sustain, tp.release};
                    v.env.gate_on();
                    v.seq_controlled = true;
                    v.seq_remaining = ev.duration;
                    if (tp.waveform == Waveform::SAMPLE) {
                        v.sample_id = tp.sample_id;
                        v.sample_loop = tp.sample_loop;
                        v.sample_pos = 0;
                    }
                    layer.active_voice_idx = vi;
                }
            }

            // Reset triggered flags on wrap
            if (layer.is_linear && layer_wrapped) {
                for (auto& ev : layer.events) ev.triggered = false;
            }
            if (!layer.is_linear && end_window > 1.0) {
                for (auto& ev : layer.events) ev.triggered = false;
            }
        }

        // Per-voice gate-off: decrement seq_remaining for ALL sequencer-controlled voices
        for (int i = 0; i < MAX_VOICES; i++) {
            auto& v = sys->voices[i];
            if (v.seq_controlled && v.env.state != EnvState::OFF && v.env.state != EnvState::RELEASE) {
                v.seq_remaining -= buffer_dur;
                if (v.seq_remaining <= 0.0) {
                    v.env.gate_off();
                    v.seq_controlled = false;
                }
            }
        }

        // Advance sequencer phase
        sys->seq_phase += num_frames * phase_inc;
        while (sys->seq_phase >= 1.0) sys->seq_phase -= 1.0;

        // Per-sample generation
        float rev_bus[2] = {};
        float del_bus[2] = {};

        for (int f = 0; f < num_frames; f++) {
            float mix_l = 0, mix_r = 0;
            float rev_l = 0, rev_r = 0;
            float del_l = 0, del_r = 0;
            int active = 0;

            // Reset track envelopes
            float t_env[NUM_TRACKS] = {};

            // Synth voices
            for (int i = 0; i < MAX_VOICES; i++) {
                auto& v = sys->voices[i];
                if (v.env.state == EnvState::OFF || v.track < 0) continue;
                float vl, vr;
                sys->generate_voice(v, vl, vr);
                mix_l += vl; mix_r += vr;
                auto& tp = sys->tracks[v.track];
                rev_l += vl * tp.reverb_send; rev_r += vr * tp.reverb_send;
                del_l += vl * tp.delay_send; del_r += vr * tp.delay_send;
                float e = std::max(std::abs(vl), std::abs(vr));
                if (e > t_env[v.track]) t_env[v.track] = e;
                active++;
            }

            // SFX channels
            for (auto& ch : sys->sfx_channels) {
                if (!ch.active || ch.sample_id < 0) continue;
                auto sit = sys->samples.find(ch.sample_id);
                if (sit == sys->samples.end()) { ch.active = false; continue; }
                auto& chunk = sit->second;
                if (ch.position + 1 < chunk.data.size()) {
                    mix_l += chunk.data[ch.position];
                    mix_r += chunk.data[ch.position + 1];
                    ch.position += 2;
                    active++;
                } else {
                    if (ch.looping) ch.position = 0;
                    else ch.active = false;
                }
            }

            // Soft average
            if (active > 1) {
                float norm = 1.0f / std::sqrt((float)active);
                mix_l *= norm; mix_r *= norm;
            }

            // Store track envelopes for sidechain
            for (int t = 0; t < NUM_TRACKS; t++)
                sys->track_envelopes[t] = t_env[t];

            // Delay
            if (sys->delay_active && !sys->delay_buffer.empty()) {
                int delay_samples = (int)(sys->delay_time * SAMPLE_RATE) * 2;
                delay_samples = std::min(delay_samples, (int)sys->delay_buffer.size() - 2);
                int read_pos = (sys->delay_head - delay_samples + (int)sys->delay_buffer.size()) % (int)sys->delay_buffer.size();
                float dl = sys->delay_buffer[read_pos], dr = sys->delay_buffer[read_pos + 1 < (int)sys->delay_buffer.size() ? read_pos + 1 : 0];
                sys->delay_buffer[sys->delay_head] = del_l + dl * sys->delay_feedback;
                sys->delay_buffer[(sys->delay_head + 1) % (int)sys->delay_buffer.size()] = del_r + dr * sys->delay_feedback;
                sys->delay_head = (sys->delay_head + 2) % (int)sys->delay_buffer.size();
                mix_l += dl * sys->delay_mix; mix_r += dr * sys->delay_mix;
            }

            // Distortion
            if (sys->distortion_amount > 0) {
                float d = 1.0f + sys->distortion_amount;
                mix_l = std::tanh(mix_l * d); mix_r = std::tanh(mix_r * d);
            }

            // Reverb
            if (sys->reverb.wet > 0) {
                float mono_in = (rev_l + rev_r) * 0.5f;
                float rl, rr;
                sys->reverb.process(mono_in, rl, rr);
                mix_l += rl * sys->reverb.wet * 0.2f;
                mix_r += rr * sys->reverb.wet * 0.2f;
            }

            // Compressor
            if (sys->compressor_active) {
                sys->compressor.process(mix_l, mix_r);
            }

            buffer[f * 2] = mix_l;
            buffer[f * 2 + 1] = mix_r;
        }

        SDL_PutAudioStreamData(stream, buffer.data(), additional_len);
    }
} g_sound;

// ── Helpers ─────────────────────────────────────────────────────

static void ensure_sound(const char* fn) {
    if (!g_sound.initialized || !g_sound.stream)
        throw jdError(ErrCode::RUNTIME_ERROR, std::string(fn) + ": call SOUND.INIT first");
}

// ── Public API ──────────────────────────────────────────────────

void sound_shutdown() {
    if (g_sound.stream) {
        SDL_DestroyAudioStream(g_sound.stream);
        g_sound.stream = nullptr;
    }
    if (g_sound.pcm_stream) {
        SDL_DestroyAudioStream(g_sound.pcm_stream);
        g_sound.pcm_stream = nullptr;
    }
    g_sound.initialized = false;
}

void register_sound_builtins(VM& vm) {
    init_note_map();

    // ── SOUND.INIT ──────────────────────────────────────────────

    vm.register_native("SOUND.INIT", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (g_sound.initialized) return Value::make_none();

        if (!SDL_WasInit(0)) {
            // SDL not initialized at all
            if (!SDL_Init(SDL_INIT_AUDIO))
                throw jdError(ErrCode::RUNTIME_ERROR, std::string("SDL_Init failed: ") + SDL_GetError());
        } else if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
            // SDL initialized but not audio subsystem
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
                throw jdError(ErrCode::RUNTIME_ERROR, std::string("SDL_InitSubSystem(AUDIO) failed: ") + SDL_GetError());
        }

        SDL_AudioSpec spec;
        spec.freq = SAMPLE_RATE;
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;

        g_sound.stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
            SoundSystem::audio_callback, &g_sound);

        if (!g_sound.stream)
            throw jdError(ErrCode::RUNTIME_ERROR, std::string("SOUND.INIT failed: ") + SDL_GetError());

        // Init effects
        g_sound.reverb.init();
        g_sound.delay_buffer.resize(SAMPLE_RATE * 2 * 2, 0.0f); // 2 sec stereo
        g_sound.wave_buffer.resize(1024, 0.0f);

        SDL_ResumeAudioStreamDevice(g_sound.stream);

        // PCM push-stream — opened on the same default device. SDL3 mixes
        // multiple device-streams together, so the synth and the raw
        // sample buffer coexist without us writing a second mixer.
        SDL_AudioSpec pcm_spec;
        pcm_spec.freq = g_sound.pcm_sample_rate;
        pcm_spec.format = SDL_AUDIO_F32;
        pcm_spec.channels = g_sound.pcm_channels;
        g_sound.pcm_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &pcm_spec, nullptr, nullptr);
        if (g_sound.pcm_stream)
            SDL_ResumeAudioStreamDevice(g_sound.pcm_stream);

        g_sound.initialized = true;
        return Value::make_none();
    });

    // ── SOUND.VOICE track, waveform$, attack, decay, sustain, release ─

    vm.register_native("SOUND.VOICE", 6, 6, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        std::string wf = args[1].as_string()->data;
        std::transform(wf.begin(), wf.end(), wf.begin(), ::toupper);
        g_sound.tracks[t].waveform = parse_waveform(wf);
        g_sound.tracks[t].attack = (float)args[2].to_double();
        g_sound.tracks[t].decay = (float)args[3].to_double();
        g_sound.tracks[t].sustain = (float)args[4].to_double();
        g_sound.tracks[t].release = (float)args[5].to_double();
        return Value::make_none();
    });

    // ── SOUND.SAMPLE track, sample_id, [base_note$], [loop] ─────

    vm.register_native("SOUND.SAMPLE", 2, 4, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].sample_id = (int)args[1].to_int();
        g_sound.tracks[t].waveform = Waveform::SAMPLE;
        if (args.size() >= 3) {
            int midi = note_name_to_midi(args[2].to_string());
            if (midi >= 0) g_sound.tracks[t].sample_base_freq = midi_to_freq(midi);
        }
        if (args.size() >= 4) g_sound.tracks[t].sample_loop = args[3].to_bool();
        return Value::make_none();
    });

    // ── SOUND.PLAY track, frequency_or_note ─────────────────────

    vm.register_native("SOUND.PLAY", 2, 2, [](const std::vector<Value>& args) -> Value {
        if (!g_sound.initialized) return Value::make_none();
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        float freq;
        if (args[1].type == ValueType::STRING)
            freq = resolve_note(args[1].as_string()->data);
        else
            freq = (float)args[1].to_double();
        if (freq <= 0) return Value::make_none();

        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        int vi = g_sound.allocate_voice(t);
        if (vi >= 0) {
            auto& v = g_sound.voices[vi];
            auto& tp = g_sound.tracks[t];
            v.frequency = freq;
            v.env = Envelope{tp.attack, tp.decay, tp.sustain, tp.release};
            v.env.gate_on();
            if (tp.waveform == Waveform::SAMPLE) {
                v.sample_id = tp.sample_id;
                v.sample_loop = tp.sample_loop;
                v.sample_pos = 0;
            }
        }
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── SOUND.RELEASE track ─────────────────────────────────────

    vm.register_native("SOUND.RELEASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        for (auto& v : g_sound.voices)
            if (v.track == t && v.env.state != EnvState::OFF) v.env.gate_off();
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── SOUND.STOP track ────────────────────────────────────────

    vm.register_native("SOUND.STOP", 1, 1, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        for (auto& v : g_sound.voices)
            if (v.track == t) v.env.state = EnvState::OFF;
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── SOUND.PLAYBUFFER samples, [sample_rate], [channels] ─────
    //
    // Push raw float samples (-1..1) onto the PCM stream. `samples` is
    // a 1D array of numbers; numbers are converted to f32 in order. If
    // the requested sample-rate or channel count differs from the
    // currently-open pcm_stream, we re-open it so SDL's resampler does
    // the conversion.

    vm.register_native("SOUND.PLAYBUFFER", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_sound("SOUND.PLAYBUFFER");
        if (!g_sound.pcm_stream)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.PLAYBUFFER: PCM stream not open");
        if (args[0].type != ValueType::ARRAY)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.PLAYBUFFER: first arg must be an array");

        int rate = (args.size() >= 2) ? (int)args[1].to_int() : g_sound.pcm_sample_rate;
        int channels = (args.size() >= 3) ? (int)args[2].to_int() : g_sound.pcm_channels;
        if (rate < 4000 || rate > 192000) rate = SAMPLE_RATE;
        if (channels < 1 || channels > 2) channels = 1;

        if (rate != g_sound.pcm_sample_rate || channels != g_sound.pcm_channels) {
            if (g_sound.pcm_stream) {
                SDL_DestroyAudioStream(g_sound.pcm_stream);
                g_sound.pcm_stream = nullptr;
            }
            SDL_AudioSpec spec;
            spec.freq = rate;
            spec.format = SDL_AUDIO_F32;
            spec.channels = channels;
            g_sound.pcm_stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (!g_sound.pcm_stream)
                throw jdError(ErrCode::RUNTIME_ERROR,
                    std::string("SOUND.PLAYBUFFER: re-open failed: ") + SDL_GetError());
            SDL_ResumeAudioStreamDevice(g_sound.pcm_stream);
            g_sound.pcm_sample_rate = rate;
            g_sound.pcm_channels = channels;
        }

        auto* arr = args[0].as_array();
        std::vector<float> buf;
        buf.reserve(arr->elements.size());
        for (auto& e : arr->elements) {
            buf.push_back((float)e.to_double());
        }
        if (!buf.empty()) {
            SDL_PutAudioStreamData(g_sound.pcm_stream,
                buf.data(), (int)(buf.size() * sizeof(float)));
        }
        return Value::make_none();
    });

    // ── SOUND.QUEUED() — bytes still pending in PCM stream ──────
    //
    // Useful for keeping the buffer between min/max water-marks without
    // overflowing or underrunning. Returns 0 if PCM stream is closed.
    vm.register_native("SOUND.QUEUED", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!g_sound.pcm_stream) return Value::make_i64(0);
        return Value::make_i64((int64_t)SDL_GetAudioStreamQueued(g_sound.pcm_stream));
    });

    // ── SFX.LOAD id, filepath ───────────────────────────────────

    vm.register_native("SFX.LOAD", 2, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        std::string path = args[1].as_string()->data;
        SDL_AudioSpec spec;
        Uint8* buf; Uint32 len;
        if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len))
            throw jdError(ErrCode::RUNTIME_ERROR, "SFX.LOAD failed: " + std::string(SDL_GetError()));

        // Convert to target format
        SDL_AudioSpec target; target.freq = SAMPLE_RATE; target.format = SDL_AUDIO_F32; target.channels = 2;
        SDL_AudioStream* conv = SDL_CreateAudioStream(&spec, &target);
        SDL_PutAudioStreamData(conv, buf, len);
        SDL_FlushAudioStream(conv);
        SDL_free(buf);

        int avail = SDL_GetAudioStreamAvailable(conv);
        SoundChunk chunk;
        chunk.data.resize(avail / sizeof(float));
        SDL_GetAudioStreamData(conv, chunk.data.data(), avail);
        SDL_DestroyAudioStream(conv);

        g_sound.samples[id] = std::move(chunk);
        return Value::make_none();
    });

    // ── SFX.PLAY id ─────────────────────────────────────────────

    vm.register_native("SFX.PLAY", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        if (g_sound.samples.find(id) == g_sound.samples.end()) return Value::make_none();
        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        for (auto& ch : g_sound.sfx_channels) {
            if (!ch.active) {
                ch.sample_id = id; ch.position = 0; ch.active = true; ch.looping = false;
                break;
            }
        }
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── MUSIC.PLAY id, [loop] ───────────────────────────────────

    vm.register_native("MUSIC.PLAY", 1, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        bool loop = (args.size() >= 2) ? args[1].to_bool() : true;
        if (g_sound.samples.find(id) == g_sound.samples.end()) return Value::make_none();
        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        // Stop current music
        if (g_sound.music_channel >= 0)
            g_sound.sfx_channels[g_sound.music_channel].active = false;
        for (int i = 0; i < MAX_SFX_CHANNELS; i++) {
            if (!g_sound.sfx_channels[i].active) {
                g_sound.sfx_channels[i] = {id, 0, true, loop};
                g_sound.music_channel = i;
                break;
            }
        }
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── MUSIC.STOP ──────────────────────────────────────────────

    vm.register_native("MUSIC.STOP", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        if (g_sound.music_channel >= 0)
            g_sound.sfx_channels[g_sound.music_channel].active = false;
        g_sound.music_channel = -1;
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── SOUND.SEQ layer, pattern$, waveform$ ────────────────────

    vm.register_native("SOUND.SEQ", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        std::string pattern = args[1].as_string()->data;
        std::string wf = args[2].as_string()->data;
        std::transform(wf.begin(), wf.end(), wf.begin(), ::toupper);

        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        auto& layer = g_sound.seq_layers[t];
        layer.events.clear();
        layer.use_voice = (wf == "VOICE");
        if (!layer.use_voice) {
            g_sound.tracks[t].waveform = parse_waveform(wf);
        }
        g_sound.parse_pattern(pattern, layer.events, 0.0f, 1.0f);
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    // ── SOUND.NOTE pattern$, [loop] ──────────────────────────────
    // Plays a note pattern (Strudel-style). Supports parallel: < melody , bass >
    vm.register_native("SOUND.NOTE", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_sound("SOUND");
        std::string pattern = args[0].as_string()->data;
        bool loop = (args.size() >= 2) ? args[1].to_bool() : false;
        bool debug = (args.size() >= 3) ? args[2].to_bool() : false;
        SDL_LockAudioStream(g_sound.stream);
        g_sound.parse_note_pattern(pattern, 0, loop);
        SDL_UnlockAudioStream(g_sound.stream);
        if (debug) {
            for (int t = 0; t < NUM_TRACKS; t++) {
                auto& layer = g_sound.seq_layers[t];
                if (layer.events.empty()) continue;
                fprintf(stderr, "Track %d: %d events, length=%.2f, linear=%d, loop=%d\n",
                    t, (int)layer.events.size(), layer.total_length, layer.is_linear, layer.looping);
                for (auto& ev : layer.events) {
                    fprintf(stderr, "  phase=%.6f dur=%.6f note=%s\n",
                        ev.start_phase, ev.duration, ev.note.c_str());
                }
            }
        }
        return Value::make_none();
    });

    vm.register_native("SOUND.STATS", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        Value r = Value::make_string(
            "triggers=" + std::to_string(g_sound.dbg_triggers) +
            " alloc_fail=" + std::to_string(g_sound.dbg_alloc_fail));
        g_sound.dbg_triggers = 0;
        g_sound.dbg_alloc_fail = 0;
        return r;
    });

    // ── SOUND.BPM bpm ───────────────────────────────────────────

    vm.register_native("SOUND.BPM", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_sound.bpm = (float)args[0].to_double();
        return Value::make_none();
    });

    // ── Track effects ───────────────────────────────────────────

    vm.register_native("SOUND.GAIN", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].gain = (float)args[1].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.PAN", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].pan = (float)args[1].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.FILTER", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].filter_cutoff = (float)args[1].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.EQ", 4, 4, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].eq_low = (float)args[1].to_double();
        g_sound.tracks[t].eq_mid = (float)args[2].to_double();
        g_sound.tracks[t].eq_high = (float)args[3].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.LFO", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].lfo_freq = (float)args[1].to_double();
        g_sound.tracks[t].lfo_depth = (float)args[2].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.FM", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].fm_amount = (float)args[1].to_double();
        g_sound.tracks[t].fm_ratio = (float)args[2].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.UNISON", 4, 4, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].unison_voices = std::clamp((int)args[1].to_int(), 1, 16);
        g_sound.tracks[t].unison_detune = (float)args[2].to_double();
        g_sound.tracks[t].unison_spread = (float)args[3].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.BITCRUSH", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].crush_bits = (float)args[1].to_double();
        g_sound.tracks[t].crush_rate = (float)args[2].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.RINGMOD", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].ring_freq = (float)args[1].to_double();
        g_sound.tracks[t].ring_mix = (float)args[2].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.REVERBSEND", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].reverb_send = (float)args[1].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.DELAYSEND", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[t].delay_send = (float)args[1].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.SIDECHAIN", 3, 3, [](const std::vector<Value>& args) -> Value {
        int target = (int)args[0].to_int(); if (target < 0 || target >= NUM_TRACKS) return Value::make_none();
        g_sound.tracks[target].sidechain_source = (int)args[1].to_int();
        g_sound.tracks[target].sidechain_amount = (float)args[2].to_double(); return Value::make_none();
    });
    vm.register_native("SOUND.SCALE", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        std::string root = args[1].to_string();
        std::transform(root.begin(), root.end(), root.begin(), ::toupper);
        int midi = note_name_to_midi(root);
        if (midi >= 0) g_sound.tracks[t].scale_root_midi = midi;
        std::string mode = args[2].to_string();
        std::transform(mode.begin(), mode.end(), mode.begin(), ::toupper);
        auto sit = g_scales.find(mode);
        if (sit != g_scales.end()) g_sound.tracks[t].scale_intervals = sit->second;
        return Value::make_none();
    });

    // ── Global effects ──────────────────────────────────────────

    vm.register_native("SOUND.DELAY", 4, 4, [](const std::vector<Value>& args) -> Value {
        g_sound.delay_active = args[0].to_bool();
        g_sound.delay_time = (float)args[1].to_double() / 1000.0f; // ms to sec
        g_sound.delay_feedback = (float)args[2].to_double();
        g_sound.delay_mix = (float)args[3].to_double();
        return Value::make_none();
    });
    vm.register_native("SOUND.REVERB", 4, 4, [](const std::vector<Value>& args) -> Value {
        g_sound.reverb.room_size = std::min((float)args[0].to_double(), 0.98f);
        g_sound.reverb.damping = (float)args[1].to_double();
        g_sound.reverb.width = (float)args[2].to_double();
        g_sound.reverb.wet = (float)args[3].to_double();
        return Value::make_none();
    });
    vm.register_native("SOUND.COMPRESSOR", 5, 5, [](const std::vector<Value>& args) -> Value {
        g_sound.compressor.threshold = (float)args[0].to_double();
        g_sound.compressor.ratio = (float)args[1].to_double();
        g_sound.compressor.attack_ms = (float)args[2].to_double();
        g_sound.compressor.release_ms = (float)args[3].to_double();
        g_sound.compressor.makeup = (float)args[4].to_double();
        g_sound.compressor_active = true;
        return Value::make_none();
    });
    vm.register_native("SOUND.DISTORTION", 1, 1, [](const std::vector<Value>& args) -> Value {
        g_sound.distortion_amount = (float)args[0].to_double(); return Value::make_none();
    });

    // ── SOUND.GET_WAVE / SOUND.GET_BUS_WAVE ─────────────────────

    vm.register_native("SOUND.GET_WAVE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        Value r = Value::make_array();
        for (auto f : g_sound.wave_buffer)
            r.as_array()->elements.push_back(Value::make_f64(f));
        return r;
    });
    vm.register_native("SOUND.GET_BUS_WAVE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_int();
        Value r = Value::make_array();
        auto& src = (bus == 0) ? g_sound.reverb_bus_wave : g_sound.delay_bus_wave;
        for (auto f : src)
            r.as_array()->elements.push_back(Value::make_f64(f));
        return r;
    });

    // ── SOUND.DEBUG ──────────────────────────────────────────────

    vm.register_native("SOUND.DEBUG", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        Value r = Value::make_object();
        r.as_object()->set("initialized", Value::make_bool(g_sound.initialized));
        r.as_object()->set("stream", Value::make_bool(g_sound.stream != nullptr));
        // Count active voices
        int active = 0;
        for (auto& v : g_sound.voices)
            if (v.env.state != EnvState::OFF) active++;
        r.as_object()->set("active_voices", Value::make_i64(active));
        // Check if callback was ever called (by checking if wave_buffer has data)
        float max_sample = 0;
        // Generate a quick test: manually run generate_voice for voice 0
        for (int i = 0; i < MAX_VOICES; i++) {
            auto& v = g_sound.voices[i];
            if (v.env.state != EnvState::OFF) {
                float l, r2;
                g_sound.generate_voice(v, l, r2);
                max_sample = std::max(max_sample, std::max(std::abs(l), std::abs(r2)));
            }
        }
        r.as_object()->set("max_sample", Value::make_f64(max_sample));
        return r;
    });

    // ── SOUND.RESET / SOUND.SHUTDOWN ────────────────────────────

    vm.register_native("SOUND.RESET", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!g_sound.stream) return Value::make_none();
        ensure_sound("SOUND"); SDL_LockAudioStream(g_sound.stream);
        for (auto& v : g_sound.voices) v.env.state = EnvState::OFF;
        for (auto& ch : g_sound.sfx_channels) ch.active = false;
        for (auto& l : g_sound.seq_layers) {
            l.events.clear(); l.active_voice_idx = -1;
            l.is_linear = false; l.looping = false;
            l.linear_cursor = 0; l.total_length = 1.0f;
        }
        g_sound.seq_phase = 0;
        g_sound.distortion_amount = 0;
        g_sound.delay_active = false;
        g_sound.compressor_active = false;
        g_sound.reverb.wet = 0;
        std::fill(g_sound.delay_buffer.begin(), g_sound.delay_buffer.end(), 0.0f);
        for (int t = 0; t < NUM_TRACKS; t++) g_sound.tracks[t] = TrackParams{};
        SDL_UnlockAudioStream(g_sound.stream);
        return Value::make_none();
    });

    vm.register_native("SOUND.SHUTDOWN", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        sound_shutdown();
        return Value::make_none();
    });
}

#endif // GFX
