#pragma once

#include <cstddef>

class VM;
void register_sound_builtins(VM& vm);
void sound_shutdown();

// Pull-mode render: fill `out` with num_frames interleaved stereo samples
// from the live sequencer (embed hosts that own the audio device).
int sound_render(float* out, int num_frames);

// One raw MIDI message (status, data1, data2) for the synth to play.
// Thread-safe; a no-op in builds without sound.
void sound_midi_push(const unsigned char* msg, size_t len);
