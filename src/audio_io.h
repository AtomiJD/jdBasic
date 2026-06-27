#pragma once
// Realtime audio device engine (miniaudio: CoreAudio / WASAPI / ALSA). Real
// implementation only under the MINIAUDIO build flag; otherwise a no-op.
class VM;
void register_audioio_builtins(VM& vm);
