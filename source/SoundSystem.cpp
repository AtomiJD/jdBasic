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
    {"NOISE", Waveform::NOISE}
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
    channels.resize(num_channels);

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

// --- The audio callback now mixes synth voices AND sample channels ---
void SoundSystem::audio_callback(void* userdata, SDL_AudioStream* stream, int additional_len, int total_len) {
    SoundSystem* self = static_cast<SoundSystem*>(userdata);

    // num_floats is total floats (Left + Right samples)
    int num_floats = additional_len / sizeof(float);
    if (num_floats == 0) return;
    // We process Frames (1 Frame = 1 Left Sample + 1 Right Sample)
    int num_frames = num_floats / 2;
    std::vector<float> buffer(num_floats);
    double seconds_per_sample = 1.0 / self->audio_spec.freq;

    
#ifdef SDLMIXER
    // --- SEQUENCER LOGIC START ---
    double phase_inc = seconds_per_sample * self->sequencer.cycles_per_second;

    self->sequencer.current_phase += phase_inc;
    if (self->sequencer.current_phase >= 1.0) {
        self->sequencer.current_phase -= 1.0;
        // Reset triggers for next cycle
        for (auto& layer : self->sequencer.layers) {
            for (auto& ev : layer.events) ev.triggered = false;
        }
    }

    // Check for events to trigger
    for (int L = 0; L < self->sequencer.layers.size(); ++L) {
        auto& layer = self->sequencer.layers[L];
        if (!layer.active) continue;

        for (auto& ev : layer.events) {
            // Trigger condition: We just passed the start time
            if (!ev.triggered && self->sequencer.current_phase >= ev.start_phase && self->sequencer.current_phase < (ev.start_phase + ev.duration)) {

                ev.triggered = true;
                float freq = 0.0f;
                // 1. Try treating token as a standard Note String (e.g., "c3")
                freq = NoteMap::get(ev.token);

                // 2. If that failed (returns 0), try treating it as a Scale Degree Number
                if (freq == 0.0f) {
                    try {
                        size_t parsed_len = 0;
                        int degree = std::stoi(ev.token, &parsed_len);
                        // Ensure the whole string was a number (avoids partial matches)
                        if (parsed_len == ev.token.length()) {
                            freq = self->get_scale_freq(degree);
                        }
                    }
                    catch (...) {
                        // Not a number either (maybe sample name or garbage)
                    }
                }

                if (freq > 0.0f) {
                    // It's a synth note
                    int track_idx = L % self->tracks.size();

                    if (layer.synth_type != Waveform::SINE) {
                        self->tracks[track_idx].waveform = layer.synth_type;
                    }

                    // Set properties
                    //self->tracks[track_idx].waveform = layer.synth_type;
                    self->tracks[track_idx].frequency = freq;
                    self->tracks[track_idx].phase = 0.0;
                    self->tracks[track_idx].adsr_state = ADSRState::ATTACK;
                    self->tracks[track_idx].envelope_level = 0.0;

                    // Adjust ADSR based on event duration
                    //double dur_sec = ev.duration / self->sequencer.cycles_per_second;
                    //self->tracks[track_idx].release_time = 0.1;
                    //self->tracks[track_idx].decay_time = dur_sec * 0.8;
                }
                else {
                    try {
                        int sample_id = std::stoi(ev.token);
                        // Sample triggering logic would go here
                    }
                    catch (...) {}
                }
            }
        }
    }
    // --- SEQUENCER LOGIC END ---
#endif
    for (int i = 0; i < num_frames; ++i) {
#ifdef SDLMIXER
        // --- SEQUENCER LOGIC START ---
        double phase_inc = seconds_per_sample * self->sequencer.cycles_per_second;

        self->sequencer.current_phase += phase_inc;
        if (self->sequencer.current_phase >= 1.0) {
            self->sequencer.current_phase -= 1.0;
            // Reset triggers for next cycle
            for (auto& layer : self->sequencer.layers) {
                for (auto& ev : layer.events) ev.triggered = false;
            }
        }

        // Check for events to trigger
        for (int L = 0; L < self->sequencer.layers.size(); ++L) {
            auto& layer = self->sequencer.layers[L];
            if (!layer.active) continue;

            for (auto& ev : layer.events) {
                // Trigger condition: We just passed the start time
                if (!ev.triggered && self->sequencer.current_phase >= ev.start_phase && self->sequencer.current_phase < (ev.start_phase + ev.duration)) {
                    ev.triggered = true;
                    float freq = 0.0f;
                    // 1. Try treating token as a standard Note String (e.g., "c3")
                    freq = NoteMap::get(ev.token);

                    // 2. If that failed (returns 0), try treating it as a Scale Degree Number
                    if (freq == 0.0f) {
                        try {
                            size_t parsed_len = 0;
                            int degree = std::stoi(ev.token, &parsed_len);
                            // Ensure the whole string was a number (avoids partial matches)
                            if (parsed_len == ev.token.length()) {
                                freq = self->get_scale_freq(degree);
                            }
                        }
                        catch (...) {
                            // Not a number either (maybe sample name or garbage)
                        }
                    }

                    if (freq > 0.0f) {
                        // It's a synth note
                        int track_idx = L % self->tracks.size();

                        if (layer.synth_type != Waveform::SINE) {
                            self->tracks[track_idx].waveform = layer.synth_type;
                        }

                        // Set properties
                        self->tracks[track_idx].seq_duration_remaining = ev.duration;
                        self->tracks[track_idx].controlled_by_sequencer = true;
                        self->tracks[track_idx].frequency = freq;
                        self->tracks[track_idx].phase = 0.0;
                        self->tracks[track_idx].adsr_state = ADSRState::ATTACK;
                        self->tracks[track_idx].envelope_level = 0.0;
                    }
                    else {
                        try {
                            int sample_id = std::stoi(ev.token);
                            // Sample triggering logic would go here
                        }
                        catch (...) {}
                    }
                }
            }
        }
        // --- 3. Gate Off Logic ---
        for (auto& track : self->tracks) {
            if (track.controlled_by_sequencer && track.adsr_state != ADSRState::OFF && track.adsr_state != ADSRState::RELEASE) {
                // Count down
                track.seq_duration_remaining -= phase_inc;
                // If time is up, let go of the key!
                if (track.seq_duration_remaining <= 0.0) {
                    track.adsr_state = ADSRState::RELEASE;
                    track.controlled_by_sequencer = false; // Reset flag
                }
            }
        }
        // --- SEQUENCER LOGIC END ---
#endif

        float mix_L = 0.0f;
        float mix_R = 0.0f;
        int active_sources = 0;

        // --- 1. Mix Synthesizer Voices ---
        for (auto& track : self->tracks) {
            if (track.adsr_state != ADSRState::OFF) {
                float sample = self->generate_sample(track);

                // Apply Gain
                sample *= track.gain;

                // Constant Power Panning (approximate for speed)
                // Linear: L = (1-pan), R = pan creates a volume dip in center.
                // Sqrt:   L = sqrt(1-pan), R = sqrt(pan) keeps power constant.
                float pan_r = track.pan;
                float pan_l = 1.0f - track.pan;

                // You can use sqrt for better quality, or keep it linear for speed
                // mix_L += sample * std::sqrt(pan_l);
                // mix_R += sample * std::sqrt(pan_r);

                // Simple Linear (easier on CPU)
                mix_L += sample * pan_l * 2.0f; // *2 to keep volume consistent with mono code at center
                mix_R += sample * pan_r * 2.0f;

                active_sources++;
            }
        }

        // --- 2. Mix Sound Effects ---
        // Note: load_sound converts samples to the device format (Stereo)
        // So loaded_samples are already Interleaved (L, R, L, R...)
        for (auto& channel : self->channels) {
            if (channel.is_active) {
                auto it = self->loaded_samples.find(channel.sample_id);
                if (it != self->loaded_samples.end()) {
                    SoundChunk& chunk = it->second;
                    // We read 2 floats (L, R) per frame
                    if (channel.position + sizeof(float) * 2 <= chunk.length) {
                        float* src = (float*)(chunk.buffer + (channel.position / sizeof(float))); // Pointer arith fix needed? 
                        // Actually chunk.buffer is float*, so just index
                        int float_idx = channel.position / sizeof(float);

                        float s_l = chunk.buffer[float_idx];
                        float s_r = chunk.buffer[float_idx + 1];

                        // Apply Channel Panning (optional, usually SFX are pre-panned)
                        // For now just pass through
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

        // Averaging
        if (active_sources > 1) {
            mix_L /= std::sqrt((float)active_sources); // Soft averaging
            mix_R /= std::sqrt((float)active_sources);
        }

        // --- 3. Stereo Delay ---
        if (self->delay_active) {
            size_t delay_frames = (size_t)((self->delay_time_ms / 1000.0) * self->audio_spec.freq);

            // Circular Buffer Logic (Frames)
            size_t read_pos = self->delay_head;
            if (read_pos < delay_frames) read_pos += (self->delay_buffer.size() / 2); // size is total floats
            read_pos -= delay_frames;

            // Read Stereo From Buffer
            float d_l = self->delay_buffer[read_pos * 2];
            float d_r = self->delay_buffer[read_pos * 2 + 1];

            // Feedback (Ping Pong or Stereo?)
            // Let's do Simple Stereo Delay: L feeds L, R feeds R
            self->delay_buffer[self->delay_head * 2] = mix_L + (d_l * self->delay_feedback);
            self->delay_buffer[self->delay_head * 2 + 1] = mix_R + (d_r * self->delay_feedback);

            // Advance Head
            self->delay_head++;
            if (self->delay_head >= (self->delay_buffer.size() / 2)) self->delay_head = 0;

            // Mix Wet
            mix_L += d_l * self->delay_mix;
            mix_R += d_r * self->delay_mix;
        }

        // --- 4. Distortion ---
        if (self->distortion_amount > 0.0) {
            mix_L = std::tanh(mix_L * (1.0 + self->distortion_amount));
            mix_R = std::tanh(mix_R * (1.0 + self->distortion_amount));
        }

        // --- 5. Visualization (Mono Sum) ---
#ifdef JD_IMGUI
        if (!self->vis_buffer.empty()) {
            self->vis_buffer[self->vis_head] = (mix_L + mix_R) * 0.5f;
            self->vis_head = (self->vis_head + 1) % self->vis_buffer.size();
        }
#endif

        // Write to output buffer (Interleaved)
        buffer[i * 2] = mix_L;
        buffer[i * 2 + 1] = mix_R;
    }
    SDL_PutAudioStreamData(stream, buffer.data(), additional_len);
}

void SoundSystem::reset() {
    if (!is_initialized) return;

    SDL_LockAudioStream(audio_stream);

    // 1. Silence Synth Tracks & Reset defaults
    for (auto& track : tracks) {
        track.adsr_state = ADSRState::OFF;
        track.envelope_level = 0.0;
        track.seq_duration_remaining = 0.0;
        track.controlled_by_sequencer = false;

        // Optional: Reset effects to clean state
        track.filter_cutoff = 20000.0;
        track.fm_amount = 0.0;
        track.crush_bits = 0.0;
        track.ring_mix = 0.0;
        track.gain = 1.0;
        track.pan = 0.5;
    }

    // 2. Stop SFX Channels
    for (auto& chan : channels) {
        chan.is_active = false;
    }

    // 3. Clear Sequencer
#ifdef SDLMIXER
    for (auto& layer : sequencer.layers) {
        layer.events.clear();
        layer.pattern_source = "";
        layer.active = false;
    }
    sequencer.current_phase = 0.0;
#endif

    // 4. Reset Global Effects
    delay_active = false;
    distortion_amount = 0.0;

    // Clear delay buffer to stop echoes immediately
    std::fill(delay_buffer.begin(), delay_buffer.end(), 0.0f);

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

float SoundSystem::generate_sample(Voice& voice) {
    float sample = 0.0f;
    double time_per_sample = 1.0 / audio_spec.freq;

    // --- 1. LFO Logic ---
        // Update LFO phase
    voice.lfo_phase += 2.0 * M_PI * voice.lfo_frequency * time_per_sample;
    if (voice.lfo_phase >= 2.0 * M_PI) voice.lfo_phase -= 2.0 * M_PI;

    // Calculate LFO output (-1.0 to 1.0)
    float lfo_val = sin(voice.lfo_phase);

    // Apply LFO to frequency (Vibrato)
    // Depth determines how many Hz to shift
    double modulated_frequency = voice.frequency + (lfo_val * voice.lfo_depth);

    // Prevent negative frequency
    if (modulated_frequency < 0) modulated_frequency = 0;

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

    // --- 3. Oscillator Generation (Use modulated_frequency) ---
    float raw_sample = 0.0f;

    // --- FM SYNTHESIS LOGIC ---
    // 1. Update Modulator Phase
    double mod_freq = modulated_frequency * voice.fm_ratio;
    voice.mod_phase += 2.0 * M_PI * mod_freq * time_per_sample;
    if (voice.mod_phase >= 2.0 * M_PI) voice.mod_phase -= 2.0 * M_PI;

    // 2. Calculate Modulation Value (Sine wave modulator)
    float mod_val = sin(voice.mod_phase) * voice.fm_amount;

    // 3. Calculate Effective Phase (Carrier Phase + Modulation)
    //    Phase Modulation is more stable than direct Frequency Modulation
    double effective_phase = voice.phase + mod_val;

    //    Wrap effective phase to 0..2PI range for wave functions
    effective_phase = fmod(effective_phase, 2.0 * M_PI);
    if (effective_phase < 0) effective_phase += 2.0 * M_PI;

    // --- OSCILLATOR GENERATION ---
    switch (voice.waveform) {
    case Waveform::SINE:
        raw_sample = sin(effective_phase);
        break;
    case Waveform::SQUARE:
        raw_sample = (sin(effective_phase) >= 0) ? 1.0f : -1.0f;
        break;
    case Waveform::SAWTOOTH:
        raw_sample = (effective_phase / M_PI) - 1.0;
        break;
    case Waveform::TRIANGLE:
        raw_sample = 2.0f * (fabs(effective_phase / M_PI - 1.0f) - 0.5f);
        break;
    case Waveform::NOISE:
        // FM doesn't affect noise, it's just random
        raw_sample = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        break;
    }

    // Update Carrier Phase (Standard increment)
    voice.phase += 2.0 * M_PI * modulated_frequency * time_per_sample;
    if (voice.phase >= 2.0 * M_PI) { voice.phase -= 2.0 * M_PI; }

    // --- 4. Filter Logic (Simple RC Low Pass) ---
        // alpha = dt / (RC + dt)
        // RC = 1 / (2 * pi * cutoff)
        // Simplification for digital one-pole:
        // output = prev_output + alpha * (input - prev_output)

    double rc = 1.0 / (2.0 * M_PI * voice.filter_cutoff);
    double alpha = time_per_sample / (rc + time_per_sample);

    // Clamp alpha for stability
    if (alpha > 1.0) alpha = 1.0;

    // Apply filter
    voice.filter_state = voice.filter_state + alpha * (raw_sample - voice.filter_state);

    // Use the filtered sample
    float final_sample = voice.filter_state;

    // --- RING MODULATOR ---
    if (voice.ring_mix > 0.0) {
        // 1. Advance Ring Modulator Phase
        voice.ring_phase += 2.0 * M_PI * voice.ring_freq * time_per_sample;
        if (voice.ring_phase >= 2.0 * M_PI) voice.ring_phase -= 2.0 * M_PI;

        // 2. Generate Carrier
        float carrier = sin(voice.ring_phase);

        // 3. Multiply (Ring Mod)
        float ring_signal = final_sample * carrier;

        // 4. Mix Dry/Wet
        final_sample = (final_sample * (1.0 - voice.ring_mix)) + (ring_signal * voice.ring_mix);
    }

    // --- BITCRUSHER ---
    if (voice.crush_bits > 0.0) {
        // 1. Sample Rate Reduction (Sample & Hold)
        voice.crush_counter += voice.crush_rate; // e.g. add 0.1 per real sample
        if (voice.crush_counter >= 1.0) {
            voice.crush_counter -= 1.0;

            // 2. Bit Depth Reduction
            // Steps = 2 ^ bits. e.g. 4 bits = 16 steps.
            float steps = pow(2.0f, (float)voice.crush_bits);
            // Quantize the sample to the nearest step
            voice.crush_last_sample = floor(final_sample * steps) / steps;
        }
        // Use the held/quantized sample
        final_sample = voice.crush_last_sample;
    }

    // Apply ADSR Volume
    return final_sample * voice.envelope_level;
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

void SoundSystem::set_scale(const std::string& root_note, const std::string& mode) {
    SDL_LockAudioStream(audio_stream);

    // 1. Convert Root Note String (e.g. "C3") to Frequency, then to MIDI Note
    // NoteMap is defined in SoundSystem.hpp (ensure it is accessible or copy the logic)
    float root_freq = NoteMap::get(root_note);
    if (root_freq > 0.0f) {
        // Frequency to MIDI formula: 69 + 12 * log2(freq / 440)
        scale_root_midi = std::round(69 + 12 * std::log2(root_freq / 440.0));
    }

    // 2. Set Intervals
    std::string m = mode;
    // Manual to_upper if needed, assuming input is uppercase from BASIC
    if (scale_library.count(m)) {
        scale_intervals = scale_library.at(m);
    }

    SDL_UnlockAudioStream(audio_stream);
}

float SoundSystem::get_scale_freq(int degree) {
    if (scale_intervals.empty()) return 0.0f;

    int num_notes = scale_intervals.size();

    // Calculate octave shift and index in the scale array
    // Handling negative numbers correctly for C++ division/modulo
    int octave_shift = (degree >= 0) ? (degree / num_notes) : ((degree - num_notes + 1) / num_notes);
    int idx = (degree >= 0) ? (degree % num_notes) : ((degree % num_notes + num_notes) % num_notes);

    int semitone_offset = scale_intervals[idx];
    int target_midi = scale_root_midi + (octave_shift * 12) + semitone_offset;

    // MIDI to Frequency: 440 * 2^((midi-69)/12)
    return 440.0f * std::pow(2.0f, (target_midi - 69.0f) / 12.0f);
}

#endif

#endif
