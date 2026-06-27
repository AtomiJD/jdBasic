#pragma once
// MIDI builtins (RtMidi: CoreMIDI / WinMM / ALSA). Real implementation only
// under the MIDI build flag; otherwise an empty no-op register function.
class VM;
void register_midi_builtins(VM& vm);
