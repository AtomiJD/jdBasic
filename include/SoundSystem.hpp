#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <map>
#ifdef SDLMIXER // --- LIVE CODING SEQUENCER EXTENSIONS ---
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm> // for std::find
#endif

// Enum for different oscillator waveforms
enum class Waveform {
    SINE,
    SQUARE,
    SAWTOOTH,
    TRIANGLE,
    NOISE
};

// Enum for the states of the ADSR envelope
enum class ADSRState {
    OFF,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE
};

// Struct to hold the state of a single synthesizer voice/track
struct Voice {
    ADSRState adsr_state = ADSRState::OFF;
    Waveform waveform = Waveform::SINE;
    double frequency = 440.0;       // Pitch in Hz
    double phase = 0.0;             // Current position in the waveform (0 to 2*PI)
    double envelope_level = 0.0;    // Current volume multiplier (0.0 to 1.0)

    // ADSR parameters (times in seconds, level is a multiplier)
    double attack_time = 0.01;
    double decay_time = 0.1;
    double sustain_level = 0.8;
    double release_time = 0.2;

    // 1. Low Pass Filter (Simple 1-pole for efficiency, or Biquad for quality)
    // We will use a state variable for a simple filter
    double filter_cutoff = 20000.0; // Hz, default open
    double filter_resonance = 0.0;  // 0.0 to 1.0
    double filter_state = 0.0;      // Current value of the filter (previous output)

    // 2. LFO for Vibrato
    double lfo_frequency = 5.0;     // Hz (speed of vibrato)
    double lfo_depth = 0.0;         // Amount of pitch shift
    double lfo_phase = 0.0;         // Current phase of LFO

    // --- FM SYNTHESIS ---
    double fm_amount = 0.0;  // How much to modulate (0.0 to 10.0+)
    double fm_ratio = 1.0;   // Ratio of modulator freq to carrier freq (e.g. 2.0 = octave)
    double mod_phase = 0.0;  // Phase of the modulator oscillator

    // --- BITCRUSHER ---
    double crush_bits = 0.0;       // 0.0 = Off, 1.0 to 16.0 = Active
    double crush_rate = 1.0;       // 1.0 = Normal, 0.5 = Half speed, 0.1 = 1/10th speed
    double crush_counter = 0.0;    // Internal counter for sample holding
    float  crush_last_sample = 0.0f; // Last held sample value

    // --- RING MODULATOR ---
    double ring_freq = 0.0;        // 0.0 = Off, >0 = Frequency Hz
    double ring_mix = 0.0;         // 0.0 to 1.0 (Dry/Wet)
    double ring_phase = 0.0;       // Internal phase

    double gain = 1.0;

    // --- STEREO PANNING ---
    double pan = 0.5; // 0.0 = Left, 0.5 = Center, 1.0 = Right

    // --- SEQUENCER GATE TIMER ---
    double seq_duration_remaining = 0.0; // Counts down from duration to 0
    bool controlled_by_sequencer = false; // Flag to know if seq is driving this
};

// --- Struct to hold loaded WAV file data ---
// The audio data is converted to the device's native format (float) upon loading.
struct SoundChunk {
    float* buffer = nullptr;
    Uint32 length = 0; // Length in bytes
};

// --- Struct to manage a single playback instance of a SoundChunk ---
struct SoundChannel {
    int sample_id = -1;       // ID of the SoundChunk to play
    Uint32 position = 0;      // Current position in the buffer, in bytes
    bool is_active = false;
    bool is_looping = false;
    double pan = 0.5;
};

#ifdef SDLMIXER

const std::map<std::string, std::vector<int>> scale_library = {
    {"CHROMATIC", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"MAJOR",     {0, 2, 4, 5, 7, 9, 11}},
    {"MINOR",     {0, 2, 3, 5, 7, 8, 10}}, // Natural Minor
    {"DORIAN",    {0, 2, 3, 5, 7, 9, 10}},
    {"PHRYGIAN",  {0, 1, 3, 5, 7, 8, 10}},
    {"LYDIAN",    {0, 2, 4, 6, 7, 9, 11}},
    {"MIXOLYDIAN",{0, 2, 4, 5, 7, 9, 10}},
    {"LOCRIAN",   {0, 1, 3, 5, 6, 8, 10}},
    {"PENT_MAJ",  {0, 2, 4, 7, 9}},
    {"PENT_MIN",  {0, 3, 5, 7, 10}},
    {"BLUES",     {0, 3, 5, 6, 7, 10}},
    {"ARABIC",    {0, 1, 4, 5, 7, 8, 11}} // Hijaz scale
};

// 1. Note to Frequency Conversion
struct NoteMap {
    static float get(const std::string& note) {
        static std::map<std::string, float> freqs;
        if (freqs.empty()) {
            std::vector<std::string> names = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
            float base_c0 = 16.35f;
            for (int oct = 0; oct < 9; ++oct) {
                for (int i = 0; i < 12; ++i) {
                    float f = base_c0 * std::pow(2.0f, oct + i / 12.0f);
                    std::string key = names[i] + std::to_string(oct);
                    freqs[key] = f;
                    // aliases (db, eb, etc could be added here)
                }
            }
        }
        // Normalize input (tolower)
        std::string n = note;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (freqs.count(n)) return freqs[n];
        return 0.0f; // Not a note (maybe a sample name)
    }
};

// 2. Event Structure
struct SeqEvent {
    double start_phase; // 0.0 to 1.0 within a cycle
    double duration;    // portion of the cycle
    std::string token;  // "c3", "bd", etc.
    bool triggered = false;
};

// 3. The Sequencer Engine
class MusicSequencer {
public:
    double cycles_per_second = 0.5; // Default 1 cycle every 2 seconds (approx 120 BPM 4/4)
    double current_phase = 0.0;     // 0.0 to 1.0

    // Multiple layers (e.g., one for melody, one for drums)
    struct Layer {
        std::string pattern_source;
        std::vector<SeqEvent> events;
        Waveform synth_type = Waveform::SINE;
        std::string sample_name; // If set, overrides synth
        bool active = true;
    };

    std::vector<Layer> layers;

    void set_cps(double cps) {
        if (cps <= 0) cps = 0.1;
        cycles_per_second = cps;
    }

    // Strudel-like parser: "c3 [e3 g3] ~ a3"
    void parse_pattern(int layer_idx, const std::string& pattern, Waveform wave) {
        if (layer_idx >= layers.size()) layers.resize(layer_idx + 1);

        // If pattern matches previous, don't re-parse (optimization)
        if (layers[layer_idx].pattern_source == pattern && layers[layer_idx].synth_type == wave) return;

        layers[layer_idx].pattern_source = pattern;
        layers[layer_idx].synth_type = wave;
        layers[layer_idx].events.clear();

        layers[layer_idx].active = true;

        // Recursively parse the string
        parse_recursive(layers[layer_idx].events, pattern, 0.0, 1.0);
    }

private:
    // Helper to find matching closing bracket
    size_t find_closing(const std::string& s, size_t start) {
        int depth = 0;
        for (size_t i = start; i < s.length(); ++i) {
            if (s[i] == '[') depth++;
            if (s[i] == ']') {
                depth--;
                if (depth == 0) return i;
            }
        }
        return std::string::npos;
    }

    void parse_recursive(std::vector<SeqEvent>& out, std::string pat, double start, double dur) {
        // Simple tokenizer respecting brackets
        std::vector<std::string> steps;
        std::string current;
        int depth = 0;

        for (size_t i = 0; i < pat.length(); ++i) {
            char c = pat[i];
            if (c == '[') depth++;
            if (c == ']') depth--;

            if (c == ' ' && depth == 0) {
                if (!current.empty()) steps.push_back(current);
                current.clear();
            }
            else {
                current += c;
            }
        }
        if (!current.empty()) steps.push_back(current);

        if (steps.empty()) return;

        double step_dur = dur / steps.size();
        for (size_t i = 0; i < steps.size(); ++i) {
            double step_start = start + (i * step_dur);
            std::string token = steps[i];

            // Cleanup token
            size_t first = token.find_first_not_of(" ");
            size_t last = token.find_last_not_of(" ");
            if (first == std::string::npos) continue;
            token = token.substr(first, (last - first + 1));

            if (token[0] == '[') {
                // Subdivision found, recurse!
                // Strip outer brackets
                if (token.back() == ']') token = token.substr(1, token.length() - 2);
                parse_recursive(out, token, step_start, step_dur);
            }
            else if (token == "~" || token == ".") {
                // Rest, do nothing
            }
            else {
                // Leaf node (Note)
                out.push_back({ step_start, step_dur, token, false });
            }
        }
    }
};
#endif


class SoundSystem {
public:
    SoundSystem();
    ~SoundSystem();

    // Initializes SDL_Audio and opens an audio device
    bool init(int num_tracks = 8, int num_channels = 16);

    void shutdown();

    void set_voice(int track_index, Waveform waveform, double attack, double decay, double sustain, double release);
    void play_note(int track_index, double frequency);
    void release_note(int track_index);
    void stop_note(int track_index);
    void set_gain(int track_index, double gain);
    void set_pan(int track_index, double pan);
    void set_delay(bool active, double time_ms, double feedback, double mix);
    void set_fm(int track, double amount, double ratio); 
    void set_distortion(double amount);
    void set_bitcrusher(int track, double bits, double rate);
    void set_ringmod(int track, double freq, double mix);
    void reset();

#ifdef JD_IMGUI
    std::vector<float> get_wave_data(); // Returns data for the GUI
#endif
    // Loads a WAV file and stores it with a given ID.
    // Returns true on success, false on failure.
    bool load_sound(int sample_id, const std::string& filename);
    void play_sound(int sample_id, bool looping = false); // Add looping parameter
    void play_music(int sample_id, bool looping = true);
    void stop_music();
#ifdef SDLMIXER
    MusicSequencer sequencer;
    void update_sequence(int layer, const std::string& pattern, const std::string& waveform_str);
    void set_bpm(double bpm);
    void set_scale(const std::string& root_note, const std::string& mode);
    float get_scale_freq(int degree);
#endif
    bool is_initialized = false;

    // --- State for WAV file playback ---
    std::map<int, SoundChunk> loaded_samples; // Stores loaded WAV data, mapped by ID.
    std::vector<SoundChannel> channels;       // A pool of channels for playing sounds.
    std::vector<Voice> tracks; // A vector to hold all our synthesizer tracks
    SDL_AudioStream* audio_stream = nullptr;

private:
    // Delay Buffer
    std::vector<float> delay_buffer;
    size_t delay_head = 0;

    // Delay Settings
    bool delay_active = false;
    double delay_time_ms = 300.0; // Time between echoes
    double delay_feedback = 0.4;  // How much sound repeats (0.0 to 0.99)
    double delay_mix = 0.3;       // Volume of echo vs dry signal

    double distortion_amount = 0.0; // 0.0 = Clean, >0.0 = Dirty

#ifdef SDLMIXER
    int scale_root_midi = 48; // Default C3
    std::vector<int> scale_intervals = { 0, 2, 4, 5, 7, 9, 11 }; // Default Major Scale
#endif

    static void audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len);

    // Helper to generate a sample for a given voice
    float generate_sample(Voice& voice);

    SDL_AudioDeviceID audio_device_id = 0;
    SDL_AudioSpec audio_spec;
    int music_channel_id = -1;
#ifdef JD_IMGUI
    std::vector<float> vis_buffer;
    size_t vis_head = 0;
#endif

};
#endif
