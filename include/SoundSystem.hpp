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
    NOISE,
    SAMPLE
};

// Enum for the states of the ADSR envelope
enum class ADSRState {
    OFF,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE
};

// 1. VoiceParams: The "Settings" for a Track (What the user configures)
struct VoiceParams {
    Waveform waveform = Waveform::SINE;

    // ADSR Target Values
    double attack_time = 0.01;
    double decay_time = 0.1;
    double sustain_level = 0.8;
    double release_time = 0.2;

    // Timbre Settings
    double filter_cutoff = 20000.0;
    double filter_resonance = 0.0;
    double lfo_frequency = 5.0;
    double lfo_depth = 0.0;
    double fm_amount = 0.0;
    double fm_ratio = 1.0;
    double crush_bits = 0.0;
    double crush_rate = 1.0;
    double ring_freq = 0.0;
    double ring_mix = 0.0;
    double gain = 1.0;
    double pan = 0.5;

    double reverb_send = 0.0; // 0.0 (Dry) to 1.0 (Wet)
    double delay_send = 0.0;  // 0.0 (Dry) to 1.0 (Wet)

    int sidechain_source = -1; // -1 = No sidechain, 0-7 = Track ID to listen to
    float sidechain_amount = 0.0f; // 0.0 = None, 1.0 = Full ducking

    // Sample Parameters
    int sample_id = -1;       
    float sample_base_freq = 261.63f; 
    bool sample_loop = false; 

    // 3-Band EQ Gains (0.0 to 2.0+, Default 1.0)
    double eq_low = 1.0;
    double eq_mid = 1.0;
    double eq_high = 1.0;

    // Unison Settings
    int unison_voices = 1;       // 1 to 16
    double unison_detune = 0.0;  // 0.0 to 1.0 (How much drift?)
    double unison_spread = 0.0;  // 0.0 to 1.0 (Stereo width)

    // Per-Track Scale Engine 
    int scale_root_midi = 48; // Default C3
    // Default to Chromatic or Major. Let's start with Chromatic (all notes) or a safe Major.
    std::vector<int> scale_intervals = { 0, 2, 4, 5, 7, 9, 11 };
};

// 2. ActiveVoice: The "Physical" Sound Generator (The runtime state)
struct ActiveVoice {
    bool active = false;
    int source_track_id = -1; // Which Track (Params) does this belong to?

    ADSRState adsr_state = ADSRState::OFF;
    double frequency = 0.0;
    double phase = 0.0;
    double envelope_level = 0.0;

    // State for effects
    double filter_state_L = 0.0;
    double filter_state_R = 0.0;
    double lfo_phase = 0.0;
    double mod_phase = 0.0;
    double ring_phase = 0.0;
    double crush_counter = 0.0;
    float  crush_last_sample = 0.0f;
    double unison_phases[16] = { 0.0 };

    // Sample Accuracy
    int start_delay_frames = 0;

    // Sequencer State
    bool controlled_by_sequencer = false;
    double seq_duration_remaining = 0.0;

    // Sample State
    double unison_cursors[16] = { 0.0 }; // Precise fractional position in the sample

    // EQ Filter State
    double eq_low_state_L = 0.0;
    double eq_low_state_R = 0.0;
    double eq_high_state_L = 0.0;
    double eq_high_state_R = 0.0;
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

// Helper: Circular Buffer for Delays
struct DelayLine {
    std::vector<float> buffer;
    size_t cursor = 0;

    void resize(int size) {
        buffer.resize(size, 0.0f);
        cursor = 0;
    }

    float read() {
        if (buffer.empty()) return 0.0f;
        return buffer[cursor];
    }

    void write(float val) {
        if (buffer.empty()) return;
        buffer[cursor] = val;
        cursor++;
        if (cursor >= buffer.size()) cursor = 0;
    }

    // For AllPass: we sometimes need to write at the same cursor we read from
    // so we handle the increment separately or use a helper.
};

struct CombFilter {
    DelayLine delay;
    float feedback = 0.5f;
    float damping = 0.5f; // Simple Lowpass
    float filter_state = 0.0f;

    void init(int size) { delay.resize(size); }

    float process(float input) {
        float output = delay.read();

        // Lowpass filter in the feedback loop (Simulates air absorption)
        filter_state = (output * (1.0f - damping)) + (filter_state * damping);

        delay.write(input + (filter_state * feedback));
        return output;
    }
};

struct AllPassFilter {
    DelayLine delay;
    float feedback = 0.7f;

    void init(int size) { delay.resize(size); }

    float process(float input) {
        float delayed = delay.read();
        float output = -input + delayed;

        // Standard Allpass difference equation
        // y[n] = -x[n] + x[n-D] + g * y[n-D]
        // But often implemented as:
        // buf_out = delay.read();
        // feed = input + buf_out * feedback;
        // delay.write(feed);
        // output = buf_out - feed * feedback; (Schroeder/Moorer style)

        // Let's use the Schroeder style:
        float buf_out = delay.read();
        float feed = input + (buf_out * feedback);
        delay.write(feed);
        return buf_out - (feed * feedback);
    }
};

struct ReverbModule {
    // 4 Parallel Comb Filters
    CombFilter combs[4];
    // 2 Series AllPass Filters
    AllPassFilter allpasses[2];

    // Parameters
    float room_size = 0.84f; // Controls Feedback
    float damping = 0.2f;    // Controls HF loss
    float wet_mix = 0.0f;    // 0.0 to 1.0
    float dry_mix = 1.0f;
    float width = 1.0f;      // Stereo spread

    void init(int seed_offset) {
        // Schroeder tuning (Prime numbers for delay lengths to avoid resonance)
        // We add seed_offset to make Left and Right channels different
        combs[0].init(1687 + seed_offset);
        combs[1].init(1601 + seed_offset);
        combs[2].init(2053 + seed_offset);
        combs[3].init(2251 + seed_offset);

        allpasses[0].init(225 + seed_offset);
        allpasses[1].init(341 + seed_offset);
    }

    void set_params(float room, float damp, float wid, float wet) {
        room_size = room; // 0.7 to 0.98
        damping = damp;   // 0.0 to 1.0
        width = wid;
        wet_mix = wet;
        dry_mix = 1.0f - (wet * 0.5f); // Simple dry compensation

        for (int i = 0; i < 4; ++i) {
            combs[i].feedback = room_size;
            combs[i].damping = damping;
        }
    }

    float process(float input) {
        float out = 0.0f;
        // Parallel Combs
        for (int i = 0; i < 4; ++i) {
            out += combs[i].process(input);
        }

        // Series AllPasses
        out = allpasses[0].process(out);
        out = allpasses[1].process(out);

        return out;
    }
};

struct Compressor {
    float threshold = 0.5f;  // Level above which compression starts (0.0 to 1.0)
    float ratio = 4.0f;      // How much to squash (e.g., 4.0 = 4:1)
    float makeup_gain = 1.0f;// Post-compression boost

    // Internal State
    float envelope = 0.0f;   // The "detected" volume level

    // Coefficients (calculated from time params)
    float alpha_attack = 0.0f;
    float alpha_release = 0.0f;

    void set_params(float thresh, float rat, float att_ms, float rel_ms, float gain, float sample_rate) {
        threshold = thresh;
        ratio = rat;
        makeup_gain = gain;

        // Convert time (ms) to one-pole filter coefficients
        alpha_attack = exp(-1.0f / (0.001f * att_ms * sample_rate));
        alpha_release = exp(-1.0f / (0.001f * rel_ms * sample_rate));
    }

    void process(float& L, float& R, float sidechain_signal = 0.0f) {
        // 1. Peak Detection: Use sidechain signal if provided, else use input
        float detector = (sidechain_signal != 0.0f) ? std::abs(sidechain_signal) : std::max(std::abs(L), std::abs(R));

        // 2. Envelope Follower
        if (detector > envelope) {
            envelope = alpha_attack * envelope + (1.0f - alpha_attack) * detector;
        }
        else {
            envelope = alpha_release * envelope + (1.0f - alpha_release) * detector;
        }

        // 3. Calculate Gain Reduction
        float gain = 1.0f;
        if (envelope > threshold) {
            // Calculate how much we are over the threshold in dB
            // (Using simplified math to avoid expensive log10 every sample if possible, 
            // but log/pow is necessary for correct dB behavior)

            float env_db = 20.0f * log10(envelope);
            float thresh_db = 20.0f * log10(threshold);

            float over_db = env_db - thresh_db;
            float reduction_db = over_db * (1.0f - (1.0f / ratio));

            gain = pow(10.0f, -reduction_db / 20.0f);
        }

        // 4. Apply Gain & Makeup
        L *= gain * makeup_gain;
        R *= gain * makeup_gain;

        // 5. Hard Limit (Safety Clipper) to prevent digital distortion
        if (L > 1.0f) L = 1.0f; else if (L < -1.0f) L = -1.0f;
        if (R > 1.0f) R = 1.0f; else if (R < -1.0f) R = -1.0f;
    }
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
    float probability = 1.0f;
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
        bool is_linear = false;      // If TRUE, ignores global loop and plays start-to-finish
        bool looping = false;      // If TRUE, linear is looping
        double linear_cursor = 0.0;  // Current position (Phase) in the song
        double total_length = 1.0;   // Total length of the pattern in Cycles
    };

    std::vector<Layer> layers;

    void set_cps(double cps) {
        if (cps <= 0) cps = 0.1;
        cycles_per_second = cps;
    }

    int count_steps(const std::string& pat) {
        int count = 0;
        int depth = 0;
        bool in_token = false;
        for (char c : pat) {
            if (c == '[') depth++;
            if (c == ']') depth--;
            if (depth == 0 && c != ' ') {
                if (!in_token) { count++; in_token = true; }
            }
            else if (depth == 0 && c == ' ') {
                in_token = false;
            }
        }
        return count > 0 ? count : 1;
    }

    // Parse Strudel-style parallel stack "< A , B , C >"
    void parse_parallel(std::string full_pattern) {
        // 1. Cleanup: Remove newlines and outer < >
        std::string clean = "";
        for (char c : full_pattern) {
            if (c == '\n' || c == '\r') clean += ' ';
            else clean += c;
        }

        // Trim spaces
        size_t start = clean.find_first_not_of(" ");
        size_t end = clean.find_last_not_of(" ");
        if (start != std::string::npos) clean = clean.substr(start, (end - start + 1));

        // Remove < > if present
        if (clean.front() == '<' && clean.back() == '>') {
            clean = clean.substr(1, clean.length() - 2);
        }

        // 2. Split by Comma
        std::vector<std::string> parts;
        std::string current;
        int bracket_depth = 0;

        for (char c : clean) {
            if (c == '[') bracket_depth++;
            if (c == ']') bracket_depth--;

            // Only split on comma if we are NOT inside brackets
            if (c == ',' && bracket_depth == 0) {
                parts.push_back(current);
                current.clear();
            }
            else {
                current += c;
            }
        }
        parts.push_back(current); // Push the last part

        // 3. Assign to Layers
        if (layers.size() < parts.size()) layers.resize(parts.size());

        for (int i = 0; i < parts.size(); ++i) {
            // CALL WITH LINEAR MODE = TRUE
            parse_pattern(i, parts[i], Waveform::SINE, true);
        }
    }

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
        // 1. Simple tokenizer respecting brackets [ ]
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

        // 2. Process each step
        double step_dur = dur / steps.size();
        for (size_t i = 0; i < steps.size(); ++i) {
            double step_start = start + (i * step_dur);
            std::string token = steps[i];

            // Cleanup whitespace
            size_t first = token.find_first_not_of(" ");
            size_t last = token.find_last_not_of(" ");
            if (first == std::string::npos) continue;
            token = token.substr(first, (last - first + 1));

            // 1. Check for Euclidean Syntax: e.g. "token(3,8)"
            size_t open_paren = token.find('(');
            size_t comma = token.find(',');
            size_t close_paren = token.find(')');

            if (open_paren != std::string::npos && comma != std::string::npos && close_paren != std::string::npos) {
                // Extract base sound: "bd"
                std::string base_sound = token.substr(0, open_paren);

                // Extract numbers
                int hits = std::stoi(token.substr(open_paren + 1, comma - (open_paren + 1)));
                int steps = std::stoi(token.substr(comma + 1, close_paren - (comma + 1)));

                // Bjorklund / Bresenham algorithm implementation
                if (steps > 0) {
                    double euclid_step_dur = step_dur / steps;
                    int bucket = 0;

                    for (int s = 0; s < steps; ++s) {
                        bucket += hits;
                        if (bucket >= steps) {
                            bucket -= steps;
                            // RECURSE: allows the base sound to be a group like "[bd sn]"
                            parse_recursive(out, base_sound, step_start + (s * euclid_step_dur), euclid_step_dur);
                        }
                    }
                }
                continue; // Skip standard processing for this token
            }

            // --- FIX: Robust Repetition Operator (*N) ---
            // Only accept * if it is at bracket depth 0
            int repeats = 1;
            int t_depth = 0;
            size_t star_pos = std::string::npos;

            for (size_t k = 0; k < token.length(); ++k) {
                if (token[k] == '[') t_depth++;
                else if (token[k] == ']') t_depth--;
                else if (token[k] == '*' && t_depth == 0) {
                    star_pos = k; // Found a top-level star
                }
            }

            if (star_pos != std::string::npos && star_pos < token.length() - 1) {
                try {
                    repeats = std::stoi(token.substr(star_pos + 1));
                    token = token.substr(0, star_pos); // Remove *N from token
                }
                catch (...) { repeats = 1; }
            }

            if (repeats < 1) repeats = 1;
            // ---------------------------------------------

            // Apply Repetition
            double sub_dur = step_dur / repeats;

            for (int r = 0; r < repeats; ++r) {
                double sub_start = step_start + (r * sub_dur);

                // Now process the cleaned token (recurse if group, add if note)
                if (token.front() == '[' && token.back() == ']') {
                    // Subdivision found, recurse!
                    std::string inner = token.substr(1, token.length() - 2);
                    parse_recursive(out, inner, sub_start, sub_dur);
                }
                else if (token == "~" || token == ".") {
                    // Rest
                }
                else {
                    // Leaf node (Note or Chord)

                    // 1. Check for Chord Syntax (comma-separated: "c3,e3,g3")
                    if (token.find(',') != std::string::npos) {
                        std::stringstream ss(token);
                        std::string note_part;

                        // Split by comma
                        while (std::getline(ss, note_part, ',')) {
                            // Trim whitespace from the note part (Critical for robustness)
                            size_t first = note_part.find_first_not_of(" ");
                            if (first == std::string::npos) continue; // Skip empty parts (e.g. "c3,,e3")
                            size_t last = note_part.find_last_not_of(" ");
                            note_part = note_part.substr(first, (last - first + 1));

                            std::string final_token = note_part;
                            float prob = 1.0f;

                            // Check for per-note probability (e.g. "c3,e3?0.5,g3")
                            size_t q_mark = note_part.find('?');
                            if (q_mark != std::string::npos) {
                                try {
                                    prob = std::stof(note_part.substr(q_mark + 1));
                                    final_token = note_part.substr(0, q_mark);
                                }
                                catch (...) { prob = 1.0f; }
                            }

                            // Push the event. Note that 'sub_start' is the SAME for all notes in the loop,
                            // causing them to play simultaneously.
                            out.push_back({ sub_start, sub_dur, final_token, false, prob });
                        }
                    }
                    else {
                        // Standard Single Note Logic (Existing Code)
                        std::string final_token = token;
                        float prob = 1.0f;
                        size_t q_mark = token.find('?');
                        if (q_mark != std::string::npos) {
                            std::string prob_str = token.substr(q_mark + 1);
                            final_token = token.substr(0, q_mark);
                            try { prob = std::stof(prob_str); }
                            catch (...) { prob = 1.0f; }
                        }

                        out.push_back({ sub_start, sub_dur, final_token, false, prob });
                    }
                }
            }
        }
    }
    // Strudel-like parser: "c3 [e3 g3] ~ a3"
    void parse_pattern(int layer_idx, const std::string& pattern, Waveform wave, bool linear_mode = false) {
        if (layer_idx >= layers.size()) layers.resize(layer_idx + 1);

        // Reset Layer
        auto& L = layers[layer_idx];
        L.pattern_source = pattern;
        L.synth_type = wave;
        L.events.clear();
        L.active = true;
        L.is_linear = linear_mode;
        L.linear_cursor = 0.0;

        // CALCULATE DURATION
        double duration = 1.0;

        if (linear_mode) {
            // "Sheet Music" Mode (SOUND.NOTE)
            // Duration grows with the number of steps (1 step = 1 Bar)
            int steps = count_steps(pattern);
            duration = (double)steps;
        }
        else {
            // "Sequencer" Mode (SOUND.SEQ)
            // The entire string fits into exactly 1 Loop Cycle
            duration = 1.0;
        }

        L.total_length = duration;

        // --- FIX: This line was inside the 'if' block, causing SEQ to be empty ---
        parse_recursive(L.events, pattern, 0.0, duration);
        // ------------------------------------------------------------------------
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

    void set_voice(int track_index, Waveform waveform, double a, double d, double s, double r);
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

    ReverbModule reverb_L;
    ReverbModule reverb_R;
    void set_reverb(float room_size, float damping, float width, float wet);

    Compressor master_compressor;
    void set_compressor(float thresh, float ratio, float attack, float release, float gain);

    void set_track_sample(int track, int sample_id, float base_freq, bool loop);
    void set_eq(int track, double low, double mid, double high);
    void set_unison(int track, int voices, double detune, double spread);
    void set_reverb_send(int track_index, double amount);
    void set_delay_send(int track_index, double amount);

    void reset();
    int allocate_voice(int track_id);

#ifdef JD_IMGUI
    std::vector<float> get_wave_data(); // Returns data for the GUI
    std::vector<float> get_reverb_wave_data();
    std::vector<float> get_delay_wave_data();
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
    void set_scale(int track_id, const std::string& root_note, const std::string& mode);
    float get_scale_freq(int degree, int track_id);
    void play_note_pattern(const std::string& pattern);
#endif
    bool is_initialized = false;

    // --- State for WAV file playback ---
    std::map<int, SoundChunk> loaded_samples; // Stores loaded WAV data, mapped by ID.
    std::vector<SoundChannel> channels;       // A pool of channels for playing sounds.
    std::vector<VoiceParams> tracks; // A vector to hold all our synthesizer tracks
    std::vector<ActiveVoice> voice_pool; // The 64 physical voices

    std::vector<float> last_track_envelopes;

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
    void generate_sample(ActiveVoice& voice, const VoiceParams& params, float& out_l, float& out_r);
    // PolyBLEP Helper
    // t: current phase (0..1), dt: phase increment per sample
    double poly_blep(double t, double dt);
    SDL_AudioDeviceID audio_device_id = 0;
    SDL_AudioSpec audio_spec;
    int music_channel_id = -1;
#ifdef JD_IMGUI
    std::vector<float> vis_buffer;
    size_t vis_head = 0;
    // Buffers for effect visualization
    std::vector<float> vis_buffer_reverb;
    size_t vis_head_reverb = 0;
    std::vector<float> vis_buffer_delay;
    size_t vis_head_delay = 0;
#endif

};
#endif
