#ifdef SDL3
#include "SoundSystem.hpp"
#include <cmath> // For sin, fmod
#include <map>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// This global map will be used in BuiltinFunctions.cpp to convert strings to enums.
const std::map<std::string, Waveform> waveform_map = {
    {"SINE", Waveform::SINE},
    {"SQUARE", Waveform::SQUARE},
    {"SAW", Waveform::SAWTOOTH},
    {"TRIANGLE", Waveform::TRIANGLE}
};

SoundSystem::SoundSystem() {}

SoundSystem::~SoundSystem() {
    shutdown();
}

bool SoundSystem::init(int num_tracks) {
    if (is_initialized) {
        return true;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        return false;
    }

    // --- UPDATED: Modern SDL3 audio initialization ---
    SDL_AudioSpec desired_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = 44100;
    desired_spec.format = SDL_AUDIO_F32;
    desired_spec.channels = 1;

    // Open a stream with a callback. This is the new way to do it.
    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec, &SoundSystem::audio_callback, this);

    if (audio_stream == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Get the device ID associated with the stream
    audio_device_id = SDL_GetAudioStreamDevice(audio_stream);

    // --- CORRECTED: Use the spec we requested, as this is the format we must provide. ---
    audio_spec = desired_spec;

    tracks.resize(num_tracks);

    // Start audio playback on the device.
    SDL_ResumeAudioDevice(audio_device_id);

    is_initialized = true;
    return true;
}

void SoundSystem::shutdown() {
    if (!is_initialized) return;

    // --- UPDATED: Correct cleanup order ---
    if (audio_device_id != 0) {
        SDL_PauseAudioDevice(audio_device_id);
    }
    if (audio_stream) {
        SDL_DestroyAudioStream(audio_stream);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    is_initialized = false;
}

// --- UPDATED: The new callback logic ---
// This function is now called by SDL to request more data for the stream.
void SoundSystem::audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len) {
    SoundSystem* self = static_cast<SoundSystem*>(userdata);
    int num_samples = additional_len / sizeof(float);
    if (num_samples == 0) {
        return;
    }

    std::vector<float> buffer(num_samples);

    for (int i = 0; i < num_samples; ++i) {
        float mixed_sample = 0.0f;

        for (auto& track : self->tracks) {
            if (track.adsr_state != ADSRState::OFF) {
                mixed_sample += self->generate_sample(track);
            }
        }

        mixed_sample /= (self->tracks.size() > 0 ? self->tracks.size() : 1);
        buffer[i] = mixed_sample;
    }

    // Put the newly generated audio data into the stream.
    SDL_PutAudioStreamData(stream, buffer.data(), additional_len);
}

float SoundSystem::generate_sample(Voice& voice) {
    float sample = 0.0f;
    double time_per_sample = 1.0 / audio_spec.freq;

    // --- (ADSR logic remains the same) ---
    switch (voice.adsr_state) {
    case ADSRState::ATTACK:
        voice.envelope_level += time_per_sample / voice.attack_time;
        if (voice.envelope_level >= 1.0) { voice.envelope_level = 1.0; voice.adsr_state = ADSRState::DECAY; }
        break;
    case ADSRState::DECAY:
        voice.envelope_level -= time_per_sample / voice.decay_time;
        if (voice.envelope_level <= voice.sustain_level) { voice.envelope_level = voice.sustain_level; voice.adsr_state = ADSRState::SUSTAIN; }
        break;
    case ADSRState::SUSTAIN: break;
    case ADSRState::RELEASE:
        voice.envelope_level -= time_per_sample / voice.release_time;
        if (voice.envelope_level <= 0.0) { voice.envelope_level = 0.0; voice.adsr_state = ADSRState::OFF; }
        break;
    case ADSRState::OFF: return 0.0f;
    }

    // --- (Waveform generation remains the same) ---
    switch (voice.waveform) {
    case Waveform::SINE: sample = sin(voice.phase); break;
    case Waveform::SQUARE: sample = (sin(voice.phase) >= 0) ? 1.0f : -1.0f; break;
    case Waveform::SAWTOOTH: sample = (fmod(voice.phase, 2.0 * M_PI) / M_PI) - 1.0; break;
    case Waveform::TRIANGLE: sample = 2.0f * (fabs(fmod(voice.phase, 2.0 * M_PI) / M_PI - 1.0f) - 0.5f); break;
    }

    voice.phase += 2.0 * M_PI * voice.frequency * time_per_sample;
    if (voice.phase >= 2.0 * M_PI) { voice.phase -= 2.0 * M_PI; }

    return sample * voice.envelope_level;
}

// --- UPDATED: All functions now lock the audio stream instead of the device ---
void SoundSystem::set_voice(int track_index, Waveform waveform, double attack, double decay, double sustain, double release) {
    if (track_index >= 0 && track_index < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track_index].waveform = waveform;
        tracks[track_index].attack_time = (attack > 0.001) ? attack : 0.001;
        tracks[track_index].decay_time = (decay > 0.001) ? decay : 0.001;
        tracks[track_index].sustain_level = sustain;
        tracks[track_index].release_time = (release > 0.001) ? release : 0.001;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::play_note(int track_index, double frequency) {
    if (track_index >= 0 && track_index < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track_index].frequency = frequency;
        tracks[track_index].phase = 0.0;
        tracks[track_index].adsr_state = ADSRState::ATTACK;
        tracks[track_index].envelope_level = 0.0;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::release_note(int track_index) {
    if (track_index >= 0 && track_index < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        if (tracks[track_index].adsr_state != ADSRState::OFF) {
            tracks[track_index].adsr_state = ADSRState::RELEASE;
        }
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::stop_note(int track_index) {
    if (track_index >= 0 && track_index < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track_index].adsr_state = ADSRState::OFF;
        tracks[track_index].envelope_level = 0.0;
        SDL_UnlockAudioStream(audio_stream);
    }
}
#endif
