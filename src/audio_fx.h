#pragma once
// Audio FX / WAV I/O builtins. Real implementation only under the FX build
// flag (#ifdef FX); otherwise the register function is an empty no-op, so the
// SDL3/sound.cpp path is completely untouched.
class VM;
void register_audiofx_builtins(VM& vm);
