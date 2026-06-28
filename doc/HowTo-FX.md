# HowTo: the jdBasic FX / Pedalboard project

A practical guide to the real-time audio toolchain: render tones offline, play
your guitar through an effect chain live, and drive the whole thing from an ImGui
pedalboard with presets, a tuner, recording, MIDI control and an AI tone designer.

- **Command reference:** `doc/languages.md` (WAV / FX / MIDI / MON sections).
- **Effect vocabulary + tone cookbook:** `doc/AudioFX.md`.
- **The app:** `jdb/demos/audio/fx_rack.jdb`.

---

## 1. Build flags

Everything here is opt-in and independent of the SDL3 `SOUND.*` synth:

| Flag | Gives you |
|---|---|
| `FX` | `WAV.*` I/O + the `FX.*` effect chain (offline) |
| `MIDI` | `MIDI.*` (RtMidi: keyboards, controllers, generative out) |
| `MINIAUDIO` | `MON.*` real-time input -> FX -> output monitoring |

For the full pedalboard you also need the GUI + synth flags:

```
build.bat HTTP GFX IMGUI NATIVEC SOUND FX MIDI MINIAUDIO        # Windows
FX=1 MIDI=1 MINIAUDIO=1 SOUND=1 HTTP=1 GFX=1 IMGUI=1 ./build.sh # Mac/Linux
```

`HTTP` is also needed for the AI tone designer (it calls an LLM over HTTP).

---

## 2. Offline: render a tone to a WAV

The synth (or a recorded `WAV.READ`) gives you samples; the chain processes them;
`WAV.WRITE` saves the result. `SOUND.RENDER` returns interleaved stereo, so take
the left channel for the mono chain.

```basic
SOUND.INIT
SOUND.VOICE 0, "SAW", 0.005, 0.4, 0.55, 0.6
SOUND.PLAY 0, "e3"
DIM stereo = SOUND.RENDER(48000)            ' 1 s
DIM mono = [], j
FOR j = 0 TO LEN(stereo) - 1 STEP 2
    mono = APPEND(mono, stereo[j])
NEXT j

DIM ch = FX.NEW()
DIM ok = FX.ADD(ch, "drive",  { "amount": 12, "level": 0.7 })
ok     = FX.ADD(ch, "lowpass", { "cutoff": 3200 })
ok     = FX.ADD(ch, "reverb",  { "mix": 0.25 })
DIM wet = FX.PROCESS(ch, mono, 48000)
DIM x   = FX.FREE(ch)
WAV.WRITE "tmp/out.wav", wet, 48000, 1
```

The node types and their parameters/ranges are in `doc/AudioFX.md`. Demos:
`jdb/demos/audio/fx_chain_demo.jdb`, `fx_pack_demo.jdb` (one WAV per effect),
`cabinet_demo.jdb`, `tone_designer.jdb`.

---

## 3. Live: play through the chain

Use **headphones** with a guitar/line input (a mic into speakers will howl). On an
iRig HD X: set it to **FX** mode and feed the amp from the iRig **headphone** out.

```basic
DIM ch = FX.NEW()
DIM ok = FX.ADD(ch, "drive", { "amount": 18, "level": 0.7 })
ok     = FX.ADD(ch, "reverb", { "mix": 0.2 })

DIM d = MON.DEVICES()                        ' { capture:[...], playback:[...] }
DIM m = MON.START(0, 0)                       ' or pick indices from d
ok = MON.GAIN(0.7)
ok = MON.FX(ch)                               ' route the monitor through the chain
SLEEP 60000
DIM s = MON.STOP()
```

`cabinet` is offline-only (it allocates per block); it is skipped on the live path.

### Tune the chain live (the FX REPL)

Start jdBasic with no script (REPL), build the chain + monitor, then turn knobs
while you play. `startfx.jdb` (Mac, gitignored) is a launcher that does the setup:

```basic
PRINT FX.DUMP$(ch)                 ' list node indices + params
PRINT FX.SET(ch, 0, "amount", 28)  ' node 0 drive: more gain
```

`FX.SET` updates one existing parameter and is race-safe against the live audio
callback. Adding/removing nodes mid-monitor is not: build a fresh chain and swap
with `MON.FX(newCh)` instead.

---

## 4. The pedalboard app (`fx_rack.jdb`)

A generic ImGui rack whose controls are generated from JSON. Run it:

```
./build/jdBasic jdb/demos/audio/fx_rack.jdb       # Windows
./build/jdbasic jdb/demos/audio/fx_rack.jdb       # Mac/Linux
```

Panels:

- **Monitor:** Start/Stop, master gain, Record/Stop (saves `rec_N.wav` next to the app).
- **Scope:** live waveform + FFT spectrum (toggle with the checkbox).
- **Tuner:** play a single string; shows note + cents off the dry input.
- **Pedalboard:** one collapsing header per effect, a slider per parameter, plus
  Enabled / Up / Down / Remove. "Add effect" appends a node.
- **Tap tempo:** tap the TAP button in time; a subdivision (1/4, 1/8, dotted 8th,
  triplet) sets the first delay node's time.
- **AI tone:** type a request (e.g. "warm jazz with light reverb"), pick a provider,
  Generate -> the AI returns a chain that is validated and loaded.
- **MIDI control:** pick the input device, Open, then press "L" next to any knob and
  move a CC to bind it (several knobs to several CCs). The "last msg" line shows
  activity so you can find the right port on a multi-port controller.
- **File menu:** Save/Load the rack to `fx_rack_user.json`, Reset. **Presets menu:**
  named tones that switch the whole chain.

### The JSON config files (next to the app)

| File | What |
|---|---|
| `fx_effects.json` | The schema: per-effect-type parameter list with min/max/def/label. Drives the sliders. Add an effect here when you add one in C++. |
| `fx_presets.json` | Named chains (Brian May, Clean Chime, ...). "Save Preset" writes back here. |
| `fx_ai.json` | AI providers: `claude` (default), `openai`, `strix` (local). Each has `url` / `model` / `key_env` / `kind`. The API key is read from the named environment variable. |

For the AI designer, the key env vars (e.g. `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`)
must be in the environment you launch the rack from (a login shell on macOS).

---

## 5. MIDI in / out

```basic
DIM p = MIDI.PORTS()                          ' { in:[...], out:[...] }
DIM h = MIDI.OPEN_OUT(0)
DIM ok = MIDI.NOTEON(h, 0, 60, 100)           ' middle C
SLEEP 500
ok = MIDI.NOTEOFF(h, 0, 60)
DIM c = MIDI.CLOSE(h)
```

For input, `MIDI.OPEN_IN(idx)` then drain with `MIDI.POLL(h)` (returns
`[[status, d1, d2], ...]`; CC messages have status 176-191). This is how the rack's
MIDI-learn maps a foot pedal/knob to a parameter.

---

## 6. Troubleshooting

- **No sound / no change live:** check `MON.RUNNING()`, the device indices from
  `MON.DEVICES()`, and that the amp is on the interface's headphone/return out (not
  a dry THRU). On the iRig, switch it to FX mode.
- **Preset too quiet:** clean tones need a `gain` stage (a guitar/interface dry
  signal is low); the shipped presets are level-matched.
- **Tuner does not catch a string:** the thin high e is the quietest; play a clear
  single note. `MON.PITCH` ignores below a low noise floor and rejects non-periodic
  input, so chords/very quiet notes read as 0.
- **MIDI controller silent:** a multi-port device (e.g. Oxygen 61) sends CCs on only
  one of its ports; open each in the dropdown and watch the "last msg" line.
- **AI "set ANTHROPIC_API_KEY ...":** the key is not in the launch environment; export
  it and start the rack from a fresh terminal.
