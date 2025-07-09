#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <map>

// Enum for different oscillator waveforms
enum class Waveform {
    SINE,
    SQUARE,
    SAWTOOTH,
    TRIANGLE
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
};

class SoundSystem {
public:
    SoundSystem();
    ~SoundSystem();

    // Initializes SDL_Audio and opens an audio device
    bool init(int num_tracks = 8, int num_channels = 16);

    // Closes the audio device and cleans up
    void shutdown();

    // Configures a voice/track with waveform and ADSR parameters
    void set_voice(int track_index, Waveform waveform, double attack, double decay, double sustain, double release);

    // Starts playing a note on a specified track
    void play_note(int track_index, double frequency);

    // Starts the release phase for a note on a specified track
    void release_note(int track_index);

    // Immediately stops a note on a specified track
    void stop_note(int track_index);

    // Loads a WAV file and stores it with a given ID.
    // Returns true on success, false on failure.
    bool load_sound(int sample_id, const std::string& filename);

    // Plays a pre-loaded sound on the next available channel.
    void play_sound(int sample_id);

    bool is_initialized = false;

    // --- State for WAV file playback ---
    std::map<int, SoundChunk> loaded_samples; // Stores loaded WAV data, mapped by ID.
    std::vector<SoundChannel> channels;       // A pool of channels for playing sounds.

private:
    // --- UPDATED: The new SDL3 audio stream callback signature ---
    static void audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len);

    // Helper to generate a sample for a given voice
    float generate_sample(Voice& voice);

    // --- UPDATED: Pointers and IDs for the new API ---
    SDL_AudioDeviceID audio_device_id = 0;
    SDL_AudioStream* audio_stream = nullptr;
    SDL_AudioSpec audio_spec;

    std::vector<Voice> tracks; // A vector to hold all our synthesizer tracks


};
#endif
