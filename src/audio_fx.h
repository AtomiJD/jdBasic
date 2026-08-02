#pragma once
// Audio FX / WAV I/O builtins. Real implementation only under the FX build
// flag (#ifdef FX); otherwise the register function is an empty no-op, so the
// SDL3/sound.cpp path is completely untouched.
class VM;
void register_audiofx_builtins(VM& vm);

// Realtime: one mono input block through an FX chain into an interleaved output
// of `outCh` channels. With the split section enabled the chain feeds branch A
// and B separately and pans them into the output, otherwise the main section is
// copied to every channel. No lock - the caller must not mutate the chain while
// it is in use (e.g. while monitoring). Cabinet (allocating convolution) is
// skipped on this path. Without FX the dry input is copied through.
void fx_process_chain_rt(int handle, const float* in, float* out,
                         unsigned frames, unsigned outCh, double rate);

// Largest block the realtime path can take. Blocks above this are passed dry.
const unsigned FX_RT_MAX_BLOCK = 4096;
