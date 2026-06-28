# Audio FX + the AI Tone Designer (jdBasic)

How to design guitar/synth tones in jdBasic: the effect building blocks, the
chain API, a small FFT "spectral ear", and a cookbook of named tones. This is
the reference an agent reads to turn a request like *"give me the Brian May tone
from Bohemian Rhapsody"* into a real, rendered sound.

**Build flags:** `SOUND` (the synth, for rendering test material) + `FX` (WAV I/O
+ the effect chain). All of this is offline (process a sample array) - no
real-time device needed, so it works headless.

```
build.bat HTTP GFX IMGUI NATIVEC SOUND FX            # Windows
SOUND=1 FX=1 HTTP=1 GFX=1 ... ./build.sh             # Mac/Linux
```

---

## The pipeline

```
source samples  ->  FX chain (FX.PROCESS)  ->  WAV.WRITE  ->  listen / spectral-ear  ->  adjust
```

Source samples = a recorded `WAV.READ`, or a synth render. `SOUND.RENDER(frames)`
returns **interleaved stereo** floats; for the (mono) FX chain take the left
channel:

```basic
SOUND.INIT
SOUND.VOICE 0, "SAW", 0.005, 0.4, 0.55, 0.6
SOUND.PLAY 0, "e3"
DIM stereo = SOUND.RENDER(44100)        ' 1 s
DIM mono = []
DIM j
FOR j = 0 TO LEN(stereo) - 1 STEP 2
    mono = APPEND(mono, stereo[j])
NEXT j
```

## The chain API

```basic
DIM ch = FX.NEW()                                   ' new empty chain -> handle
DIM ok = FX.ADD(ch, "drive", { "amount": 12 })      ' append a node (params optional)
ok     = FX.ADD(ch, "lowpass", { "cutoff": 3000 })
DIM wet = FX.PROCESS(ch, mono, 44100)               ' run the array through; rate optional
DIM x   = FX.FREE(ch)                               ' free (use the paren form!)
```

Nodes run **in the order added**. `FX.PROCESS` works on a mono float buffer.
Call `FX.FREE(ch)` in **paren form** - `FX.FREE ch` (statement form) mis-parses.

### Live tuning (the FX REPL)

While a chain is running (offline or under `MON.FX` live monitoring) you can
inspect and tweak it from the REPL:

```basic
PRINT FX.DUMP$(ch)                       ' list nodes: index, type, all params
DIM ok = FX.SET(ch, 3, "amount", 30)     ' set node 3's "amount" to 30 -> TRUE
```

`FX.SET(chain, nodeIndex, param$, value)` updates **one** parameter of an existing
node and returns `TRUE`, or `FALSE` if the chain/index/param is unknown. It only
writes parameters that already exist (every node type's full set is pre-populated
at `FX.ADD` time), so it is safe to call while the lock-free audio callback is
reading the chain. Build the chain, start monitoring, then turn knobs live -
play, listen, `FX.SET`, repeat. Use the **paren form** with `PRINT` or `DIM x =`;
the bare statement form with comma args mis-parses.

## The effect vocabulary

| type | params (defaults) | what it does |
|---|---|---|
| `gain` | `gain` (1.0) | linear level |
| `drive` | `amount` (5), `level` (0.7) | tanh waveshaper; `amount` ~0..20 = clean..fuzz; `level` tames output |
| `lowpass` | `cutoff` (2000), `q` (0.707) | RBJ biquad low-pass (darken / tame fizz) |
| `highpass` | `cutoff` (2000), `q` (0.707) | RBJ biquad high-pass (tighten lows; a treble-booster feel) |
| `delay` | `time_ms` (300), `feedback` (0.35), `mix` (0.3) | feedback delay / echo |
| `compressor` | `threshold` (0.5), `ratio` (4), `makeup` (1.0) | peak compressor (sustain, evenness) |
| `cabinet` | `ir` (WAV path), `level` (0.7), `mix` (1.0) | convolve with a speaker-cabinet impulse response = the realism jump |
| `chorus` | `rate` (0.8), `depth` (3), `delay` (14), `mix` (0.5) | LFO-modulated short delay; lush, doubled, shimmering |
| `flanger` | `rate` (0.3), `depth` (2), `delay` (1), `mix` (0.5), `feedback` (0.5) | short modulated delay + feedback; jet-plane sweep |
| `vibrato` | `rate` (5), `depth` (2), `delay` (14) | 100% wet pitch wobble (no dry) |
| `phaser` | `rate` (0.5), `depth` (2 oct), `base` (500), `mix` (0.5), `feedback` (0.3) | 4-stage swept allpass; classic phasing swoosh |
| `tremolo` | `rate` (5), `depth` (0.7) | amplitude modulation; pulsing volume |
| `fuzz` | `amount` (10), `level` (0.6) | asymmetric hard clip; harsher/buzzier than `drive` |
| `bitcrush` | `bits` (8), `downsample` (4), `mix` (1.0) | bit-depth + sample-rate reduction; lo-fi/digital grit |
| `octave` | `amount` (8), `level` (0.5), `mix` (0.7) | full-wave-rectifier octave-up fuzz (DC-blocked) |
| `autowah` | `base` (300), `range` (2200), `sensitivity` (8), `q` (4), `mix` (1.0) | envelope-following bandpass; funky auto-wah |
| `noisegate` | `threshold` (0.02) | silences signal below threshold; tames high-gain hiss |
| `eq` | `freq` (800), `q` (1), `gain` (6 dB) | single peaking band; cut/boost mids (negative `gain` = cut) |
| `reverb` | `roomsize` (0.7), `damp` (0.5), `mix` (0.3) | Freeverb-style room/hall ambience |

All nodes except `cabinet` are **real-time safe** (they allocate at most once), so
they run on the live `MON.FX` path too. `cabinet` is offline-only (it allocates
per block) and is skipped while monitoring.

### Cabinet IR
`cabinet` loads a `.wav` impulse response (left channel, unit-peak normalized)
and convolves it with the signal. A real cab IR makes "digital fizz" sound like
a miked amp. No IR file handy? Synthesize a usable one by band-passing a short
impulse:

```basic
DIM imp = []
DIM k
FOR k = 0 TO 399
    IF k = 0 THEN imp = APPEND(imp, 1.0) ELSE imp = APPEND(imp, 0.0)
NEXT k
DIM c = FX.NEW()
DIM ok = FX.ADD(c, "highpass", { "cutoff": 110 })
ok     = FX.ADD(c, "lowpass",  { "cutoff": 3600, "q": 1.2 })
DIM ir = FX.PROCESS(c, imp, 44100)
DIM d  = FX.FREE(c)
WAV.WRITE "tmp/cab_ir.wav", ir, 44100, 1
```

---

## The spectral "ear"

The agent can't hear, but it can read the spectrum. `FFT(window)` returns
`[N][2]` (real/imag pairs); band **energy** is `re*re + im*im`. Sum into
low/mid/high to see the tonal balance shift after a chain - then reason
("too much 2 kHz, lower the lowpass"):

```basic
FUNC BANDS(sig)                 ' -> { low, mid, high } in percent
    DIM N = 4096
    DIM st = (LEN(sig) - N) / 2 : IF st < 0 THEN st = 0
    DIM win = [] : DIM i
    FOR i = 0 TO N - 1
        IF st + i < LEN(sig) THEN win = APPEND(win, sig[st+i]) ELSE win = APPEND(win, 0.0)
    NEXT i
    DIM spec = FFT(win)
    DIM lo = 0.0 : DIM mi = 0.0 : DIM hi = 0.0
    FOR i = 1 TO N/2 - 1
        DIM en = spec[i][0]*spec[i][0] + spec[i][1]*spec[i][1]
        DIM hz = i * 44100 / N
        IF hz < 250 THEN
            lo = lo + en
        ELSEIF hz < 2500 THEN
            mi = mi + en
        ELSE
            hi = hi + en
        ENDIF
    NEXT i
    DIM tot = lo + mi + hi : IF tot <= 0 THEN tot = 1
    DIM r AS MAP
    r{"low"} = INT(lo/tot*100) : r{"mid"} = INT(mi/tot*100) : r{"high"} = INT(hi/tot*100)
    RETURN r
ENDFUNC
```

Working demo: `jdb/demos/audio/tone_designer.jdb`.

---

## How the AI designs a tone (the workflow)

1. **Translate the request to a recipe.** The agent already knows the tone:
   *Brian May, Bohemian Rhapsody* = Red Special -> treble booster -> cranked Vox
   AC30 (edge-of-breakup, mid-focused) -> cabinet -> harmonised tape delays ->
   bright, singing sustain.
2. **Map the recipe to primitives** (the table above) and emit `FX.ADD` calls.
3. **Render a short note/riff** through the chain -> `WAV.WRITE`.
4. **Read the spectral ear + let the human listen.** Adjust params, repeat.

## Tone cookbook (starting points - tune to taste)

**Brian May / Bohemian Rhapsody** (singing, mid-focused, harmonised echo):
```basic
FX.ADD ch, "compressor", { "threshold": 0.25, "ratio": 3, "makeup": 1.3 }
FX.ADD ch, "highpass",   { "cutoff": 600 }                 ' treble-booster feel
FX.ADD ch, "drive",      { "amount": 11, "level": 0.7 }    ' AC30 edge-of-breakup
FX.ADD ch, "cabinet",    { "ir": "tmp/cab_ir.wav", "level": 0.9 }
FX.ADD ch, "delay",      { "time_ms": 435, "feedback": 0.33, "mix": 0.28 }
```

**Clean chime** (sparkly, spacious):
```basic
FX.ADD ch, "compressor", { "threshold": 0.4, "ratio": 2, "makeup": 1.1 }
FX.ADD ch, "highpass",   { "cutoff": 120 }
FX.ADD ch, "delay",      { "time_ms": 380, "feedback": 0.25, "mix": 0.22 }
```

**Metal rhythm** (tight, scooped, palm-mute friendly):
```basic
FX.ADD ch, "highpass", { "cutoff": 90 }
FX.ADD ch, "drive",    { "amount": 18, "level": 0.7 }
FX.ADD ch, "lowpass",  { "cutoff": 4500, "q": 0.9 }
FX.ADD ch, "cabinet",  { "ir": "tmp/cab_ir.wav", "level": 0.95 }
```

**Lush clean (chorus + reverb)** (ambient, dreamy):
```basic
FX.ADD ch, "compressor", { "threshold": 0.4, "ratio": 2, "makeup": 1.1 }
FX.ADD ch, "chorus",     { "rate": 0.9, "depth": 3.5, "mix": 0.5 }
FX.ADD ch, "reverb",     { "roomsize": 0.85, "damp": 0.4, "mix": 0.35 }
```

**Funk rhythm (auto-wah)** (envelope-following quack):
```basic
FX.ADD ch, "compressor", { "threshold": 0.3, "ratio": 4, "makeup": 1.4 }
FX.ADD ch, "autowah",    { "base": 300, "range": 2400, "sensitivity": 10, "q": 5 }
FX.ADD ch, "drive",      { "amount": 4, "level": 0.8 }
```

**Lo-fi crunch (fuzz + bitcrush)** (gritty, broken):
```basic
FX.ADD ch, "fuzz",     { "amount": 16, "level": 0.6 }
FX.ADD ch, "bitcrush", { "bits": 6, "downsample": 6, "mix": 0.6 }
FX.ADD ch, "lowpass",  { "cutoff": 3000 }
```

(Use `DIM ok = FX.ADD(...)` paren form in real code; shown bare here for brevity.)

---

## See also
- `jdb/demos/audio/` - `synth_to_wav.jdb`, `fx_chain_demo.jdb`, `cabinet_demo.jdb`,
  `tone_designer.jdb`
- WAV: `WAV.WRITE/READ/INFO`. MIDI: `MIDI.PORTS/OPEN_OUT/OPEN_IN/SEND/NOTEON/
  NOTEOFF/CC/POLL/CLOSE` (see the `audio` jdTrakr project + `notes/audio_midi_plan.md`).
- Real-time monitoring (`MINIAUDIO` flag): `MON.DEVICES/START/STOP/GAIN/FX/RUNNING`
  run a live guitar/line input through an FX chain. Tune it live with `FX.SET` /
  `FX.DUMP$` (see `jdb/demos/audio/live_fx.jdb`).
- Planned: longer/partitioned convolution so `cabinet` (and full reverb IRs) can
  run on the live path too.
