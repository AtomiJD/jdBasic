#pragma once

class VM;
void register_sound_builtins(VM& vm);
void sound_shutdown();

// Pull-mode render: fill `out` with num_frames interleaved stereo samples
// from the live sequencer (embed hosts that own the audio device).
int sound_render(float* out, int num_frames);
