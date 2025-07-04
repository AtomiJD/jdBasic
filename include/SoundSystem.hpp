#pragma once
#ifdef SDL3
#include <SDL3/SDL.h>
#include <vector>
#include <string>

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

class SoundSystem {
public:
    SoundSystem();
    ~SoundSystem();

    // Initializes SDL_Audio and opens an audio device
    bool init(int num_tracks = 8);

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

    bool is_initialized = false;

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
