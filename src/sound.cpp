#include "sound.h"
#include "sound_pattern.h"
#include "vm.h"
#include "errors.h"

#if !defined(GFX) && !defined(SOUND_DSP)
// Stub when neither the SDL device (GFX) nor the standalone DSP (SOUND_DSP) is built.
void sound_shutdown() {}
void register_sound_builtins(VM& vm) {
    vm.register_native("SOUND.INIT", [](const std::vector<Value>&) -> Value {
        throw jdError(ErrCode::RUNTIME_ERROR, "SOUND requires a GFX or SOUND build");
    });
}
#else // GFX or SOUND_DSP

// DSP core compiles for both builds; SDL (the audio device) only under GFX.
#ifdef GFX
#include <SDL3/SDL.h>
#define SND_LOCK()   SDL_LockAudioStream(g_sound.stream)
#define SND_UNLOCK() SDL_UnlockAudioStream(g_sound.stream)
#else
#define SND_LOCK()   ((void)0)
#define SND_UNLOCK() ((void)0)
#endif
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <atomic>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr int SAMPLE_RATE = 44100;
static constexpr int NUM_TRACKS = 16;
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
    // Filter state: one-pole, and the two SVF integrators per channel
    float filter_l = 0.0f, filter_r = 0.0f;
    float svf_l1 = 0.0f, svf_l2 = 0.0f, svf_r1 = 0.0f, svf_r2 = 0.0f;
    Envelope fenv;
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
    float sample_gain = 1.0f;
    float pan = -1.0f;  // below 0: the track pan
    float gain = 1.0f;     // per-event level
    bool sample_loop = false;
    // Sequencer timing: frames until the onset, then frames until gate-off
    bool seq_controlled = false;
    int64_t delay_frames = 0;
    int64_t gate_frames = -1;
    // Kit choke group, and whether the onset still has to cut the group
    int choke = 0;
    bool choke_pending = false;
};

// ── Parameter modulation ────────────────────────────────────────

enum ModParam { MP_CUTOFF, MP_RESONANCE, MP_GAIN, MP_PAN, MP_DETUNE, MP_FM,
                MP_CRUSH, MP_REVERB, MP_DELAY, MP_FENV, MP_COUNT };
enum class ModShape { NONE, SINE, COSINE, SAW, ISAW, TRI, SQUARE, RAND, PERLIN };

// A parameter moving between lo and hi, one period every `cycles` cycles.
struct ModSlot {
    ModShape shape = ModShape::NONE;
    float lo = 0.0f, hi = 1.0f, cycles = 1.0f, offset = 0.0f;
    float base = 0.0f;  // the value before modulation, restored when it stops
};

static float hash01(int64_t i) {
    return (float)(jdpat::hash32(0x9e3779b9u, i) / 4294967295.0);
}

// 0..1 at time x, in periods
static float mod_signal(ModShape sh, double x) {
    double fl = std::floor(x);
    float fr = (float)(x - fl);
    switch (sh) {
        case ModShape::SINE:   return 0.5f + 0.5f * std::sin(2.0f * (float)M_PI * fr);
        case ModShape::COSINE: return 0.5f + 0.5f * std::cos(2.0f * (float)M_PI * fr);
        case ModShape::SAW:    return fr;
        case ModShape::ISAW:   return 1.0f - fr;
        case ModShape::TRI:    return fr < 0.5f ? 2.0f * fr : 2.0f - 2.0f * fr;
        case ModShape::SQUARE: return fr < 0.5f ? 0.0f : 1.0f;
        case ModShape::RAND:   return hash01((int64_t)fl);
        case ModShape::PERLIN: {
            float a = hash01((int64_t)fl), b = hash01((int64_t)fl + 1);
            float s = fr * fr * fr * (fr * (fr * 6.0f - 15.0f) + 10.0f);
            return a + (b - a) * s;
        }
        default: return 0.0f;
    }
}

// ── Track Parameters ────────────────────────────────────────────

// One named kit sound: its variants (name:n picks one) and a level.
struct KitSound {
    std::vector<int> ids;
    float gain = 1.0f;
    int choke = 0;  // a hit cuts the sounds of its group still ringing on the track
};

struct TrackParams {
    Waveform waveform = Waveform::SINE;
    float attack = 0.01f, decay = 0.1f, sustain = 0.7f, release = 0.3f;
    float gain = 1.0f, pan = 0.5f;
    float filter_cutoff = 20000.0f;
    float filter_res = 0.0f;
    int filter_mode = 0;  // 0 one-pole low-pass, 1 LP, 2 HP, 3 BP (state-variable)
    float fenv_amount = 0.0f;  // octaves the cutoff opens at the envelope peak
    float fenv_attack = 0.005f, fenv_decay = 0.2f, fenv_sustain = 0.0f, fenv_release = 0.1f;
    ModSlot mods[MP_COUNT];
    float eq_low = 1.0f, eq_mid = 1.0f, eq_high = 1.0f;
    float lfo_freq = 0.0f, lfo_depth = 0.0f;
    float fm_amount = 0.0f, fm_ratio = 1.0f;
    int unison_voices = 1;
    float unison_detune = 0.0f, unison_spread = 0.5f;
    float crush_bits = 16.0f, crush_rate = 1.0f;
    float ring_freq = 0.0f, ring_mix = 0.0f;
    float reverb_send = 0.0f, delay_send = 0.0f;
    int orbit = 0;  // the effect bus the sends go to
    float nudge = 0.0f;  // seconds every note of the track starts late
    int sidechain_source = -1;
    float sidechain_amount = 0.0f;
    // Scale
    int scale_root_midi = 48; // C3
    std::vector<int> scale_intervals = {0,2,4,5,7,9,11}; // Major
    // Sample
    int sample_id = -1;
    float sample_base_freq = 261.63f; // C4
    bool sample_loop = false;
    // Drum kit: pattern atoms name sounds instead of notes
    std::unordered_map<std::string, KitSound> kit;
};

static float* mod_target(TrackParams& tp, int param) {
    switch (param) {
        case MP_CUTOFF:    return &tp.filter_cutoff;
        case MP_RESONANCE: return &tp.filter_res;
        case MP_GAIN:      return &tp.gain;
        case MP_PAN:       return &tp.pan;
        case MP_DETUNE:    return &tp.unison_detune;
        case MP_FM:        return &tp.fm_amount;
        case MP_CRUSH:     return &tp.crush_bits;
        case MP_REVERB:    return &tp.reverb_send;
        case MP_DELAY:     return &tp.delay_send;
        case MP_FENV:      return &tp.fenv_amount;
        default:           return nullptr;
    }
}

// ── Sequencer Event ─────────────────────────────────────────────

struct SeqEvent {
    double start_phase;  // 0..total_length within cycle
    double duration;     // in phase units
    std::string note;    // note name or frequency
    float probability = 1.0f;
    float pan = -1.0f;  // below 0: the track pan
    float gain = 1.0f;     // per-event level
    float legato = -1.0f;  // gate as a share of the step; below 0: the default
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
    // Cyclic (SOUND.SEQ) mode: `events` holds the cycle `built_cycle`.
    // A new pattern waits in `pending` until the next cycle starts.
    jdpat::NodeP pat, pending;
    bool has_pending = false;
    int64_t built_cycle = INT64_MIN;
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

static constexpr int NUM_ORBITS = 4;

// One effect bus: a delay and a reverb fed by the sends of its tracks.
// Buffers are allocated when the bus is first used.
struct FxBus {
    ReverbModule reverb;
    std::vector<float> delay_buffer;
    int delay_head = 0;
    float delay_time = 0.3f, delay_feedback = 0.3f, delay_mix = 0.3f;
    bool delay_active = false;
    bool ready = false;

    void prepare(float wet) {
        if (ready) return;
        reverb.init();
        reverb.wet = wet;
        delay_buffer.assign(SAMPLE_RATE * 2 * 2, 0.0f);  // 2 s stereo
        ready = true;
    }

    // Adds this bus's delay output to the mix
    void run_delay(float in_l, float in_r, float& mix_l, float& mix_r) {
        if (!delay_active || delay_buffer.empty()) return;
        int size = (int)delay_buffer.size();
        int delay_samples = std::min((int)(delay_time * SAMPLE_RATE) * 2, size - 2);
        int read_pos = (delay_head - delay_samples + size) % size;
        float dl = delay_buffer[read_pos], dr = delay_buffer[read_pos + 1 < size ? read_pos + 1 : 0];
        delay_buffer[delay_head] = in_l + dl * delay_feedback;
        delay_buffer[(delay_head + 1) % size] = in_r + dr * delay_feedback;
        delay_head = (delay_head + 2) % size;
        mix_l += dl * delay_mix; mix_r += dr * delay_mix;
    }

    // Adds this bus's reverb output to the mix
    void run_reverb(float in_l, float in_r, float& mix_l, float& mix_r) {
        if (!ready || reverb.wet <= 0) return;
        float rl, rr;
        reverb.process((in_l + in_r) * 0.5f, rl, rr);
        mix_l += rl * reverb.wet * 0.2f;
        mix_r += rr * reverb.wet * 0.2f;
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
#ifdef GFX
    SDL_AudioStream* stream = nullptr;
    // Push-mode stream for SOUND.PLAYBUFFER: scripts queue raw float32
    // samples directly, the SDL audio mixer combines this stream's
    // output with the synth's `stream` on the same device.
    SDL_AudioStream* pcm_stream = nullptr;
#endif
    int pcm_sample_rate = SAMPLE_RATE;
    int pcm_channels = 1;

    // Voices
    Voice voices[MAX_VOICES];
    TrackParams tracks[NUM_TRACKS];
    float track_envelopes[NUM_TRACKS] = {};

    // SFX
    std::unordered_map<int, SoundChunk> samples;
    // Kit samples loaded by path, under ids above any SFX.LOAD id
    std::unordered_map<std::string, int> kit_paths;
    int next_kit_id = 1 << 30;
    SfxChannel sfx_channels[MAX_SFX_CHANNELS];
    int music_channel = -1;

    // Sequencer
    float bpm = 120.0f;
    double seq_time = 0.0;  // sequencer clock in cycles since start
    // The clock as frames counted from seq_base at tempo seq_bpm, so long
    // renders add no rounding drift
    double seq_base = 0.0, seq_bpm = 0.0;
    int64_t seq_frames = 0;

    void reset_clock() { seq_time = 0.0; seq_base = 0.0; seq_frames = 0; seq_bpm = bpm; }

    // Commands queued by SOUND.AT, applied by the mixer when the clock
    // reaches their cycle; equal cycles keep the order they were sent in
    struct SchedCmd {
        double at;
        uint64_t order;
        std::string name;
        std::function<Value(const std::vector<Value>&)> fn;
        std::vector<Value> args;
    };
    std::vector<SchedCmd> sched;
    uint64_t sched_order = 0;
    double sched_at = -1.0;   // the SOUND.AT target; below 0 commands apply at once
    bool in_sched = false;    // true while the mixer applies a queued command

    // Applies every queued command due by cycle `until`, earliest first
    void run_sched(double until) {
        std::vector<SchedCmd> due;
        for (size_t i = 0; i < sched.size();) {
            if (sched[i].at <= until) {
                due.push_back(std::move(sched[i]));
                sched[i] = std::move(sched.back());
                sched.pop_back();
            } else {
                i++;
            }
        }
        std::sort(due.begin(), due.end(), [](const SchedCmd& a, const SchedCmd& b) {
            return a.at != b.at ? a.at < b.at : a.order < b.order;
        });
        in_sched = true;
        for (auto& c : due) {
            try { c.fn(c.args); }
            catch (const std::exception& e) {
                fprintf(stderr, "SOUND.AT %g: %s: %s\n", c.at, c.name.c_str(), e.what());
            }
        }
        in_sched = false;
    }

    double next_sched_at() const {
        double m = sched.front().at;
        for (auto& c : sched) m = std::min(m, c.at);
        return m;
    }
    SeqLayer seq_layers[NUM_TRACKS];
    int dbg_triggers = 0;
    int dbg_alloc_fail = 0;

    // Effect buses: track sends go to the bus of the track's orbit
    FxBus buses[NUM_ORBITS];
    Compressor compressor;
    bool compressor_active = false;
    // Distortion
    float distortion_amount = 0.0f;
    // Master: false divides the mix by sqrt(active voices), true scales
    // every voice by a fixed headroom
    bool master_fixed = false;
    float master_headroom = 0.5f;

    // Visualization
    std::vector<float> wave_buffer;
    std::vector<float> reverb_bus_wave;
    std::vector<float> delay_bus_wave;
    size_t wave_pos = 0;  // next write in the three wave rings

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
        // Nothing is releasing: steal the quietest held note instead of
        // returning -1. A program that never calls SOUND.RELEASE would
        // otherwise fill the pool with sustaining voices and go silent
        // for good - including its sound effects.
        if (best < 0) {
            best_level = 999.0f;
            for (int i = 0; i < MAX_VOICES; i++) {
                if (voices[i].env.state == EnvState::SUSTAIN && voices[i].env.level < best_level) {
                    best = i; best_level = voices[i].env.level;
                }
            }
        }
        if (best >= 0) { voices[best] = Voice{}; voices[best].track = track; }
        return best;
    }

    // Releases the sounding voices in the choke group of voice `vi`.
    void choke(int vi) {
        const auto& src = voices[vi];
        for (int i = 0; i < MAX_VOICES; i++) {
            auto& v = voices[i];
            if (i == vi || v.track != src.track || v.choke != src.choke) continue;
            if (v.env.state == EnvState::OFF || v.delay_frames > 0 || v.choke_pending) continue;
            v.env.gate_off();
        }
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
        if (v.sample_id >= 0) {
            auto sit = samples.find(v.sample_id);
            if (sit != samples.end()) {
                auto& chunk = sit->second;
                float rate = freq / tp.sample_base_freq;
                if (v.sample_pos < chunk.data.size() / 2) {
                    size_t idx = (size_t)v.sample_pos * 2;
                    sample_l = (idx < chunk.data.size()) ? chunk.data[idx] : 0;
                    sample_r = (idx + 1 < chunk.data.size()) ? chunk.data[idx + 1] : sample_l;
                    sample_l *= v.sample_gain;
                    sample_r *= v.sample_gain;
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

        // Filter, its cutoff opened by the filter envelope
        float fe = 0.0f;
        if (tp.fenv_amount != 0.0f) {
            if (v.env.state == EnvState::RELEASE &&
                v.fenv.state != EnvState::RELEASE && v.fenv.state != EnvState::OFF)
                v.fenv.gate_off();
            fe = v.fenv.process(dt);
        }
        if (tp.filter_mode != 0 || tp.filter_cutoff < 19999.0f || tp.fenv_amount != 0.0f) {
            float fc = tp.filter_cutoff;
            if (fe != 0.0f) fc *= std::exp2(tp.fenv_amount * fe);
            fc = std::clamp(fc, 10.0f, 0.45f * SAMPLE_RATE);
            if (tp.filter_mode == 0) {
                float rc = 1.0f / (2.0f * (float)M_PI * fc);
                float alpha = dt / (rc + dt);
                v.filter_l += alpha * (sample_l - v.filter_l); sample_l = v.filter_l;
                v.filter_r += alpha * (sample_r - v.filter_r); sample_r = v.filter_r;
            } else {
                // Topology-preserving state-variable filter (Simper)
                float g = std::tan((float)M_PI * fc / SAMPLE_RATE);
                float k = 2.0f - 1.97f * std::clamp(tp.filter_res, 0.0f, 1.0f);
                float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                auto svf = [&](float x, float& ic1, float& ic2) {
                    float v3 = x - ic2;
                    float v1 = a1 * ic1 + a2 * v3;
                    float v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2.0f * v1 - ic1;
                    ic2 = 2.0f * v2 - ic2;
                    if (tp.filter_mode == 2) return x - k * v1 - v2;
                    if (tp.filter_mode == 3) return v1;
                    return v2;
                };
                sample_l = svf(sample_l, v.svf_l1, v.svf_l2);
                sample_r = svf(sample_r, v.svf_r1, v.svf_r2);
            }
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

        float pan = v.pan >= 0.0f ? v.pan : tp.pan;
        out_l = sample_l * env * tp.gain * v.gain * (1.0f - pan) * 2.0f;
        out_r = sample_r * env * tp.gain * v.gain * pan * 2.0f;
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
            layer.pat.reset(); layer.pending.reset(); layer.has_pending = false;
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

    // Envelopes for a voice that starts a note on track `tp`
    static void start_envelopes(Voice& v, const TrackParams& tp) {
        v.env = Envelope{tp.attack, tp.decay, tp.sustain, tp.release};
        v.env.gate_on();
        v.fenv = Envelope{tp.fenv_attack, tp.fenv_decay, tp.fenv_sustain, tp.fenv_release};
        v.fenv.gate_on();
    }

    // Starts the voice for one sequencer event, `offset` cycles into the buffer.
    static void trigger_event(SoundSystem* sys, int t, const SeqEvent& ev,
                              double offset, double phase_inc) {
        if (ev.probability < 1.0f && ((float)rand() / RAND_MAX) > ev.probability)
            return;
        auto& tp = sys->tracks[t];
        if (!tp.kit.empty()) {
            std::string name = ev.note;
            int variant = 0;
            size_t colon = name.find(':');
            if (colon != std::string::npos) {
                variant = std::atoi(name.c_str() + colon + 1);
                name.resize(colon);
            }
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            auto it = tp.kit.find(name);
            if (it == tp.kit.end() || it->second.ids.empty()) return;
            const auto& ids = it->second.ids;
            int n = (int)ids.size();
            int vi = sys->allocate_voice(t);
            if (vi < 0) { sys->dbg_alloc_fail++; return; }
            sys->dbg_triggers++;
            auto& v = sys->voices[vi];
            v.frequency = tp.sample_base_freq;
            start_envelopes(v, tp);
            v.sample_id = ids[((variant % n) + n) % n];
            v.sample_gain = it->second.gain;
            v.sample_loop = false;
            v.seq_controlled = true;
            v.pan = ev.pan;
            v.gain = ev.gain;
            v.delay_frames = (int64_t)(offset / phase_inc) + (int64_t)(tp.nudge * SAMPLE_RATE);
            v.gate_frames = ev.legato >= 0.0f ? (int64_t)(ev.duration / phase_inc) : -1;
            v.choke = it->second.choke;
            v.choke_pending = v.choke > 0;
            return;
        }
        float freq = 0;
        int midi = note_name_to_midi(ev.note);
        if (midi >= 0) {
            freq = midi_to_freq(midi);
        } else {
            try { int deg = std::stoi(ev.note); freq = sys->degree_to_freq(t, deg); }
            catch (...) { try { freq = std::stof(ev.note); } catch (...) { return; } }
        }
        int vi = sys->allocate_voice(t);
        if (vi < 0) { sys->dbg_alloc_fail++; return; }
        sys->dbg_triggers++;
        auto& v = sys->voices[vi];
        v.frequency = freq;
        v.phase = 0.0f;
        start_envelopes(v, tp);
        v.seq_controlled = true;
        v.pan = ev.pan;
        v.gain = ev.gain;
        v.delay_frames = (int64_t)(offset / phase_inc) + (int64_t)(tp.nudge * SAMPLE_RATE);
        v.gate_frames = (int64_t)(ev.duration / phase_inc);
        if (tp.waveform == Waveform::SAMPLE) {
            v.sample_id = tp.sample_id;
            v.sample_loop = tp.sample_loop;
            v.sample_pos = 0;
        }
        sys->seq_layers[t].active_voice_idx = vi;
    }

    // Moves every modulated track parameter to its value at cycle time `now`.
    void apply_mods(double now) {
        for (int t = 0; t < NUM_TRACKS; t++) {
            auto& tp = tracks[t];
            for (int m = 0; m < MP_COUNT; m++) {
                auto& ms = tp.mods[m];
                if (ms.shape == ModShape::NONE) continue;
                double x = now / std::max(ms.cycles, 0.0001f) + ms.offset;
                float val = ms.lo + (ms.hi - ms.lo) * mod_signal(ms.shape, x);
                float* dst = mod_target(tp, m);
                if (!dst) continue;
                if (m == MP_CRUSH) val = std::clamp(val, 1.0f, 16.0f);
                *dst = val;
            }
        }
    }

    // Linear up to the knee, then bends smoothly towards full scale
    static float soft_clip(float x) {
        const float knee = 0.8f;
        float a = std::abs(x);
        if (a <= knee) return x;
        float y = knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee));
        return x < 0 ? -y : y;
    }

    // ── Audio callback ──────────────────────────────────────────

    // SDL-free render: fill `buffer` with num_frames interleaved stereo samples.
    // Works in short blocks, so a long render schedules no more notes at once
    // than the audio device would.
    static void render_frames(SoundSystem* sys, float* buffer, int num_frames) {
        const int block = 256;
        int done = 0;
        while (done < num_frames) {
            int n = std::min(block, num_frames - done);
            // A block ends on the last frame before a queued command is due,
            // so a pattern it starts still sees the downbeat of its cycle
            if (!sys->sched.empty()) {
                double cps = sys->bpm / 60.0 / 4.0;
                double at = sys->next_sched_at();
                double k = std::floor((at - sys->seq_time) * SAMPLE_RATE / cps + 1e-9);
                if (k <= 0) {
                    sys->run_sched(std::max(at, sys->seq_time));
                    continue;
                }
                if (k < n) n = (int)k;
            }
            render_block(sys, buffer + done * 2, n);
            done += n;
        }
    }

    static void render_block(SoundSystem* sys, float* buffer, int num_frames) {
        double dt = 1.0 / SAMPLE_RATE;

        // Sequencer clock, in cycles
        double cps = sys->bpm / 60.0 / 4.0; // cycles per second
        double phase_inc = dt * cps;
        double buffer_dur = num_frames * phase_inc;
        if (sys->bpm != sys->seq_bpm) {
            sys->seq_base = sys->seq_time;
            sys->seq_frames = 0;
            sys->seq_bpm = sys->bpm;
        }
        double t0 = sys->seq_base + (double)sys->seq_frames * cps / SAMPLE_RATE;
        double t1 = sys->seq_base + (double)(sys->seq_frames + num_frames) * cps / SAMPLE_RATE;

        for (int t = 0; t < NUM_TRACKS; t++) {
            auto& layer = sys->seq_layers[t];

            if (!layer.is_linear) {
                if (!layer.pat && !layer.has_pending) continue;
                for (int64_t c = (int64_t)std::floor(t0); (double)c < t1; c++) {
                    if (layer.built_cycle != c) {
                        if (layer.has_pending) {
                            layer.pat = layer.pending;
                            layer.pending.reset();
                            layer.has_pending = false;
                        }
                        layer.events.clear();
                        for (auto& e : jdpat::query_cycle(layer.pat, c)) {
                            SeqEvent ev;
                            ev.start_phase = e.start;
                            ev.duration = e.dur * (e.legato >= 0.0f ? e.legato : 0.8);
                            ev.note = e.note;
                            ev.probability = e.prob;
                            ev.pan = e.pan;
                            ev.gain = e.gain;
                            ev.legato = e.legato;
                            layer.events.push_back(ev);
                        }
                        layer.built_cycle = c;
                    }
                    double ws = std::max(t0, (double)c) - (double)c;
                    double we = std::min(t1, (double)c + 1.0) - (double)c;
                    for (auto& ev : layer.events)
                        if (ev.start_phase >= ws && ev.start_phase < we)
                            trigger_event(sys, t, ev, (double)c + ev.start_phase - t0, phase_inc);
                }
                continue;
            }

            if (layer.events.empty()) continue;
            double start_window = layer.linear_cursor;
            double end_window = start_window + buffer_dur;
            bool layer_wrapped = false;
            layer.linear_cursor += buffer_dur;

            if (end_window >= layer.total_length) {
                if (layer.looping) {
                    layer.linear_cursor -= layer.total_length;
                    layer_wrapped = true;
                } else if (start_window >= layer.total_length) {
                    layer.events.clear();
                    continue;
                }
            }

            for (auto& ev : layer.events) {
                double offset = -1.0;
                if (ev.start_phase >= start_window && ev.start_phase < end_window)
                    offset = ev.start_phase - start_window;
                else if (layer_wrapped && ev.start_phase >= 0.0 &&
                         ev.start_phase <= end_window - layer.total_length)
                    offset = ev.start_phase + layer.total_length - start_window;
                if (offset < 0.0 || ev.triggered) continue;
                ev.triggered = true;
                trigger_event(sys, t, ev, offset, phase_inc);
            }
            if (layer_wrapped)
                for (auto& ev : layer.events) ev.triggered = false;
        }

        sys->seq_time = t1;
        sys->seq_frames += num_frames;

        // Per-sample generation
        for (int f = 0; f < num_frames; f++) {
            float mix_l = 0, mix_r = 0;
            float rev_l[NUM_ORBITS] = {}, rev_r[NUM_ORBITS] = {};
            float del_l[NUM_ORBITS] = {}, del_r[NUM_ORBITS] = {};
            int active = 0;

            // Reset track envelopes
            float t_env[NUM_TRACKS] = {};

            // Synth voices
            if ((f & 31) == 0) sys->apply_mods(t0 + f * phase_inc);
            for (int i = 0; i < MAX_VOICES; i++) {
                auto& v = sys->voices[i];
                if (v.env.state == EnvState::OFF || v.track < 0) continue;
                if (v.delay_frames > 0) { v.delay_frames--; continue; }
                if (v.choke_pending) { v.choke_pending = false; sys->choke(i); }
                if (v.seq_controlled && v.gate_frames >= 0 && v.gate_frames-- == 0) {
                    v.env.gate_off();
                    v.seq_controlled = false;
                }
                float vl, vr;
                sys->generate_voice(v, vl, vr);
                mix_l += vl; mix_r += vr;
                auto& tp = sys->tracks[v.track];
                int o = tp.orbit;
                rev_l[o] += vl * tp.reverb_send; rev_r[o] += vr * tp.reverb_send;
                del_l[o] += vl * tp.delay_send; del_r[o] += vr * tp.delay_send;
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

            if (sys->master_fixed) {
                float h = sys->master_headroom;
                mix_l *= h; mix_r *= h;
                for (int o = 0; o < NUM_ORBITS; o++) {
                    rev_l[o] *= h; rev_r[o] *= h;
                    del_l[o] *= h; del_r[o] *= h;
                }
            } else if (active > 1) {
                float norm = 1.0f / std::sqrt((float)active);
                mix_l *= norm; mix_r *= norm;
            }

            // Store track envelopes for sidechain
            for (int t = 0; t < NUM_TRACKS; t++)
                sys->track_envelopes[t] = t_env[t];

            for (int o = 0; o < NUM_ORBITS; o++)
                sys->buses[o].run_delay(del_l[o], del_r[o], mix_l, mix_r);

            // Distortion
            if (sys->distortion_amount > 0) {
                float d = 1.0f + sys->distortion_amount;
                mix_l = std::tanh(mix_l * d); mix_r = std::tanh(mix_r * d);
            }

            for (int o = 0; o < NUM_ORBITS; o++)
                sys->buses[o].run_reverb(rev_l[o], rev_r[o], mix_l, mix_r);

            // Compressor
            if (sys->compressor_active) {
                sys->compressor.process(mix_l, mix_r);
            }

            buffer[f * 2] = soft_clip(mix_l);
            buffer[f * 2 + 1] = soft_clip(mix_r);
            if (!sys->wave_buffer.empty()) {
                float rs = 0, ds = 0;
                for (int o = 0; o < NUM_ORBITS; o++) {
                    rs += rev_l[o] + rev_r[o];
                    ds += del_l[o] + del_r[o];
                }
                size_t w = sys->wave_pos;
                sys->wave_buffer[w] = 0.5f * (buffer[f * 2] + buffer[f * 2 + 1]);
                sys->reverb_bus_wave[w] = 0.5f * rs;
                sys->delay_bus_wave[w] = 0.5f * ds;
                sys->wave_pos = (w + 1) % sys->wave_buffer.size();
            }
        }
    }

#ifdef GFX
    // SDL callback: render into a temp buffer, hand it to the device stream.
    static void audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len) {
        (void)total_len;
        auto* sys = (SoundSystem*)userdata;
        int num_floats = additional_len / sizeof(float);
        std::vector<float> buffer(num_floats, 0.0f);
        render_frames(sys, buffer.data(), num_floats / 2);
        SDL_PutAudioStreamData(stream, buffer.data(), additional_len);
    }
#endif
} g_sound;

// ── Helpers ─────────────────────────────────────────────────────

static void ensure_sound(const char* fn) {
    if (!g_sound.initialized)
        throw jdError(ErrCode::RUNTIME_ERROR, std::string(fn) + ": call SOUND.INIT first");
}

// ── Public API ──────────────────────────────────────────────────

void sound_shutdown() {
#ifdef GFX
    if (g_sound.stream) {
        SDL_DestroyAudioStream(g_sound.stream);
        g_sound.stream = nullptr;
    }
    if (g_sound.pcm_stream) {
        SDL_DestroyAudioStream(g_sound.pcm_stream);
        g_sound.pcm_stream = nullptr;
    }
#endif
    g_sound.initialized = false;
}

// Pull-mode render entry point: fill `out` with num_frames interleaved
// stereo samples from the live sequencer. Used by embed hosts (Godot) that
// drive their own audio device. Returns frames produced.
int sound_render(float* out, int num_frames) {
    if (!g_sound.initialized || num_frames <= 0) return 0;
    SoundSystem::render_frames(&g_sound, out, num_frames);
    return num_frames;
}

// ── Built-in drum kit, synthesized (SOUND.KIT track, "SYNTH") ──

namespace synthkit {

struct Rng {
    uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 8388608.0f - 1.0f; }
};

static std::vector<float> buf(float sec) { return std::vector<float>((size_t)(sec * SAMPLE_RATE), 0.0f); }
static float t_of(size_t i) { return (float)i / SAMPLE_RATE; }
static float decay(float t, float tau) { return std::exp(-t / tau); }

// State-variable filter over a buffer: mode 0 LP, 1 HP, 2 BP
static void svf(std::vector<float>& x, float fc, float q, int mode) {
    float g = std::tan((float)M_PI * fc / SAMPLE_RATE), k = 1.0f / q;
    float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    float ic1 = 0, ic2 = 0;
    for (auto& s : x) {
        float v3 = s - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        s = mode == 0 ? v2 : mode == 1 ? s - k * v1 - v2 : v1;
    }
}

static std::vector<float> noise(float len, uint32_t seed) {
    auto x = buf(len);
    Rng r{seed};
    for (auto& s : x) s = r.next();
    return x;
}

// Sine falling from f0 to f1 with time constant `sweep`, plus a noise click
static std::vector<float> tone_drum(float f0, float f1, float sweep, float tau, float len, float click) {
    auto x = buf(len);
    Rng r{7};
    float ph = 0;
    for (size_t i = 0; i < x.size(); i++) {
        float t = t_of(i);
        ph += (f1 + (f0 - f1) * decay(t, sweep)) / SAMPLE_RATE;
        x[i] = std::sin(2.0f * (float)M_PI * ph) * decay(t, tau);
        if (i < 100) x[i] += click * r.next() * (1.0f - i / 100.0f);
    }
    return x;
}

// Six detuned square waves through a high-pass: the analog cymbal and hat source
static std::vector<float> metal(float len, float tau, float hp, uint32_t seed) {
    static const float fr[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
    auto x = buf(len);
    auto n = noise(len, seed);
    for (size_t i = 0; i < x.size(); i++) {
        float t = t_of(i), s = 0;
        for (float f : fr) s += std::fmod(t * f, 1.0f) < 0.5f ? 1.0f : -1.0f;
        x[i] = s / 6.0f + 0.3f * n[i];
    }
    svf(x, hp, 0.7f, 1);
    svf(x, hp * 1.4f, 0.9f, 2);
    for (size_t i = 0; i < x.size(); i++) x[i] *= decay(t_of(i), tau);
    return x;
}

static void scale_peak(std::vector<float>& x, float peak) {
    float m = 0;
    for (float s : x) m = std::max(m, std::abs(s));
    if (m > 0) for (auto& s : x) s *= peak / m;
}

// The mono samples of one sound; empty for an unknown name
static std::vector<std::vector<float>> make(const std::string& name) {
    std::vector<std::vector<float>> out;
    if (name == "bd") {
        out.push_back(tone_drum(170, 52, 0.025f, 0.16f, 0.8f, 0.25f));
        out.push_back(tone_drum(120, 46, 0.05f, 0.55f, 1.6f, 0.12f));
    } else if (name == "sd") {
        auto x = buf(0.4f);
        auto n = noise(0.4f, 11);
        svf(n, 1800, 0.6f, 1);
        for (size_t i = 0; i < x.size(); i++) {
            float t = t_of(i);
            float body = std::sin(2.0f * (float)M_PI * 185 * t) + 0.6f * std::sin(2.0f * (float)M_PI * 330 * t);
            x[i] = 0.5f * body * decay(t, 0.05f) + 0.9f * n[i] * decay(t, 0.12f);
        }
        out.push_back(x);
    } else if (name == "hh") {
        out.push_back(metal(0.25f, 0.035f, 7500, 21));
    } else if (name == "oh") {
        out.push_back(metal(1.0f, 0.3f, 7500, 22));
    } else if (name == "cr") {
        auto x = metal(2.5f, 0.8f, 5000, 23);
        auto n = noise(2.5f, 24);
        svf(n, 6000, 0.7f, 1);
        for (size_t i = 0; i < x.size(); i++) x[i] += 0.5f * n[i] * decay(t_of(i), 0.9f);
        out.push_back(x);
    } else if (name == "cp") {
        auto x = noise(0.4f, 31);
        svf(x, 1200, 1.5f, 2);
        for (size_t i = 0; i < x.size(); i++) {
            float t = t_of(i), e = 0;
            for (int b = 0; b < 3; b++) {
                float tb = t - 0.011f * b;
                if (tb >= 0) e = std::max(e, decay(tb, 0.004f));
            }
            if (t >= 0.033f) e = std::max(e, decay(t - 0.033f, 0.11f));
            x[i] *= e;
        }
        out.push_back(x);
    } else if (name == "lt" || name == "mt" || name == "ht") {
        float f = name == "lt" ? 85.0f : name == "mt" ? 125.0f : 180.0f;
        out.push_back(tone_drum(f * 1.5f, f, 0.06f, 0.22f, 0.8f, 0.04f));
    } else if (name == "rim") {
        auto x = buf(0.08f);
        for (size_t i = 0; i < x.size(); i++) {
            float t = t_of(i);
            x[i] = std::sin(2.0f * (float)M_PI * 1700 * t) * decay(t, 0.008f)
                 + 0.6f * std::sin(2.0f * (float)M_PI * 480 * t) * decay(t, 0.012f);
        }
        out.push_back(x);
    } else if (name == "cb") {
        auto x = buf(0.6f);
        for (size_t i = 0; i < x.size(); i++) {
            float t = t_of(i);
            float s = (std::fmod(t * 540, 1.0f) < 0.5f ? 1.0f : -1.0f) + (std::fmod(t * 800, 1.0f) < 0.5f ? 1.0f : -1.0f);
            x[i] = s * (0.6f * decay(t, 0.015f) + 0.4f * decay(t, 0.25f));
        }
        svf(x, 800, 2.0f, 2);
        out.push_back(x);
    } else if (name == "cl") {
        auto x = buf(0.12f);
        for (size_t i = 0; i < x.size(); i++)
            x[i] = std::sin(2.0f * (float)M_PI * 2500 * t_of(i)) * decay(t_of(i), 0.02f);
        out.push_back(x);
    } else if (name == "sh") {
        auto x = noise(0.15f, 41);
        svf(x, 5000, 0.7f, 1);
        for (size_t i = 0; i < x.size(); i++) {
            float t = t_of(i);
            x[i] *= std::min(1.0f, t / 0.01f) * decay(t, 0.035f);
        }
        out.push_back(x);
    }
    for (auto& x : out) {
        size_t fade = std::min(x.size(), (size_t)(0.01f * SAMPLE_RATE));
        for (size_t i = 0; i < fade; i++) x[x.size() - 1 - i] *= (float)i / fade;
        scale_peak(x, 0.9f);
    }
    return out;
}

struct Entry { const char* name; float gain; int choke; };
static const Entry entries[] = {
    {"bd", 1.0f, 0}, {"sd", 0.9f, 0}, {"hh", 0.55f, 1}, {"oh", 0.5f, 1},
    {"cr", 0.45f, 0}, {"cp", 0.85f, 0}, {"lt", 0.9f, 0}, {"mt", 0.9f, 0},
    {"ht", 0.9f, 0}, {"rim", 0.7f, 0}, {"cb", 0.55f, 0}, {"cl", 0.6f, 0},
    {"sh", 0.4f, 0},
};

}  // namespace synthkit

// Reads a WAV file into an interleaved-stereo chunk at the engine rate.
static SoundChunk load_wav_file(const std::string& path, const std::string& who) {
#ifndef GFX
    // SDL-free WAV loader for headless/SOUND_DSP builds: parses canonical
    // RIFF/PCM (8/16/24/32-bit int, 32-bit float; mono or stereo) and
    // resamples to the engine rate, producing interleaved-stereo float.
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw jdError(ErrCode::RUNTIME_ERROR, who + " failed: cannot open " + path);
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsz < 44) { fclose(f); throw jdError(ErrCode::RUNTIME_ERROR, who + " failed: file too small: " + path); }
    std::vector<uint8_t> b((size_t)fsz);
    size_t got = fread(b.data(), 1, (size_t)fsz, f);
    fclose(f);
    if (got != (size_t)fsz) throw jdError(ErrCode::RUNTIME_ERROR, who + " failed: short read: " + path);

    auto rd16 = [&](size_t o) -> uint16_t { return (uint16_t)(b[o] | (b[o + 1] << 8)); };
    auto rd32 = [&](size_t o) -> uint32_t {
        return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
    };
    if (memcmp(b.data(), "RIFF", 4) != 0 || memcmp(b.data() + 8, "WAVE", 4) != 0)
        throw jdError(ErrCode::RUNTIME_ERROR, who + " failed: not a WAV: " + path);

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t srate = 0, dataOff = 0, dataLen = 0;
    size_t p = 12;
    while (p + 8 <= (size_t)fsz) {
        uint32_t csz = rd32(p + 4);
        if (memcmp(&b[p], "fmt ", 4) == 0 && p + 24 <= (size_t)fsz) {
            fmt = rd16(p + 8); channels = rd16(p + 10); srate = rd32(p + 12); bits = rd16(p + 22);
        } else if (memcmp(&b[p], "data", 4) == 0) {
            dataOff = (uint32_t)(p + 8); dataLen = csz;
        }
        p += 8 + csz + (csz & 1); // chunks are word-aligned
    }
    if (dataOff == 0 || channels == 0 || bits == 0)
        throw jdError(ErrCode::RUNTIME_ERROR, who + " failed: malformed WAV: " + path);
    if ((size_t)dataOff + dataLen > (size_t)fsz) dataLen = (uint32_t)fsz - dataOff;

    size_t bytesPerSample = bits / 8;
    size_t frameStride = bytesPerSample * channels;
    size_t frameCount = frameStride ? (dataLen / frameStride) : 0;

    std::vector<float> srcL(frameCount), srcR(frameCount);
    for (size_t i = 0; i < frameCount; ++i) {
        for (int c = 0; c < channels; ++c) {
            size_t so = (size_t)dataOff + i * frameStride + (size_t)c * bytesPerSample;
            float s = 0.0f;
            if (fmt == 3 && bits == 32) { float v; memcpy(&v, &b[so], 4); s = v; }
            else if (bits == 16) { s = (int16_t)rd16(so) / 32768.0f; }
            else if (bits == 8)  { s = ((int)b[so] - 128) / 128.0f; }
            else if (bits == 24) { int32_t v = (int32_t)(b[so] | (b[so + 1] << 8) | (b[so + 2] << 16)); if (v & 0x800000) v |= ~0xFFFFFF; s = v / 8388608.0f; }
            else if (bits == 32) { s = (int32_t)rd32(so) / 2147483648.0f; }
            if (c == 0) srcL[i] = s;
            if (c == 1) srcR[i] = s;
        }
        if (channels == 1) srcR[i] = srcL[i];
    }

    SoundChunk chunk;
    if (srate == 0) srate = SAMPLE_RATE;
    if (srate == (uint32_t)SAMPLE_RATE) {
        chunk.data.resize(frameCount * 2);
        for (size_t i = 0; i < frameCount; ++i) { chunk.data[i * 2] = srcL[i]; chunk.data[i * 2 + 1] = srcR[i]; }
    } else {
        double ratio = (double)SAMPLE_RATE / (double)srate;
        size_t outFrames = (size_t)(frameCount * ratio);
        chunk.data.resize(outFrames * 2);
        for (size_t i = 0; i < outFrames; ++i) {
            double sp = i / ratio;
            size_t i0 = (size_t)sp; double fr = sp - i0;
            size_t i1 = (i0 + 1 < frameCount) ? i0 + 1 : i0;
            chunk.data[i * 2]     = (float)(srcL[i0] * (1.0 - fr) + srcL[i1] * fr);
            chunk.data[i * 2 + 1] = (float)(srcR[i0] * (1.0 - fr) + srcR[i1] * fr);
        }
    }

    return chunk;
#else

    SDL_AudioSpec spec;
    Uint8* buf; Uint32 len;
    if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len))
        throw jdError(ErrCode::RUNTIME_ERROR, who + " failed: " + std::string(SDL_GetError()));

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

    return chunk;
#endif
}

void register_sound_builtins(VM& vm) {
    init_note_map();

    // ── SOUND.INIT ──────────────────────────────────────────────

    // SOUND.INIT [mode$]: "PULL" opens no audio device; SOUND.RENDER is
    // then the only clock, which makes a render reproducible.
    vm.register_native("SOUND.INIT", 0, 1, [](const std::vector<Value>& args) -> Value {
        if (g_sound.initialized) return Value::make_none();
        bool pull = false;
        if (!args.empty()) {
            std::string m = args[0].to_string();
            std::transform(m.begin(), m.end(), m.begin(), ::toupper);
            if (m == "PULL") pull = true;
            else if (m != "DEVICE")
                throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.INIT: mode is DEVICE or PULL, not " + m);
        }
        (void)pull;

        // Effect buffers - needed in both device and pull mode.
        g_sound.buses[0].prepare(0.3f);
        g_sound.wave_buffer.resize(1024, 0.0f);
        g_sound.reverb_bus_wave.resize(1024, 0.0f);
        g_sound.delay_bus_wave.resize(1024, 0.0f);

#ifdef GFX
        if (pull) {
            g_sound.initialized = true;
            return Value::make_none();
        }
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

        SDL_ResumeAudioStreamDevice(g_sound.stream);

        // PCM push-stream - opened on the same default device. SDL3 mixes
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
#endif

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

    // ── SOUND.KIT track, [kit] ──────────────────────────────────
    // kit is a kit file, or a MAP from sound name to a sample id, a WAV path
    // or an array of them. Pattern atoms on the track then name sounds.

    vm.register_native("SOUND.KIT", 1, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        std::unordered_map<std::string, KitSound> kit;
        std::vector<std::pair<int, SoundChunk>> loaded;
        std::unordered_map<std::string, int> new_ids;
        int next_id = g_sound.next_kit_id;
        auto sample_for = [&](const std::string& path) -> int {
            auto it = g_sound.kit_paths.find(path);
            if (it != g_sound.kit_paths.end()) return it->second;
            auto jt = new_ids.find(path);
            if (jt != new_ids.end()) return jt->second;
            loaded.push_back({next_id, load_wav_file(path, "SOUND.KIT")});
            new_ids[path] = next_id;
            return next_id++;
        };
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        Value kv = args.size() >= 2 ? args[1] : Value::make_none();
        if (kv.type == ValueType::STRING && lower(kv.as_string()->data) == "synth") {
            for (const auto& e : synthkit::entries) {
                KitSound ks;
                ks.gain = e.gain;
                ks.choke = e.choke;
                std::string key = std::string("<synth>/") + e.name;
                if (g_sound.kit_paths.count(key + ":0")) {
                    for (int v = 0; g_sound.kit_paths.count(key + ":" + std::to_string(v)); v++)
                        ks.ids.push_back(g_sound.kit_paths[key + ":" + std::to_string(v)]);
                } else {
                    auto mono = synthkit::make(e.name);
                    for (size_t v = 0; v < mono.size(); v++) {
                        SoundChunk chunk;
                        chunk.data.resize(mono[v].size() * 2);
                        for (size_t i = 0; i < mono[v].size(); i++)
                            chunk.data[i * 2] = chunk.data[i * 2 + 1] = mono[v][i];
                        loaded.push_back({next_id, std::move(chunk)});
                        new_ids[key + ":" + std::to_string(v)] = next_id;
                        ks.ids.push_back(next_id++);
                    }
                }
                kit[e.name] = std::move(ks);
            }
        } else if (kv.type == ValueType::STRING) {
            std::string file = kv.as_string()->data;
            FILE* fp = file.empty() ? nullptr : fopen(file.c_str(), "rb");
            if (!file.empty() && !fp)
                throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.KIT: cannot open " + file);
            std::string text;
            if (fp) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, fp)) > 0) text.append(buf, n);
                fclose(fp);
            }
            std::string dir;
            size_t sl = file.find_last_of("/\\");
            if (sl != std::string::npos) dir = file.substr(0, sl + 1);
            std::istringstream in(text);
            std::string line;
            int lineno = 0;
            while (std::getline(in, line)) {
                lineno++;
                size_t hash = line.find('#');
                if (hash != std::string::npos) line.resize(hash);
                std::istringstream ls(line);
                std::string name, tok;
                if (!(ls >> name)) continue;
                KitSound ks;
                while (ls >> tok) {
                    if (tok.rfind("gain=", 0) == 0) { ks.gain = (float)std::atof(tok.c_str() + 5); continue; }
                    if (tok.rfind("choke=", 0) == 0) { ks.choke = std::atoi(tok.c_str() + 6); continue; }
                    bool absolute = tok[0] == '/' || tok[0] == '\\' || (tok.size() > 1 && tok[1] == ':');
                    ks.ids.push_back(sample_for(absolute ? tok : dir + tok));
                }
                if (ks.ids.empty())
                    throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.KIT: " + file + " line " +
                                  std::to_string(lineno) + ": " + name + " has no sample");
                kit[lower(name)] = std::move(ks);
            }
        } else if (kv.type == ValueType::OBJECT) {
            auto* obj = dynamic_cast<ObjectObj*>(kv.obj);
            if (!obj) throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.KIT: the kit is a file name or a MAP");
            for (auto& field : obj->fields) {
                KitSound ks;
                auto add = [&](const Value& v) {
                    if (v.type == ValueType::STRING)
                        ks.ids.push_back(sample_for(v.as_string()->data));
                    else if (v.type >= ValueType::BYTE && v.type <= ValueType::FLOAT64)
                        ks.ids.push_back((int)v.to_int());
                    else
                        throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.KIT: " + field.first +
                                      " needs a sample id or a WAV path");
                };
                if (field.second.type == ValueType::ARRAY)
                    for (auto& e : field.second.as_array()->elements) add(e);
                else
                    add(field.second);
                if (ks.ids.empty())
                    throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.KIT: " + field.first + " has no sample");
                kit[lower(field.first)] = std::move(ks);
            }
        } else if (kv.type != ValueType::NONE) {
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.KIT: the kit is a file name or a MAP");
        }

        ensure_sound("SOUND"); SND_LOCK();
        for (auto& lc : loaded) g_sound.samples[lc.first] = std::move(lc.second);
        for (auto& p : new_ids) g_sound.kit_paths[p.first] = p.second;
        g_sound.next_kit_id = next_id;
        auto& tp = g_sound.tracks[t];
        tp.kit = std::move(kit);
        if (!tp.kit.empty()) {
            tp.waveform = Waveform::SAMPLE;
            tp.attack = 0.0005f; tp.decay = 0.001f; tp.sustain = 1.0f; tp.release = 0.05f;
        }
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── SOUND.MASTER mode$, [headroom] ──────────────────────────
    // "AUTO" divides the mix by the square root of the sounding voices;
    // "FIXED" scales every voice by headroom, so tracks keep their level.

    vm.register_native("SOUND.MASTER", 1, 2, [](const std::vector<Value>& args) -> Value {
        std::string m = args[0].to_string();
        std::transform(m.begin(), m.end(), m.begin(), ::toupper);
        if (m != "AUTO" && m != "FIXED")
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.MASTER: mode is AUTO or FIXED, not " + m);
        float h = args.size() >= 2 ? (float)args[1].to_double() : 0.5f;
        if (h <= 0.0f || h > 4.0f)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.MASTER: headroom is above 0 and at most 4");
        ensure_sound("SOUND"); SND_LOCK();
        g_sound.master_fixed = (m == "FIXED");
        g_sound.master_headroom = h;
        SND_UNLOCK();
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

        ensure_sound("SOUND"); SND_LOCK();
        int vi = g_sound.allocate_voice(t);
        if (vi >= 0) {
            auto& v = g_sound.voices[vi];
            auto& tp = g_sound.tracks[t];
            v.frequency = freq;
            SoundSystem::start_envelopes(v, tp);
            if (tp.waveform == Waveform::SAMPLE) {
                v.sample_id = tp.sample_id;
                v.sample_loop = tp.sample_loop;
                v.sample_pos = 0;
            }
        }
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── SOUND.RELEASE track ─────────────────────────────────────

    vm.register_native("SOUND.RELEASE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        ensure_sound("SOUND"); SND_LOCK();
        for (auto& v : g_sound.voices)
            if (v.track == t && v.env.state != EnvState::OFF) v.env.gate_off();
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── SOUND.STOP track ────────────────────────────────────────

    vm.register_native("SOUND.STOP", 1, 1, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        ensure_sound("SOUND"); SND_LOCK();
        for (auto& v : g_sound.voices)
            if (v.track == t) v.env.state = EnvState::OFF;
        SND_UNLOCK();
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
#ifndef GFX
        (void)args;
        throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.PLAYBUFFER requires a GFX build");
#else
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
#endif
    });

    // ── SOUND.QUEUED() - bytes still pending in PCM stream ──────
    //
    // Useful for keeping the buffer between min/max water-marks without
    // overflowing or underrunning. Returns 0 if PCM stream is closed.
    vm.register_native("SOUND.QUEUED", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
#ifdef GFX
        if (!g_sound.pcm_stream) return Value::make_i64(0);
        return Value::make_i64((int64_t)SDL_GetAudioStreamQueued(g_sound.pcm_stream));
#else
        return Value::make_i64(0);
#endif
    });

    // ── SFX.LOAD id, filepath ───────────────────────────────────

    vm.register_native("SFX.LOAD", 2, 2, [](const std::vector<Value>& args) -> Value {
        g_sound.samples[(int)args[0].to_int()] = load_wav_file(args[1].as_string()->data, "SFX.LOAD");
        return Value::make_none();
    });

    // ── SFX.PLAY id ─────────────────────────────────────────────

    vm.register_native("SFX.PLAY", 1, 1, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        if (g_sound.samples.find(id) == g_sound.samples.end()) return Value::make_none();
        ensure_sound("SOUND"); SND_LOCK();
        for (auto& ch : g_sound.sfx_channels) {
            if (!ch.active) {
                ch.sample_id = id; ch.position = 0; ch.active = true; ch.looping = false;
                break;
            }
        }
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── MUSIC.PLAY id, [loop] ───────────────────────────────────

    vm.register_native("MUSIC.PLAY", 1, 2, [](const std::vector<Value>& args) -> Value {
        int id = (int)args[0].to_int();
        bool loop = (args.size() >= 2) ? args[1].to_bool() : true;
        if (g_sound.samples.find(id) == g_sound.samples.end()) return Value::make_none();
        ensure_sound("SOUND"); SND_LOCK();
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
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── MUSIC.STOP ──────────────────────────────────────────────

    vm.register_native("MUSIC.STOP", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        ensure_sound("SOUND"); SND_LOCK();
        if (g_sound.music_channel >= 0)
            g_sound.sfx_channels[g_sound.music_channel].active = false;
        g_sound.music_channel = -1;
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── SOUND.SEQ layer, pattern$, waveform$ ────────────────────

    vm.register_native("SOUND.SEQ", 3, 3, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        std::string pattern = args[1].as_string()->data;
        std::string wf = args[2].as_string()->data;
        std::transform(wf.begin(), wf.end(), wf.begin(), ::toupper);

        jdpat::NodeP pat;
        try { pat = jdpat::parse(pattern); }
        catch (const std::exception& e) {
            throw jdError(ErrCode::RUNTIME_ERROR, std::string("SOUND.SEQ: ") + e.what());
        }

        ensure_sound("SOUND"); SND_LOCK();
        bool idle = true;
        for (auto& l : g_sound.seq_layers)
            if (l.pat || l.has_pending || (l.is_linear && !l.events.empty())) idle = false;
        auto& layer = g_sound.seq_layers[t];
        if (layer.is_linear) {
            layer.events.clear();
            layer.is_linear = false;
            layer.looping = false;
        }
        layer.use_voice = (wf == "VOICE");
        if (!layer.use_voice) {
            g_sound.tracks[t].waveform = parse_waveform(wf);
        }
        // A playing layer switches at the next cycle; an empty one starts now,
        // and the first pattern of an idle sequencer starts a fresh cycle.
        if (layer.pat || layer.has_pending) {
            layer.pending = pat;
            layer.has_pending = true;
        } else {
            if (idle && !g_sound.in_sched) g_sound.reset_clock();
            layer.pat = pat;
            layer.events.clear();
            layer.built_cycle = INT64_MIN;
        }
        SND_UNLOCK();
        return Value::make_none();
    });

    // ── SOUND.EVENTS$(pattern$, [cycles], [first]) ──────────────
    // The events a pattern plays, one line each: "start duration note".

    vm.register_native("SOUND.EVENTS$", 1, 3, [](const std::vector<Value>& args) -> Value {
        std::string pattern = args[0].to_string();
        int64_t cycles = (args.size() >= 2) ? args[1].to_int() : 1;
        int64_t first = (args.size() >= 3) ? args[2].to_int() : 0;
        jdpat::NodeP pat;
        try { pat = jdpat::parse(pattern); }
        catch (const std::exception& e) {
            throw jdError(ErrCode::RUNTIME_ERROR, std::string("SOUND.EVENTS$: ") + e.what());
        }
        std::string out;
        char buf[64];
        for (int64_t c = first; c < first + cycles; c++) {
            for (auto& e : jdpat::query_cycle(pat, c)) {
                if (!out.empty()) out += '\n';
                std::snprintf(buf, sizeof buf, "%.6g %.6g ", (double)c + e.start, e.dur);
                out += buf;
                out += e.note;
                if (e.prob < 1.0f) {
                    std::snprintf(buf, sizeof buf, "?%.6g", e.prob);
                    out += buf;
                }
                if (e.pan >= 0.0f) {
                    std::snprintf(buf, sizeof buf, " pan=%.6g", e.pan);
                    out += buf;
                }
                if (e.gain != 1.0f) {
                    std::snprintf(buf, sizeof buf, " gain=%.6g", e.gain);
                    out += buf;
                }
                if (e.legato >= 0.0f) {
                    std::snprintf(buf, sizeof buf, " legato=%.6g", e.legato);
                    out += buf;
                }
            }
        }
        return Value::make_string(out);
    });

    // ── SOUND.NOTE pattern$, [loop] ──────────────────────────────
    // Plays a note pattern (Strudel-style). Supports parallel: < melody , bass >
    vm.register_native("SOUND.NOTE", 1, 3, [](const std::vector<Value>& args) -> Value {
        ensure_sound("SOUND");
        std::string pattern = args[0].as_string()->data;
        bool loop = (args.size() >= 2) ? args[1].to_bool() : false;
        bool debug = (args.size() >= 3) ? args[2].to_bool() : false;
        SND_LOCK();
        g_sound.parse_note_pattern(pattern, 0, loop);
        SND_UNLOCK();
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
    // SOUND.FILTER track, cutoff, [resonance], [mode$]
    // Two arguments keep the one-pole low-pass; a resonance or a mode
    // ("LP", "HP", "BP") selects the state-variable filter.
    vm.register_native("SOUND.FILTER", 2, 4, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        auto& tp = g_sound.tracks[t];
        tp.filter_cutoff = (float)args[1].to_double();
        if (args.size() >= 3) {
            tp.filter_res = std::clamp((float)args[2].to_double(), 0.0f, 1.0f);
            tp.filter_mode = 1;
        }
        if (args.size() >= 4) {
            std::string m = args[3].to_string();
            std::transform(m.begin(), m.end(), m.begin(), ::toupper);
            if (m == "LP") tp.filter_mode = 1;
            else if (m == "HP") tp.filter_mode = 2;
            else if (m == "BP") tp.filter_mode = 3;
            else if (m == "LP1") tp.filter_mode = 0;
            else throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.FILTER: mode is LP, HP, BP or LP1, not " + m);
        }
        return Value::make_none();
    });

    // SOUND.FENV track, octaves, attack, decay, [sustain], [release]
    vm.register_native("SOUND.FENV", 4, 6, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        auto& tp = g_sound.tracks[t];
        tp.fenv_amount = (float)args[1].to_double();
        tp.fenv_attack = (float)args[2].to_double();
        tp.fenv_decay = (float)args[3].to_double();
        tp.fenv_sustain = (args.size() >= 5) ? (float)args[4].to_double() : 0.0f;
        tp.fenv_release = (args.size() >= 6) ? (float)args[5].to_double() : 0.1f;
        return Value::make_none();
    });

    // SOUND.MOD track, param$, shape$, [lo], [hi], [cycles], [offset]
    // shape$ "OFF" stops the modulation and restores the value it started from.
    vm.register_native("SOUND.MOD", 3, 7, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int(); if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        std::string pn = args[1].to_string(), sn = args[2].to_string();
        std::transform(pn.begin(), pn.end(), pn.begin(), ::toupper);
        std::transform(sn.begin(), sn.end(), sn.begin(), ::toupper);
        static const std::pair<const char*, int> params[] = {
            {"CUTOFF", MP_CUTOFF}, {"RESONANCE", MP_RESONANCE}, {"GAIN", MP_GAIN},
            {"PAN", MP_PAN}, {"DETUNE", MP_DETUNE}, {"FM", MP_FM}, {"CRUSH", MP_CRUSH},
            {"REVERB", MP_REVERB}, {"DELAY", MP_DELAY}, {"FENV", MP_FENV}};
        static const std::pair<const char*, ModShape> shapes[] = {
            {"OFF", ModShape::NONE}, {"SINE", ModShape::SINE}, {"COSINE", ModShape::COSINE},
            {"SAW", ModShape::SAW}, {"ISAW", ModShape::ISAW}, {"TRI", ModShape::TRI},
            {"SQUARE", ModShape::SQUARE}, {"RAND", ModShape::RAND}, {"PERLIN", ModShape::PERLIN}};
        int param = -1;
        for (auto& p : params) if (pn == p.first) param = p.second;
        if (param < 0)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.MOD: unknown parameter " + pn +
                " (CUTOFF, RESONANCE, GAIN, PAN, DETUNE, FM, CRUSH, REVERB, DELAY, FENV)");
        int shape = -1;
        for (auto& s : shapes) if (sn == s.first) shape = (int)s.second;
        if (shape < 0)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.MOD: unknown shape " + sn +
                " (SINE, COSINE, SAW, ISAW, TRI, SQUARE, RAND, PERLIN, OFF)");
        float cycles = (args.size() >= 6) ? (float)args[5].to_double() : 1.0f;
        if (cycles <= 0.0f)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.MOD: cycles must be positive");

        SND_LOCK();
        auto& tp = g_sound.tracks[t];
        auto& ms = tp.mods[param];
        float* dst = mod_target(tp, param);
        if ((ModShape)shape == ModShape::NONE) {
            if (ms.shape != ModShape::NONE && dst) *dst = ms.base;
            ms.shape = ModShape::NONE;
        } else {
            if (ms.shape == ModShape::NONE && dst) ms.base = *dst;
            ms.shape = (ModShape)shape;
            ms.lo = (args.size() >= 4) ? (float)args[3].to_double() : 0.0f;
            ms.hi = (args.size() >= 5) ? (float)args[4].to_double() : 1.0f;
            ms.cycles = cycles;
            ms.offset = (args.size() >= 7) ? (float)args[6].to_double() : 0.0f;
            if (param == MP_RESONANCE && tp.filter_mode == 0) tp.filter_mode = 1;
        }
        SND_UNLOCK();
        return Value::make_none();
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

    // Orbit of the optional last argument of SOUND.DELAY / SOUND.REVERB,
    // with its bus ready to use
    static auto bus_arg = [](const std::vector<Value>& args, size_t i, const char* fn) -> FxBus& {
        int o = args.size() > i ? (int)args[i].to_int() : 0;
        if (o < 0 || o >= NUM_ORBITS)
            throw jdError(ErrCode::RUNTIME_ERROR, std::string(fn) + ": orbit is 0 to " + std::to_string(NUM_ORBITS - 1));
        g_sound.buses[o].prepare(0.0f);
        return g_sound.buses[o];
    };
    vm.register_native("SOUND.DELAY", 4, 5, [](const std::vector<Value>& args) -> Value {
        SND_LOCK();
        FxBus* b = nullptr;
        try { b = &bus_arg(args, 4, "SOUND.DELAY"); } catch (...) { SND_UNLOCK(); throw; }
        b->delay_active = args[0].to_bool();
        b->delay_time = (float)args[1].to_double() / 1000.0f;  // ms to sec
        b->delay_feedback = (float)args[2].to_double();
        b->delay_mix = (float)args[3].to_double();
        SND_UNLOCK();
        return Value::make_none();
    });
    vm.register_native("SOUND.REVERB", 4, 5, [](const std::vector<Value>& args) -> Value {
        SND_LOCK();
        FxBus* b = nullptr;
        try { b = &bus_arg(args, 4, "SOUND.REVERB"); } catch (...) { SND_UNLOCK(); throw; }
        b->reverb.room_size = std::min((float)args[0].to_double(), 0.98f);
        b->reverb.damping = (float)args[1].to_double();
        b->reverb.width = (float)args[2].to_double();
        b->reverb.wet = (float)args[3].to_double();
        SND_UNLOCK();
        return Value::make_none();
    });
    // ── SOUND.CYCLE() -> the sequencer clock, in cycles ─────────

    vm.register_native("SOUND.CYCLE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!g_sound.initialized) return Value::make_f64(0.0);
        SND_LOCK();
        double c = g_sound.seq_time;
        SND_UNLOCK();
        return Value::make_f64(c);
    });

    // ── SOUND.NUDGE track, ms ───────────────────────────────────
    // Starts every note of the track ms milliseconds late (0 to 1000).

    vm.register_native("SOUND.NUDGE", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        double ms = args[1].to_double();
        if (ms < 0 || ms > 1000)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.NUDGE: ms is 0 to 1000");
        SND_LOCK();
        g_sound.tracks[t].nudge = (float)(ms / 1000.0);
        SND_UNLOCK();
        return Value::make_none();
    });

    vm.register_native("SOUND.ORBIT", 2, 2, [](const std::vector<Value>& args) -> Value {
        int t = (int)args[0].to_int();
        if (t < 0 || t >= NUM_TRACKS) return Value::make_none();
        int o = (int)args[1].to_int();
        if (o < 0 || o >= NUM_ORBITS)
            throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.ORBIT: orbit is 0 to " + std::to_string(NUM_ORBITS - 1));
        SND_LOCK();
        g_sound.buses[o].prepare(0.0f);
        g_sound.tracks[t].orbit = o;
        SND_UNLOCK();
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

    // ── SOUND.RENDER(frames) - pull interleaved stereo samples ──
    //
    // Renders the next `frames` stereo frames of the live sequencer and
    // returns them as a flat array [L0, R0, L1, R1, ...]. For embed hosts
    // that own the audio device (e.g. feeding a Godot AudioStreamGenerator).

    vm.register_native("SOUND.RENDER", 1, 1, [](const std::vector<Value>& args) -> Value {
        int frames = (int)args[0].to_int();
        Value r = Value::make_array();
        if (frames <= 0 || !g_sound.initialized) return r;
        std::vector<float> buf((size_t)frames * 2, 0.0f);
        SoundSystem::render_frames(&g_sound, buf.data(), frames);
        r.as_array()->elements.reserve(buf.size());
        for (float f : buf)
            r.as_array()->elements.push_back(Value::make_f64(f));
        return r;
    });

    // ── SOUND.GET_WAVE / SOUND.GET_BUS_WAVE ─────────────────────

    // The last 1024 output samples of a ring, oldest first.
    static auto ring_array = [](const std::vector<float>& ring) -> Value {
        Value r = Value::make_array();
        if (!g_sound.initialized || ring.empty()) return r;
        SND_LOCK();
        size_t n = ring.size(), w = g_sound.wave_pos % n;
        auto& el = r.as_array()->elements;
        el.reserve(n);
        for (size_t i = 0; i < n; i++) el.push_back(Value::make_f64(ring[(w + i) % n]));
        SND_UNLOCK();
        return r;
    };
    vm.register_native("SOUND.GET_WAVE", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        return ring_array(g_sound.wave_buffer);
    });
    vm.register_native("SOUND.GET_BUS_WAVE", 1, 1, [](const std::vector<Value>& args) -> Value {
        int bus = (int)args[0].to_int();
        return ring_array(bus == 0 ? g_sound.reverb_bus_wave : g_sound.delay_bus_wave);
    });

    // ── SOUND.DEBUG ──────────────────────────────────────────────

    vm.register_native("SOUND.DEBUG", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        Value r = Value::make_object();
        r.as_object()->set("initialized", Value::make_bool(g_sound.initialized));
#ifdef GFX
        r.as_object()->set("stream", Value::make_bool(g_sound.stream != nullptr));
#endif
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
        if (!g_sound.initialized) return Value::make_none();
        ensure_sound("SOUND"); SND_LOCK();
        for (auto& v : g_sound.voices) v.env.state = EnvState::OFF;
        for (auto& ch : g_sound.sfx_channels) ch.active = false;
        for (auto& l : g_sound.seq_layers) {
            l.events.clear(); l.active_voice_idx = -1;
            l.is_linear = false; l.looping = false;
            l.linear_cursor = 0; l.total_length = 1.0f;
            l.pat.reset(); l.pending.reset(); l.has_pending = false;
            l.built_cycle = INT64_MIN;
        }
        g_sound.reset_clock();
        g_sound.sched.clear();
        g_sound.sched_at = -1.0;
        g_sound.distortion_amount = 0;
        g_sound.compressor_active = false;
        g_sound.master_fixed = false;
        g_sound.master_headroom = 0.5f;
        for (auto& b : g_sound.buses) {
            b.delay_active = false;
            b.reverb.wet = 0;
            std::fill(b.delay_buffer.begin(), b.delay_buffer.end(), 0.0f);
        }
        for (int t = 0; t < NUM_TRACKS; t++) g_sound.tracks[t] = TrackParams{};
        SND_UNLOCK();
        return Value::make_none();
    });

    vm.register_native("SOUND.SHUTDOWN", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        sound_shutdown();
        return Value::make_none();
    });

    // ── SOUND.AT [cycle] / SOUND.PENDING() ──────────────────────
    // After SOUND.AT c, the track and mix commands below are queued and
    // the mixer applies them on the frame the clock reaches cycle c.
    // SOUND.AT with no argument or a negative one applies them at once.

    vm.register_native("SOUND.AT", 0, 1, [](const std::vector<Value>& args) -> Value {
        g_sound.sched_at = args.empty() ? -1.0 : args[0].to_double();
        return Value::make_none();
    });

    vm.register_native("SOUND.PENDING", 0, 0, [](const std::vector<Value>& args) -> Value {
        (void)args;
        if (!g_sound.initialized) return Value::make_i64(0);
        SND_LOCK();
        int64_t n = (int64_t)g_sound.sched.size();
        SND_UNLOCK();
        return Value::make_i64(n);
    });

    static const char* const schedulable[] = {
        "SOUND.SEQ", "SOUND.NOTE", "SOUND.VOICE", "SOUND.BPM", "SOUND.GAIN",
        "SOUND.PAN", "SOUND.FILTER", "SOUND.FENV", "SOUND.MOD", "SOUND.EQ",
        "SOUND.LFO", "SOUND.FM", "SOUND.UNISON", "SOUND.BITCRUSH",
        "SOUND.RINGMOD", "SOUND.REVERBSEND", "SOUND.DELAYSEND",
        "SOUND.SIDECHAIN", "SOUND.SCALE", "SOUND.DELAY", "SOUND.REVERB",
        "SOUND.NUDGE", "SOUND.ORBIT", "SOUND.COMPRESSOR", "SOUND.DISTORTION",
        "SOUND.MASTER"};
    for (const char* name : schedulable) {
        const VM::NativeEntry* e = vm.native_find(name);
        if (!e) continue;
        VM::NativeFunc raw = e->fn;
        int lo = e->min_args, hi = e->max_args;
        std::string nm = name;
        vm.register_native(name, lo, hi, [raw, nm](const std::vector<Value>& args) -> Value {
            if (g_sound.sched_at < 0 || !g_sound.initialized) return raw(args);
            if (nm == "SOUND.SEQ") {
                try { jdpat::parse(args[1].to_string()); }
                catch (const std::exception& ex) {
                    throw jdError(ErrCode::RUNTIME_ERROR, std::string("SOUND.SEQ: ") + ex.what());
                }
            }
            std::vector<Value> copy;
            for (auto& a : args) {
                if (a.type == ValueType::ARRAY || a.type == ValueType::OBJECT || a.type == ValueType::TENSOR)
                    throw jdError(ErrCode::RUNTIME_ERROR, "SOUND.AT: " + nm + " takes no array or map while scheduling");
                copy.push_back(a.type == ValueType::STRING ? Value::make_string(a.to_string()) : a);
            }
            SND_LOCK();
            bool later = g_sound.sched_at > g_sound.seq_time + 1e-9;
            if (later)
                g_sound.sched.push_back({g_sound.sched_at, g_sound.sched_order++, nm, raw, std::move(copy)});
            SND_UNLOCK();
            return later ? Value::make_none() : raw(args);
        });
    }
}

#endif // GFX
