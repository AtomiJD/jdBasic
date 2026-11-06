#pragma once

#include <cstddef>
#include <cstdint>

class VM;
void register_sound_builtins(VM& vm);
void sound_shutdown();

// Pull-mode render: fill `out` with num_frames interleaved stereo samples
// from the live sequencer (embed hosts that own the audio device).
int sound_render(float* out, int num_frames);

// One raw MIDI message (status, data1, data2) for the synth to play.
// Thread-safe; a no-op in builds without sound.
void sound_midi_push(const unsigned char* msg, size_t len);

// An outside audio device takes over the clock of a "PULL" synth: MIDI input
// is spread over its buffers and its latency is reported. FALSE when
// SOUND.INIT opened its own device.
bool sound_attach_output(bool on, int64_t out_latency_ns);

// Where the sequencer is heard: cycle `cycle` leaves the speaker at steady-clock
// time t_ns (nanoseconds) and the song moves at cps cycles per second.
// FALSE before SOUND.INIT.
bool sound_clock_anchor(double* cycle, int64_t* t_ns, double* cps);
