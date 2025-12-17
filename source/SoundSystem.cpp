#include "AppConfig.hpp"
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
    {"TRIANGLE", Waveform::TRIANGLE},
    {"NOISE", Waveform::NOISE},
    {"SAMPLE", Waveform::SAMPLE}
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

    srand(static_cast<unsigned int>(time(nullptr)));
    delay_buffer.resize(44100 * 2 * 2, 0.0f);

#ifdef JD_IMGUI
    vis_buffer.resize(1024, 0.0f);
#endif
    // --- Modern SDL3 audio initialization ---
    SDL_AudioSpec desired_spec;
    SDL_zero(desired_spec);
    desired_spec.freq = 44100;
    desired_spec.format = SDL_AUDIO_F32;
    desired_spec.channels = 2;

    //// Open a stream with a callback. This is the new way to do it.
    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec, &SoundSystem::audio_callback, this);

    if (audio_stream == nullptr) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Get the device ID associated with the stream
    audio_device_id = SDL_GetAudioStreamDevice(audio_stream);
    audio_spec = desired_spec;

    tracks.resize(num_tracks);
    voice_pool.resize(64);
    channels.resize(num_channels);

    // Initialize Reverb with slightly different lengths for Stereo Width
    reverb_L.init(0);
    reverb_R.init(23); // Offset of 23 samples makes R distinct from L

    // Standard "Punchy" settings
    master_compressor.set_params(0.6f, 4.0f, 10.0f, 100.0f, 1.2f, (float)audio_spec.freq);

    // Start audio playback on the device.
    SDL_ResumeAudioDevice(audio_device_id);

    is_initialized = true;
    return true;
}

void SoundSystem::shutdown() {
    if (!is_initialized) return;

    // 1. Stop and Destroy the Audio Stream
    if (audio_stream) {
        // This stops the callback and unbinds from the device
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = nullptr;
    }

    // 3. Clean up memory
    for (auto const& [id, chunk] : loaded_samples) {
        if (chunk.buffer) SDL_free(chunk.buffer);
    }
    loaded_samples.clear();

    tracks.clear();
    channels.clear();

    is_initialized = false;
}

// Helper for anti-aliasing (smoothes discontinuities)
double SoundSystem::poly_blep(double t, double dt) {
    // t is phase (0..1), dt is phase_inc
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    else if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

int SoundSystem::allocate_voice(int track_id) {
    // 1. Try to find a completely inactive voice
    for (int i = 0; i < voice_pool.size(); ++i) {
        if (!voice_pool[i].active || voice_pool[i].adsr_state == ADSRState::OFF) {
            voice_pool[i].active = true;
            voice_pool[i].source_track_id = track_id;
            return i;
        }
    }

    // 2. If full, "steal" the voice with the lowest volume (oldest release)
    int best_candidate = 0;
    double lowest_level = 100.0;

    for (int i = 0; i < voice_pool.size(); ++i) {
        if (voice_pool[i].adsr_state == ADSRState::RELEASE) {
            if (voice_pool[i].envelope_level < lowest_level) {
                lowest_level = voice_pool[i].envelope_level;
                best_candidate = i;
            }
        }
    }

    // Reset the stolen voice
    voice_pool[best_candidate].active = true;
    voice_pool[best_candidate].source_track_id = track_id;
    return best_candidate;
}

// [SoundSystem.cpp]

void SoundSystem::audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len) {
    SoundSystem* self = static_cast<SoundSystem*>(userdata);
    int num_floats = additional_len / sizeof(float);
    int num_frames = num_floats / 2;
    std::vector<float> buffer(num_floats, 0.0f);
    double seconds_per_sample = 1.0 / self->audio_spec.freq;
    double phase_inc_per_frame = seconds_per_sample * self->sequencer.cycles_per_second;

#ifdef SDLMIXER
    // --- SEQUENCER LOGIC (PRE-CALCULATION BLOCK) ---
    // This runs ONCE per buffer, not per sample.

    double buffer_duration_phase = num_frames * phase_inc_per_frame;

    // 1. TRIGGER EVENTS
    for (int L = 0; L < self->sequencer.layers.size(); ++L) {
        auto& layer = self->sequencer.layers[L];
        if (!layer.active) continue;

        double start_window, end_window;
        bool layer_wrapped = false;

        int track_idx = L % self->tracks.size();

        // --- Handle Linear vs Looping Logic ---
        if (layer.is_linear) {
            start_window = layer.linear_cursor;
            end_window = start_window + buffer_duration_phase;

            // Advance Cursor
            layer.linear_cursor += buffer_duration_phase;

            // Check for End of Song
            if (end_window >= layer.total_length) {
                if (layer.looping) {
                    // WRAP AROUND (Looping Linear Mode)
                    layer.linear_cursor -= layer.total_length;
                    layer_wrapped = true;
                }
                else {
                    // STOP (One-Shot Mode)
                    // If we have fully played past the end, deactivate
                    if (start_window >= layer.total_length) {
                        layer.active = false;
                        continue; // Stop processing this layer
                    }
                }
            }
        }
        else {
            // Standard Global Sequencer Behavior
            start_window = self->sequencer.current_phase;
            end_window = start_window + buffer_duration_phase;
        }
        // -------------------------------------------

        for (auto& ev : layer.events) {
            bool in_window = false;

            if (layer.is_linear) {
                if (layer_wrapped) {
                    // Special check for the wrap-around point
                    // 1. Tail: Event is at the very end of the song
                    bool tail = (ev.start_phase >= start_window && ev.start_phase < layer.total_length);
                    // 2. Head: Event is at the very start of the song (0.0)
                    bool head = (ev.start_phase >= 0.0 && ev.start_phase < (end_window - layer.total_length));
                    in_window = tail || head;
                }
                else {
                    // Standard linear check
                    in_window = (ev.start_phase >= start_window && ev.start_phase < end_window);
                }
            }
            else {
                // Standard Global Sequencer Check
                if (end_window > 1.0) {
                    bool tail = (ev.start_phase >= start_window && ev.start_phase < 1.0);
                    bool head = (ev.start_phase >= 0.0 && ev.start_phase < (end_window - 1.0));
                    in_window = tail || head;
                }
                else {
                    in_window = (ev.start_phase >= start_window && ev.start_phase < end_window);
                }
            }

            if (!ev.triggered && in_window) {
                ev.triggered = true;

                // ROLL THE DICE
                float roll = (float)rand() / (float)RAND_MAX;
                if (roll > ev.probability) {
                    continue; // Skip playback this time
                }

                // Calculate Sample Offset
                // FIX: Use start_window!
                double phase_delta = 0.0;

                if (layer.is_linear && layer_wrapped && ev.start_phase < start_window) {
                    // Event is at start (Head), Window is at end (Tail)
                    // Delta = (Distance to End) + (Distance from 0.0)
                    phase_delta = (layer.total_length - start_window) + ev.start_phase;
                }
                else if (!layer.is_linear && end_window > 1.0 && ev.start_phase < start_window) {
                    // Standard wrap logic
                    phase_delta = (1.0 - start_window) + ev.start_phase;
                }
                else {
                    phase_delta = ev.start_phase - start_window;
                }

                int offset_frames = (int)(phase_delta / phase_inc_per_frame);
                if (offset_frames < 0) offset_frames = 0;
                if (offset_frames >= num_frames) offset_frames = num_frames - 1;

                // Resolve Frequency
                float freq = NoteMap::get(ev.token);

                if (freq == 0.0f) {
                    // If not a note name (e.g. "C3"), try parsing as a Scale Degree (e.g. "0", "4", "-1")
                    try {
                        size_t parsed_len = 0;
                        int degree = std::stoi(ev.token, &parsed_len);
                        // Ensure the whole string was a number
                        if (parsed_len == ev.token.length()) {
                            freq = self->get_scale_freq(degree, track_idx);
                        }
                    }
                    catch (...) {
                        // Not a number either, just ignore
                    }
                }

                if (freq > 0.0f) {
                    // Update Track Settings (Params)
                    int track_idx = L % self->tracks.size();
                    if (layer.synth_type != Waveform::SINE) {
                        self->tracks[track_idx].waveform = layer.synth_type;
                    }

                    // Allocate Voice (Polyphony)
                    int v_idx = self->allocate_voice(track_idx);
                    auto& v = self->voice_pool[v_idx];

                    // Initialize Voice
                    v.frequency = freq;
                    v.phase = 0.0;
                    v.adsr_state = ADSRState::ATTACK;
                    v.envelope_level = 0.0;
                    v.start_delay_frames = offset_frames;

                    // Effects Reset
                    v.lfo_phase = 0.0;
                    v.mod_phase = 0.0;
                    v.ring_phase = 0.0;

                    for (int i = 0; i < 16; ++i) {
                        v.unison_phases[i] = ((float)rand() / RAND_MAX);
                        v.unison_cursors[i] = 0.0; // Reset Sample Cursors
                    }

                    // Sequencer Control
                    v.controlled_by_sequencer = true;
                    v.seq_duration_remaining = ev.duration;
                }
            }
        }
        // Reset Triggers if wrapped
        if (layer.is_linear && layer_wrapped) {
            for (auto& ev : layer.events) ev.triggered = false;
        }
    }

    // 2. GATE OFF LOGIC (Duration Handling)
    // We iterate the ACTIVE VOICES to see if they should stop
    for (auto& voice : self->voice_pool) {
        if (voice.active && voice.controlled_by_sequencer &&
            voice.adsr_state != ADSRState::OFF && voice.adsr_state != ADSRState::RELEASE) {

            // Decrement duration by the length of this buffer
            voice.seq_duration_remaining -= buffer_duration_phase;

            // If time is up, trigger release
            if (voice.seq_duration_remaining <= 0.0) {
                voice.adsr_state = ADSRState::RELEASE;
                voice.controlled_by_sequencer = false;
            }
        }
    }

    // 3. ADVANCE PHASE
    self->sequencer.current_phase += buffer_duration_phase;
    if (self->sequencer.current_phase >= 1.0) {
        self->sequencer.current_phase -= 1.0;
        // Reset triggers
        for (auto& layer : self->sequencer.layers) {
            for (auto& ev : layer.events) ev.triggered = false;
        }
    }
#endif

    // --- MIXING LOOP (PER SAMPLE) ---
    for (int i = 0; i < num_frames; ++i) {
        float mix_L = 0.0f;
        float mix_R = 0.0f;
        int active_sources = 0;

        // 1. Mix Synthesizer Voices
        for (auto& voice : self->voice_pool) {
            if (!voice.active) continue;
            if (voice.source_track_id == -1) continue;

            const auto& params = self->tracks[voice.source_track_id];

            float v_left, v_right;
            // Call new stereo generator
            self->generate_sample(voice, params, v_left, v_right);

            if (voice.adsr_state == ADSRState::OFF) {
                voice.active = false;
                continue;
            }

            // Apply Track Pan (Master Pan for the whole unison stack)
            float trk_pan_l = 1.0f - params.pan;
            float trk_pan_r = params.pan;

            mix_L += v_left * trk_pan_l * params.gain;
            mix_R += v_right * trk_pan_r * params.gain;
        }

        // 2. Mix Sound Effects (SFX)
        for (auto& channel : self->channels) {
            if (channel.is_active) {
                auto it = self->loaded_samples.find(channel.sample_id);
                if (it != self->loaded_samples.end()) {
                    SoundChunk& chunk = it->second;
                    if (channel.position + sizeof(float) * 2 <= chunk.length) {
                        int float_idx = channel.position / sizeof(float);
                        float s_l = chunk.buffer[float_idx];
                        float s_r = chunk.buffer[float_idx + 1];

                        mix_L += s_l;
                        mix_R += s_r;
                        channel.position += sizeof(float) * 2;
                        active_sources++;
                    }
                    else {
                        if (channel.is_looping) channel.position = 0;
                        else channel.is_active = false;
                    }
                }
            }
        }

        // Soft Averaging
        if (active_sources > 1) {
            mix_L /= std::sqrt((float)active_sources);
            mix_R /= std::sqrt((float)active_sources);
        }

        // 3. Stereo Delay
        if (self->delay_active) {
            size_t delay_frames = (size_t)((self->delay_time_ms / 1000.0) * self->audio_spec.freq);
            size_t read_pos = self->delay_head;
            if (read_pos < delay_frames) read_pos += (self->delay_buffer.size() / 2);
            read_pos -= delay_frames;

            float d_l = self->delay_buffer[read_pos * 2];
            float d_r = self->delay_buffer[read_pos * 2 + 1];

            self->delay_buffer[self->delay_head * 2] = mix_L + (d_l * self->delay_feedback);
            self->delay_buffer[self->delay_head * 2 + 1] = mix_R + (d_r * self->delay_feedback);

            self->delay_head++;
            if (self->delay_head >= (self->delay_buffer.size() / 2)) self->delay_head = 0;

            mix_L += d_l * self->delay_mix;
            mix_R += d_r * self->delay_mix;
        }

        // 4. Distortion
        if (self->distortion_amount > 0.0) {
            mix_L = std::tanh(mix_L * (1.0 + self->distortion_amount));
            mix_R = std::tanh(mix_R * (1.0 + self->distortion_amount));
        }

        // 5. REVERB ---
        // (Assuming reverb_L.wet_mix > 0.0)
        if (self->reverb_L.wet_mix > 0.0f) {
            float mono_in = (mix_L + mix_R) * 0.5f;

            float wet_L = self->reverb_L.process(mono_in);
            float wet_R = self->reverb_R.process(mono_in);

            // Apply Width (Spread)
            // If width=0, wet_L and wet_R are panned center.
            // If width=1, they are hard L/R.

            float w = self->reverb_L.width; // Shared param
            float final_wet_L = wet_L * (1.0f + w) + wet_R * (1.0f - w);
            float final_wet_R = wet_R * (1.0f + w) + wet_L * (1.0f - w);

            // Mix Dry/Wet
            mix_L = (mix_L * self->reverb_L.dry_mix) + (final_wet_L * self->reverb_L.wet_mix * 0.2f); // *0.2 to tame gain
            mix_R = (mix_R * self->reverb_R.dry_mix) + (final_wet_R * self->reverb_R.wet_mix * 0.2f);
        }

        // 6. Visualization
#ifdef JD_IMGUI
        if (!self->vis_buffer.empty()) {
            self->vis_buffer[self->vis_head] = (mix_L + mix_R) * 0.5f;
            self->vis_head = (self->vis_head + 1) % self->vis_buffer.size();
        }
#endif

        // --- 7. MASTER COMPRESSOR ---
        self->master_compressor.process(mix_L, mix_R);

        buffer[i * 2] = mix_L;
        buffer[i * 2 + 1] = mix_R;
    }
    SDL_PutAudioStreamData(stream, buffer.data(), additional_len);
}
void SoundSystem::reset() {
    if (!is_initialized) return;

    SDL_LockAudioStream(audio_stream);

    // 1. Reset Logical Tracks (Params)
    for (auto& track : tracks) {
        track.waveform = Waveform::SINE;
        track.attack_time = 0.01;
        track.decay_time = 0.1;
        track.sustain_level = 0.8;
        track.release_time = 0.2;

        track.filter_cutoff = 20000.0;
        track.filter_resonance = 0.0; // Reset resonance
        track.fm_amount = 0.0;
        track.crush_bits = 0.0;
        track.ring_mix = 0.0;
        track.gain = 1.0;
        track.pan = 0.5;
        track.eq_low = 1.0;
        track.eq_mid = 1.0;
        track.eq_high = 1.0;
        // Reset Sample settings
        track.sample_id = -1;
        track.sample_loop = false;
    }

    // 2. Kill all Physical Voices AND CLEAR INTERNAL STATE
    for (auto& voice : voice_pool) {
        voice.active = false;
        voice.source_track_id = -1;
        voice.adsr_state = ADSRState::OFF;
        voice.envelope_level = 0.0;

        // --- CRITICAL FIX: Clear internal effect memory ---
        voice.filter_state_L = 0.0;
        voice.filter_state_R = 0.0;
        voice.lfo_phase = 0.0;
        voice.mod_phase = 0.0;
        voice.ring_phase = 0.0;
        voice.crush_counter = 0.0;
        voice.crush_last_sample = 0.0f;

        voice.eq_low_state_L = 0.0; voice.eq_low_state_R = 0.0;
        voice.eq_high_state_L = 0.0; voice.eq_high_state_R = 0.0;

        // --- CRITICAL FIX: Unbind from Sequencer ---
        // If this stays TRUE, manual notes in the next script might die instantly
        voice.controlled_by_sequencer = false;
        voice.seq_duration_remaining = 0.0;

        // Clear Unison state
        for (int i = 0; i < 16; ++i) {
            voice.unison_phases[i] = 0.0;
            voice.unison_cursors[i] = 0.0;
        }
    }

    // 3. Stop SFX Channels
    for (auto& chan : channels) {
        chan.is_active = false;
    }

    // 4. Clear Sequencer Completely
#ifdef SDLMIXER
    // Clear the vector to remove old layer configurations (looping flags, synth types)
    sequencer.layers.clear();
    sequencer.current_phase = 0.0;
#endif

    // 5. Reset Global Effects
    delay_active = false;
    distortion_amount = 0.0;
    // Clear Main Delay Buffer
    std::fill(delay_buffer.begin(), delay_buffer.end(), 0.0f);

    // 6. Clear Reverb Buffers ---
    // The reverb has internal delay lines that hold old audio. We must zero them.
    auto clear_reverb = [](ReverbModule& r) {
        r.set_params(0.0f, 0.0f, 0.0f, 0.0f); // Reset params
        // Manually clear the buffers inside the structs
        for (int i = 0; i < 4; ++i) std::fill(r.combs[i].delay.buffer.begin(), r.combs[i].delay.buffer.end(), 0.0f);
        for (int i = 0; i < 2; ++i) std::fill(r.allpasses[i].delay.buffer.begin(), r.allpasses[i].delay.buffer.end(), 0.0f);
        };
    clear_reverb(reverb_L);
    clear_reverb(reverb_R);

    // 7. Reset Logical Tracks
    for (auto& track : tracks) {
        // ... (Existing resets for ADSR, Filter, etc.) ...

        // --- NEW: Reset Scale ---
        track.scale_root_midi = 48; // Reset to C3
        track.scale_intervals = { 0, 2, 4, 5, 7, 9, 11 }; // Reset to Major
        // Or use Chromatic: {0,1,2,3,4,5,6,7,8,9,10,11}
    }

    // Reset Compressor
    master_compressor.set_params(1.0f, 1.0f, 10.0f, 100.0f, 1.0f, 44100.0f);
    master_compressor.envelope = 0.0f;

    SDL_UnlockAudioStream(audio_stream);
}
// --- Load a WAV file from disk ---
bool SoundSystem::load_sound(int sample_id, const std::string& filename) {
    if (!is_initialized) return false;

    SDL_AudioSpec loaded_spec;
    Uint8* loaded_buffer = nullptr;
    Uint32 loaded_length = 0;

    // Load the WAV file
    if (SDL_LoadWAV(filename.c_str(), &loaded_spec, &loaded_buffer, &loaded_length) == false) {
        TextIO::print("Failed to load WAV '" + filename + "': " + std::string(SDL_GetError())); TextIO::nl();
        return false;
    }

    // Create a stream to convert the loaded audio to our desired format (AUDIO_F32)
    SDL_AudioStream* converter = SDL_CreateAudioStream(&loaded_spec, &audio_spec);
    if (converter == nullptr) {
        TextIO::print("Failed to create audio converter: " + std::string(SDL_GetError())); TextIO::nl();
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
void SoundSystem::play_sound(int sample_id, bool looping) {
    if (!is_initialized || loaded_samples.find(sample_id) == loaded_samples.end()) {
        return;
    }

    SDL_LockAudioStream(audio_stream);

    int channel_to_use = -1;
    for (int i = 0; i < channels.size(); ++i) {
        if (!channels[i].is_active) {
            channel_to_use = i;
            break;
        }
    }

    if (channel_to_use != -1) {
        channels[channel_to_use].sample_id = sample_id;
        channels[channel_to_use].position = 0;
        channels[channel_to_use].is_looping = looping; // Set the looping flag
        channels[channel_to_use].is_active = true;
    }

    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::play_music(int sample_id, bool looping) {
    if (!is_initialized || loaded_samples.find(sample_id) == loaded_samples.end()) {
        return;
    }

    // Stop any previously playing music first
    stop_music();

    SDL_LockAudioStream(audio_stream);

    int channel_to_use = -1;
    for (int i = 0; i < channels.size(); ++i) {
        if (!channels[i].is_active) {
            channel_to_use = i;
            break;
        }
    }

    if (channel_to_use != -1) {
        channels[channel_to_use].sample_id = sample_id;
        channels[channel_to_use].position = 0;
        channels[channel_to_use].is_looping = looping;
        channels[channel_to_use].is_active = true;
        music_channel_id = channel_to_use; // IMPORTANT: Remember which channel is the music
    }

    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::stop_music() {
    if (music_channel_id != -1 && music_channel_id < channels.size()) {
        SDL_LockAudioStream(audio_stream);
        channels[music_channel_id].is_active = false; // Deactivate the specific music channel
        music_channel_id = -1; // Forget the channel
        SDL_UnlockAudioStream(audio_stream);
    }
}

void apply_eq_band(float& sample, double& low_state, double& high_state, const VoiceParams& params, double time_per_sample) {
    // Crossover Frequencies
    double low_cutoff = 250.0;
    double high_cutoff = 3000.0;

    // 1. Low Band
    double rc_low = 1.0 / (2.0 * M_PI * low_cutoff);
    double alpha_low = time_per_sample / (rc_low + time_per_sample);
    low_state += alpha_low * (sample - low_state);
    double low_part = low_state;

    // 2. High Band
    double rc_high = 1.0 / (2.0 * M_PI * high_cutoff);
    double alpha_high = time_per_sample / (rc_high + time_per_sample);
    high_state += alpha_high * (sample - high_state);
    double high_part = sample - high_state;

    // 3. Mid Band
    double mid_part = sample - low_part - high_part;

    // 4. Sum
    sample = (low_part * params.eq_low) + (mid_part * params.eq_mid) + (high_part * params.eq_high);
}

void SoundSystem::generate_sample(ActiveVoice& voice, const VoiceParams& params, float& out_l, float& out_r) {
    out_l = 0.0f;
    out_r = 0.0f;

    if (voice.start_delay_frames > 0) { voice.start_delay_frames--; return; }

    double time_per_sample = 1.0 / audio_spec.freq;

    // --- LFO Update ---
    voice.lfo_phase += params.lfo_frequency * time_per_sample;
    if (voice.lfo_phase >= 1.0) voice.lfo_phase -= 1.0;
    float lfo_val = sin(voice.lfo_phase * 2.0 * M_PI);

    // Base Frequency
    double base_freq = voice.frequency + (lfo_val * params.lfo_depth);
    if (base_freq < 0) base_freq = 0;

    // --- ADSR Logic ---
    if (voice.adsr_state == ADSRState::OFF) return;
    switch (voice.adsr_state) {
    case ADSRState::ATTACK:
        voice.envelope_level += time_per_sample / params.attack_time;
        if (voice.envelope_level >= 1.0) { voice.envelope_level = 1.0; voice.adsr_state = ADSRState::DECAY; }
        break;
    case ADSRState::DECAY:
        voice.envelope_level -= time_per_sample / params.decay_time;
        if (voice.envelope_level <= params.sustain_level) { voice.envelope_level = params.sustain_level; voice.adsr_state = ADSRState::SUSTAIN; }
        break;
    case ADSRState::SUSTAIN: break;
    case ADSRState::RELEASE:
        voice.envelope_level -= time_per_sample / params.release_time;
        if (voice.envelope_level <= 0.0) { voice.envelope_level = 0.0; voice.adsr_state = ADSRState::OFF; }
        break;
    }

    // --- FM Setup ---
    double mod_freq = base_freq * params.fm_ratio;
    voice.mod_phase += mod_freq * time_per_sample;
    if (voice.mod_phase >= 1.0) voice.mod_phase -= 1.0;

    // Calculate FM depth once
    float mod_val = sin(voice.mod_phase * 2.0 * M_PI) * (params.fm_amount / (2.0 * M_PI));

    // --- Unison Setup ---
    int count = params.unison_voices;
    if (count < 1) count = 1;
    if (count > 16) count = 16;

    float accum_l = 0.0f;
    float accum_r = 0.0f;

    for (int k = 0; k < count; ++k) {
        // 1. Calculate Detune & Spread
        double ratio = 0.0;
        if (count > 1) ratio = (double)k / (double)(count - 1) - 0.5;

        double detune_factor = 1.0 + (ratio * params.unison_detune * 0.1);
        double final_freq = base_freq * detune_factor;
        double dt = final_freq * time_per_sample;

        // 2. Waveform Generation
        float raw_sample = 0.0f;

        if (params.waveform == Waveform::SAMPLE) {
            // --- SAMPLE PLAYBACK (With Unison Support) ---
            if (loaded_samples.count(params.sample_id)) {
                const SoundChunk& chunk = loaded_samples[params.sample_id];

                // Sample Rate adjusted by Detune and FM
                double rate = (final_freq / params.sample_base_freq);
                rate += (mod_val * 4.0); // Apply FM to sample speed
                if (rate < 0.0) rate = 0.0;

                // Use the specific cursor for this unison voice
                double& cursor = voice.unison_cursors[k];

                int pos_int = (int)cursor;
                float frac = (float)(cursor - pos_int);
                int max_idx = (chunk.length / sizeof(float)) - 2;
                int index_A = pos_int * 2; // Stereo Interleaved

                if (index_A < max_idx) {
                    // Simple Mono read from Left channel for mixing
                    float sampA = chunk.buffer[index_A];
                    float sampB = chunk.buffer[index_A + 2];
                    raw_sample = sampA + frac * (sampB - sampA);
                }

                cursor += rate;

                // Loop Logic
                int total_frames = (chunk.length / sizeof(float)) / 2;
                if (cursor >= total_frames) {
                    if (params.sample_loop) cursor = fmod(cursor, (double)total_frames);
                    else raw_sample = 0.0f;
                }
            }
        }
        else {
            // --- SYNTH PLAYBACK ---
            // Apply FM: Add mod_val to phase
            double t = voice.unison_phases[k] + mod_val;
            t -= floor(t); // Wrap

            switch (params.waveform) {
            case Waveform::SINE: raw_sample = sin(t * 2.0 * M_PI); break;
            case Waveform::SAWTOOTH:
                raw_sample = (2.0 * t) - 1.0;
                raw_sample -= poly_blep(t, dt);
                break;
            case Waveform::SQUARE:
                raw_sample = (t < 0.5) ? 1.0 : -1.0;
                raw_sample += poly_blep(t, dt);
                { double t2 = t + 0.5; if (t2 >= 1.0) t2 -= 1.0; raw_sample -= poly_blep(t2, dt); }
                break;
            case Waveform::TRIANGLE:
                raw_sample = 2.0f * (fabs(2.0f * t - 1.0f) - 0.5f);
                break;
            case Waveform::NOISE:
                raw_sample = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
                break;
            }

            // Advance Phase
            voice.unison_phases[k] += dt;
            if (voice.unison_phases[k] >= 1.0) voice.unison_phases[k] -= 1.0;
        }

        // 3. Stereo Panning
        float pan = 0.5f + (ratio * params.unison_spread);
        if (pan < 0.0f) pan = 0.0f; if (pan > 1.0f) pan = 1.0f;

        accum_l += raw_sample * (1.0f - pan);
        accum_r += raw_sample * pan;
    }

    // Normalize Volume
    float norm = 1.0f / sqrt((float)count);
    accum_l *= norm;
    accum_r *= norm;

    // --- STEREO EFFECTS CHAIN ---

    // 1. Filter (Low Pass) - Independent L/R
    double rc = 1.0 / (2.0 * M_PI * params.filter_cutoff);
    double alpha = time_per_sample / (rc + time_per_sample);
    if (alpha > 1.0) alpha = 1.0;

    voice.filter_state_L += alpha * (accum_l - voice.filter_state_L);
    voice.filter_state_R += alpha * (accum_r - voice.filter_state_R);

    accum_l = voice.filter_state_L;
    accum_r = voice.filter_state_R;

    // 2. Ring Mod
    if (params.ring_mix > 0.0) {
        voice.ring_phase += params.ring_freq * time_per_sample;
        if (voice.ring_phase >= 1.0) voice.ring_phase -= 1.0;
        float carrier = sin(voice.ring_phase * 2.0 * M_PI);

        float wet_l = accum_l * carrier;
        float wet_r = accum_r * carrier;

        accum_l = (accum_l * (1.0 - params.ring_mix)) + (wet_l * params.ring_mix);
        accum_r = (accum_r * (1.0 - params.ring_mix)) + (wet_r * params.ring_mix);
    }

    // 3. Bitcrusher (Stereo)
    if (params.crush_bits > 0.0) {
        voice.crush_counter += params.crush_rate;
        if (voice.crush_counter >= 1.0) {
            voice.crush_counter -= 1.0;
            float steps = pow(2.0f, (float)params.crush_bits);
            // We reuse 'crush_last_sample' but ideally need a stereo struct for this.
            // For now, crushing L and R identically at the same time step is acceptable.
            voice.crush_last_sample = floor(accum_l * steps) / steps;
            // Note: True stereo crush needs 'crush_last_sample_R'
            // For now, we apply the quantized L to R which is "Dual Mono" crushing.
            // Let's just crush directly without hold for simplicity or accept mono-ish artifact
            accum_l = floor(accum_l * steps) / steps;
            accum_r = floor(accum_r * steps) / steps;
        }
        // Note: Proper sample-and-hold reduction requires storing L and R state separately.
        // If you hear artifacts, this is why.
    }

    // 4. EQ (Stereo)
    apply_eq_band(accum_l, voice.eq_low_state_L, voice.eq_high_state_L, params, time_per_sample);
    apply_eq_band(accum_r, voice.eq_low_state_R, voice.eq_high_state_R, params, time_per_sample);

    // --- FINAL OUTPUT ---
    out_l = accum_l * voice.envelope_level;
    out_r = accum_r * voice.envelope_level;
}

// --- All functions now lock the audio stream instead of the device ---
void SoundSystem::set_voice(int track_index, Waveform waveform, double attack, double decay, double sustain, double release) {
    if (track_index >= 0 && track_index < tracks.size()) {
        SDL_LockAudioStream(audio_stream);

        // Update the Parameters (VoiceParams)
        tracks[track_index].waveform = waveform;
        tracks[track_index].attack_time = (attack > 0.001) ? attack : 0.001;
        tracks[track_index].decay_time = (decay > 0.001) ? decay : 0.001;
        tracks[track_index].sustain_level = sustain;
        tracks[track_index].release_time = (release > 0.001) ? release : 0.001;

        // Reset Timbre Defaults 
        tracks[track_index].filter_cutoff = 20000.0;
        tracks[track_index].filter_resonance = 0.0;
        tracks[track_index].lfo_depth = 0.0;
        tracks[track_index].lfo_frequency = 5.0;
        tracks[track_index].fm_amount = 0.0;
        tracks[track_index].fm_ratio = 1.0;
        tracks[track_index].crush_bits = 0.0;
        tracks[track_index].ring_mix = 0.0;

        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::play_note(int track_index, double frequency) {
    if (track_index < 0 || track_index >= tracks.size()) return;
    SDL_LockAudioStream(audio_stream);

    int v_idx = allocate_voice(track_index);

    // Reset Voice State
    auto& v = voice_pool[v_idx];
    v.frequency = frequency;
    // Reset ALL unison phases
    for (int i = 0; i < 16; ++i) {
        v.unison_phases[i] = ((float)rand() / RAND_MAX);
        v.unison_cursors[i] = 0.0; // Reset Sample Cursors
    }
    v.phase = 0.0;
    v.adsr_state = ADSRState::ATTACK;
    v.envelope_level = 0.0;
    v.start_delay_frames = 0; // Immediate play for manual keys

    // Reset LFO/Mod phases to keep sound consistent
    v.lfo_phase = 0.0;
    v.mod_phase = 0.0;
    v.ring_phase = 0.0;

    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::release_note(int track_index) {
    if (track_index < 0 || track_index >= tracks.size()) return;
    SDL_LockAudioStream(audio_stream);

    // Find ALL voices playing this track and trigger release
    for (auto& voice : voice_pool) {
        if (voice.active && voice.source_track_id == track_index) {
            if (voice.adsr_state != ADSRState::OFF) {
                voice.adsr_state = ADSRState::RELEASE;
            }
        }
    }
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::stop_note(int track_index) {
    if (track_index < 0 || track_index >= tracks.size()) return;
    SDL_LockAudioStream(audio_stream);

    // Find ALL voices playing this track and kill them
    for (auto& voice : voice_pool) {
        if (voice.active && voice.source_track_id == track_index) {
            voice.adsr_state = ADSRState::OFF;
            voice.envelope_level = 0.0;
            voice.active = false;
        }
    }
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::set_gain(int track_index, double gain) {
    if (track_index >= 0 && track_index < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track_index].gain = gain;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_pan(int track_index, double pan) {
    if (track_index >= 0 && track_index < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        // Clamp to 0.0 - 1.0
        tracks[track_index].pan = (pan < 0.0) ? 0.0 : ((pan > 1.0) ? 1.0 : pan);
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_delay(bool active, double time_ms, double feedback, double mix) {
    SDL_LockAudioStream(audio_stream);
    delay_active = active;
    delay_time_ms = time_ms;
    delay_feedback = feedback;
    delay_mix = mix;
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::set_fm(int track, double amount, double ratio) {
    if (track >= 0 && track < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track].fm_amount = amount;
        tracks[track].fm_ratio = ratio;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_distortion(double amount) {
    SDL_LockAudioStream(audio_stream);
    distortion_amount = amount;
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::set_bitcrusher(int track, double bits, double rate) {
    if (track >= 0 && track < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track].crush_bits = bits;
        tracks[track].crush_rate = (rate > 1.0) ? 1.0 : (rate < 0.01 ? 0.01 : rate);
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_ringmod(int track, double freq, double mix) {
    if (track >= 0 && track < tracks.size() && audio_stream) {
        SDL_LockAudioStream(audio_stream);
        tracks[track].ring_freq = freq;
        tracks[track].ring_mix = mix;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_reverb(float room_size, float damping, float width, float wet) {
    SDL_LockAudioStream(audio_stream);
    // Safety Clamps
    if (room_size > 0.98f) room_size = 0.98f;
    reverb_L.set_params(room_size, damping, width, wet);
    reverb_R.set_params(room_size, damping, width, wet);
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::set_compressor(float thresh, float ratio, float attack, float release, float gain) {
    SDL_LockAudioStream(audio_stream);
    master_compressor.set_params(thresh, ratio, attack, release, gain, (float)audio_spec.freq);
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::set_track_sample(int track, int sample_id, float base_freq, bool loop) {
    if (track >= 0 && track < tracks.size()) {
        SDL_LockAudioStream(audio_stream);
        tracks[track].sample_id = sample_id;
        tracks[track].sample_base_freq = base_freq;
        tracks[track].sample_loop = loop;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_eq(int track, double low, double mid, double high) {
    if (track >= 0 && track < tracks.size()) {
        SDL_LockAudioStream(audio_stream);
        tracks[track].eq_low = low;
        tracks[track].eq_mid = mid;
        tracks[track].eq_high = high;
        SDL_UnlockAudioStream(audio_stream);
    }
}

void SoundSystem::set_unison(int track, int voices, double detune, double spread) {
    if (track >= 0 && track < tracks.size()) {
        SDL_LockAudioStream(audio_stream);
        tracks[track].unison_voices = voices;
        tracks[track].unison_detune = detune;
        tracks[track].unison_spread = spread;
        SDL_UnlockAudioStream(audio_stream);
    }
}

#ifdef JD_IMGUI
// Returns the buffer rotated so it looks like a continuous wave
std::vector<float> SoundSystem::get_wave_data() {
    std::vector<float> result;
    if (vis_buffer.empty()) return result;

    SDL_LockAudioStream(audio_stream);
    result.resize(vis_buffer.size());
    // Copy in two chunks to "unroll" the circular buffer
    size_t head = vis_head;
    size_t tail_len = vis_buffer.size() - head;

    // Copy from head to end
    std::copy(vis_buffer.begin() + head, vis_buffer.end(), result.begin());
    // Copy from start to head
    std::copy(vis_buffer.begin(), vis_buffer.begin() + head, result.begin() + tail_len);

    SDL_UnlockAudioStream(audio_stream);
    return result;
}
#endif

#ifdef SDLMIXER
void SoundSystem::update_sequence(int layer, const std::string& pattern, const std::string& waveform_str) {
    SDL_LockAudioStream(audio_stream);
    Waveform wf = Waveform::SINE;
    if (waveform_map.count(waveform_str)) wf = waveform_map.at(waveform_str);
    sequencer.parse_pattern(layer, pattern, wf);
    SDL_UnlockAudioStream(audio_stream);
}

void SoundSystem::set_bpm(double bpm) {
    SDL_LockAudioStream(audio_stream);
    sequencer.set_cps(bpm / 60.0 / 4.0); // Assuming 4 beats per cycle
    SDL_UnlockAudioStream(audio_stream);
}

float SoundSystem::get_scale_freq(int degree, int track_id) {
    if (track_id < 0 || track_id >= tracks.size()) return 0.0f;

    const auto& track = tracks[track_id]; // Access specific track
    const auto& intervals = track.scale_intervals;

    if (intervals.empty()) return 0.0f;

    int num_notes = intervals.size();

    // Standard Scale Math
    int octave_shift = (degree >= 0) ? (degree / num_notes) : ((degree - num_notes + 1) / num_notes);
    int idx = (degree >= 0) ? (degree % num_notes) : ((degree % num_notes + num_notes) % num_notes);

    int semitone_offset = intervals[idx];
    int target_midi = track.scale_root_midi + (octave_shift * 12) + semitone_offset;

    return 440.0f * std::pow(2.0f, (target_midi - 69.0f) / 12.0f);
}

void SoundSystem::set_scale(int track_id, const std::string& root_note, const std::string& mode) {
    if (track_id < 0 || track_id >= tracks.size()) return;

    SDL_LockAudioStream(audio_stream);

    auto& track = tracks[track_id];

    // 1. Convert Root Note String to MIDI
    float root_freq = NoteMap::get(root_note);
    if (root_freq > 0.0f) {
        track.scale_root_midi = std::round(69 + 12 * std::log2(root_freq / 440.0));
    }

    // 2. Set Intervals (Lookup from your library)
    // Assuming scale_library is accessible here
    if (scale_library.count(mode)) {
        track.scale_intervals = scale_library.at(mode);
    }
    // Optional: Handle "CHROMATIC" or reset if mode is invalid

    SDL_UnlockAudioStream(audio_stream);
}

//float SoundSystem::get_scale_freq(int degree) {
//    if (scale_intervals.empty()) return 0.0f;
//
//    int num_notes = scale_intervals.size();
//
//    // Calculate octave shift and index in the scale array
//    // Handling negative numbers correctly for C++ division/modulo
//    int octave_shift = (degree >= 0) ? (degree / num_notes) : ((degree - num_notes + 1) / num_notes);
//    int idx = (degree >= 0) ? (degree % num_notes) : ((degree % num_notes + num_notes) % num_notes);
//
//    int semitone_offset = scale_intervals[idx];
//    int target_midi = scale_root_midi + (octave_shift * 12) + semitone_offset;
//
//    // MIDI to Frequency: 440 * 2^((midi-69)/12)
//    return 440.0f * std::pow(2.0f, (target_midi - 69.0f) / 12.0f);
//}

void SoundSystem::play_note_pattern(const std::string& pattern) {
    SDL_LockAudioStream(audio_stream);

    // We assume the user wants to use their existing Voice settings.
    // The parser separates the string into layers.
    sequencer.parse_parallel(pattern);

    // Ensure all used layers are marked active and use "VOICE" mode
    for (int i = 0; i < sequencer.layers.size(); ++i) {
        if (!sequencer.layers[i].events.empty()) {
            sequencer.layers[i].active = true;
            // Force the layer to NOT override the track's sound
            // (assuming you added logic to treat SINE or a specific flag as "don't change synth")
            sequencer.layers[i].synth_type = Waveform::SINE; // Placeholder
        }
    }

    SDL_UnlockAudioStream(audio_stream);
}

#endif

#endif
