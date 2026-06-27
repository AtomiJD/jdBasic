#pragma once
// Audio FX / WAV I/O builtins. Real implementation only under the FX build
// flag (#ifdef FX); otherwise the register function is an empty no-op, so the
// SDL3/sound.cpp path is completely untouched.
class VM;
void register_audiofx_builtins(VM& vm);

// Realtime: process one audio block through an FX chain, in place. No lock - the
// caller must not mutate the chain while it is in use (e.g. while monitoring).
// Cabinet (allocating convolution) is skipped on this path. No-op without FX.
void fx_process_chain_rt(int handle, float* buf, unsigned frames, double rate);
