#ifdef SDL3
#include "SoundSystem.hpp"
#include "TextIO.hpp"
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

bool SoundSystem::init(int num_tracks, int num_channels) {
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
    audio_spec = desired_spec;

    tracks.resize(num_tracks);
    channels.resize(num_channels);

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

    // --- Free all loaded sound chunks ---
    for (auto const& [id, chunk] : loaded_samples) {
        if (chunk.buffer) {
            SDL_free(chunk.buffer);
        }
    }
    loaded_samples.clear();

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    is_initialized = false;
}

// --- UPDATED: The audio callback now mixes synth voices AND sample channels ---
void SoundSystem::audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len) {
    SoundSystem* self = static_cast<SoundSystem*>(userdata);
    int num_samples = additional_len / sizeof(float);
    if (num_samples == 0) return;

    std::vector<float> buffer(num_samples);

    for (int i = 0; i < num_samples; ++i) {
        float mixed_sample = 0.0f;
        int active_sources = 0;

        // 1. Mix active synthesizer voices
        for (auto& track : self->tracks) {
            if (track.adsr_state != ADSRState::OFF) {
                mixed_sample += self->generate_sample(track);
                active_sources++;
            }
        }

        // 2. Mix active sound effect channels
        for (auto& channel : self->channels) {
            if (channel.is_active) {
                // Find the sound chunk data
                auto it = self->loaded_samples.find(channel.sample_id);
                if (it != self->loaded_samples.end()) {
                    SoundChunk& chunk = it->second;
                    // Check if we are still within the sound's length
                    if (channel.position < chunk.length) {
                        // Get the sample value (buffer is float*, position is in bytes)
                        mixed_sample += chunk.buffer[channel.position / sizeof(float)];
                        channel.position += sizeof(float);
                        active_sources++;
                    }
                    else {
                        // Sound has finished playing
                        channel.is_active = false;
                    }
                }
                else {
                    // Invalid ID, deactivate
                    channel.is_active = false;
                }
            }
        }

        // Simple averaging to prevent clipping.
        if (active_sources > 1) {
            mixed_sample /= active_sources;
        }

        buffer[i] = mixed_sample;
    }

    SDL_PutAudioStreamData(stream, buffer.data(), additional_len);
}


// --- Load a WAV file from disk ---
bool SoundSystem::load_sound(int sample_id, const std::string& filename) {
    if (!is_initialized) return false;

    SDL_AudioSpec loaded_spec;
    Uint8* loaded_buffer = nullptr;
    Uint32 loaded_length = 0;

    // Load the WAV file
    if (SDL_LoadWAV(filename.c_str(), &loaded_spec, &loaded_buffer, &loaded_length) == false) {
        TextIO::print("Failed to load WAV '" + filename + "': " + std::string(SDL_GetError()) + "\n");
        return false;
    }

    // Create a stream to convert the loaded audio to our desired format (AUDIO_F32)
    SDL_AudioStream* converter = SDL_CreateAudioStream(&loaded_spec, &audio_spec);
    if (converter == nullptr) {
        TextIO::print("Failed to create audio converter: " + std::string(SDL_GetError()) + "\n");
        SDL_free(loaded_buffer);
        return false;
    }

    // Put the loaded data into the converter
    SDL_PutAudioStreamData(converter, loaded_buffer, loaded_length);
    SDL_FlushAudioStream(converter);
    SDL_free(loaded_buffer); // Don't need the original buffer anymore

    // Get the total amount of converted data available
    int converted_bytes = SDL_GetAudioStreamAvailable(converter);
    float* converted_buffer = (float*)SDL_malloc(converted_bytes);
    if (!converted_buffer) {
        SDL_DestroyAudioStream(converter);
        return false;
    }

    // Read the converted data
    SDL_GetAudioStreamData(converter, converted_buffer, converted_bytes);
    SDL_DestroyAudioStream(converter);

    // If a sound with this ID already exists, free the old one first
    if (loaded_samples.count(sample_id) && loaded_samples[sample_id].buffer) {
        SDL_free(loaded_samples[sample_id].buffer);
    }

    // Store the new, converted sound data
    loaded_samples[sample_id] = { converted_buffer, (Uint32)converted_bytes };
    return true;
}


// --- Play a loaded WAV file ---
void SoundSystem::play_sound(int sample_id) {
    if (!is_initialized || loaded_samples.find(sample_id) == loaded_samples.end()) {
        return; // Sound not loaded or system not ready
    }

    SDL_LockAudioStream(audio_stream);

    // Find the first available (inactive) channel
    int channel_to_use = -1;
    for (int i = 0; i < channels.size(); ++i) {
        if (!channels[i].is_active) {
            channel_to_use = i;
            break;
        }
    }

    // If we found a free channel, configure it to play our sound
    if (channel_to_use != -1) {
        channels[channel_to_use].sample_id = sample_id;
        channels[channel_to_use].position = 0; // Start from the beginning
        channels[channel_to_use].is_active = true;
    }
    // If no channel is free, the sound is simply not played.

    SDL_UnlockAudioStream(audio_stream);
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

// --- All functions now lock the audio stream instead of the device ---
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
