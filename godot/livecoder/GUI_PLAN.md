# jdBasic Live Coder - Studio GUI: phased plan

Turn the code-only livecoder into a full studio GUI. Single source of truth is a
`STUDIO` state model in jdBasic; code (F5/EXECUTE) builds it, the GUI reflects and
edits it (live `SOUND.*` + `GDX.CONNECT` callbacks), and it can regenerate the
code text / save patches as JSON.

UI construction: **hybrid** - static chrome and templates as hand-authored `.tscn`
text (editable in the Godot editor), instanced per track from jdBasic
(`GDX.INSTANTIATE`) and wired via `GDX.CONNECT`. All controls are Godot Control
nodes (the embed is headless+SOUND, no jdBasic SCREEN/IMGUI). Visualization draws
via `GDX.DRAW_*` on a CanvasItem.

Confirmed enablers: `GDX.CONNECT(h,"value_changed","sub")` forwards the real float
value to the handler; full per-track + global `SOUND.*` API; `SOUND.GET_WAVE` /
`SOUND.GET_BUS_WAVE` for visualization.

## STUDIO model
```
STUDIO = { bpm, master_gain, machine$,
  tracks[0..7] = { kind$, instr$, params{adsr,filter,lfo,fm,eq,unison,bitcrush,
                   ringmod,delay,reverb}, seq$, gain, pan, rev_send, dly_send, mute, solo },
  global_fx = { delay{}, reverb{}, compressor{}, distortion } }
```
Default code uses thin `LV.VOICE/LV.DRUM/LV.SEQ/LV.FX` wrappers around SQ that also
populate STUDIO, so the GUI knows each track's contents after every F5.

## Phases
- [ ] **0  Foundation** - STUDIO + LV.* wrappers; UI helper SUBs (UI.SLIDER/KNOB/DROPDOWN/TOGGLE/PANEL); .tscn shell; binding spike (one live slider).
- [ ] **1  Mixer strips** - per-track gain/pan/sends/mute/solo (live SOUND.GAIN/PAN/REVERBSEND/DELAYSEND) + level meter from GET_BUS_WAVE.
- [ ] **2  Synth voice editor** - ADSR, waveform, filter, LFO, FM, unison, EQ, bitcrush, ringmod, per-voice delay/reverb; per-section enable; per-track param copy; live SOUND.<fx>.
- [ ] **3  Sequence display + edit** - step grid per track from seq$; read-only then clickable -> SOUND.SEQ; beat-position highlight.
- [ ] **4  Drum GUI + machine dropdown + KPR77** - drum rack knobs; machine dropdown TR-808/KPR-77/CR-80; generalize LOAD_DRUM_SAMPLE(machine,code,knobs); add KPR77/CR-80 kits to sq_data.json.
- [ ] **5  Global FX panel** - master delay/reverb/compressor/distortion sliders + on/off.
- [ ] **6  Visualization** - oscilloscope (GET_WAVE) + FFT spectrum, drawn via GDX.DRAW_* with queue_redraw per _process; optional per-bus mini-scopes.
- [ ] **7  Persistence + round-trip** - regenerate CodeEdit text from STUDIO; patch save/load JSON (SQ.SAVE/LOAD).

## Also missing (proactive)
Transport bar (play/stop/pause, BPM, master gain, voice meter via SOUND.STATS);
scale/key selector (SOUND.SCALE); sidechain; instrument browser (assign to track);
preset/patch browser; piano keys to audition; pattern arrangement (stretch);
undo/redo, pattern copy/paste.

## Risks to clear early
Knob-callback throughput (throttle sample-reloads to drag_ended, live params per
tick); TR-808 sample-reload latency on knob turn (SFX_CACHE + kit preload); .tscn
layout authored as text then refined in the editor.
