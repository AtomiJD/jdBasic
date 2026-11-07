
# Sequencer Help

[Docs home](README.md) · [Tour](tour.md) · [Language reference](languages.md) · [Module library](../lib/README.md)

This document provides a detailed reference for the `SOUND` and `SFX` modules in jdBasic. It covers the live-coding sequencer, track-level sound design, global effects, visualization, and custom sample handling.

## 1. Core Sound System

* **`SOUND.INIT`**: Initializes the audio engine. Must be called before any other sound commands.
* **`SOUND.BPM value`**: Sets the tempo for the sequencer in Beats Per Minute.
* **`SOUND.RESET`**: Silences all audio, clears all sequencer layers, and resets effects to default.
* **`SOUND.SHUTDOWN`**: Safely stops the audio device and releases memory.

## 2. Live Coding Sequencer

The sequencer programs rhythmic patterns into 16 tracks (0-15).

* **`SOUND.SEQ track, pattern$, waveform$`**: Programs a rhythmic pattern into a specific track.
* **`pattern$`**: Space-separated tokens using mini-notation. Use `~` for rests and `[...]` for subdivisions. Transforms wrap a pattern: `"every(4, rev, c3 e3 g3)"`, `"jux(rev, ...)"`, `"arp(up, [c3,e3,g3])"`, `"off(0.125, add(7), ...)"` (see the Pattern Transforms table in languages.md).
* **`waveform$`**: Options include `"SINE"`, `"SQUARE"`, `"SAW"`, `"TRIANGLE"`, `"NOISE"`, or `"VOICE"`.

* **`SOUND.KIT track, kit`**: Makes the track a drum kit, so pattern atoms name sounds: `SOUND.KIT 0, "SYNTH"` uses the built-in drums, or `SOUND.KIT 0, "jdb/demos/sound/SynthesizerPercussion-SFZ-20220718/kit.txt"` then `SOUND.SEQ 0, "bd [~ bd] sd hh:1", "VOICE"`. `kit` is `"SYNTH"`, a kit file or a MAP name -> WAV path or SFX id.
* **`SOUND.ORBIT track, orbit`**: four effect buses (0-3), each with its own `SOUND.REVERB`/`SOUND.DELAY` (their optional last argument). **`SOUND.NUDGE track, ms`** starts a track late. Pattern values: `gain(v, p)`, `legato(v, p)`, `pan(v, p)`, `swing(n, p)`.
* **`SOUND.SCALE track, root$, scale_mode$`**: Maps integer pattern tokens (e.g., "0", "1") to a musical scale.

* **`SOUND.NOTE pattern$, looping`**: Plays pattern parts as melody (not part of the sequencer bar). pattern$ = "<c4 c4, e2 e4>" plays a melody on track 0 and the bass on track 1

### Pattern Syntax

The sequencer divides time into "cycles". You can arrange events within a cycle using space-separated tokens.

* **Notes**: Plays a musical note.
  * **Frequency**: `"c3"`, `"f#4"`
  * **Scale Degree**: `"0"`, `"1"`, `"-1"` (Requires `SOUND.SCALE` to be set).
  * **Rests** (`"~"`): A step of silence.
  * **Subdivision** (` "[... ...]"  `): Groups multiple steps into the timespan of a single step. This allows you to create fast rhythms (tuplets).
  * `"c4 c4"` = Two quarter notes (if cycle is 1 bar).
  * `"[c4 c4] c4"` = Two eighth notes followed by one quarter note.
  * `"c4 [c4 c4 c4]"` = One quarter note followed by eighth note triplets.
  * **Stack** (`a, b`): play together, `"[c3,e3,g3] ~"` is a chord.
  * **Chord symbols**: an upper-case root is a chord, `"<Am F C G>"`, `"Cmaj7 G7 Dm9 Am/E"`, voice-led so each chord takes the inversion nearest the one before. Roman numerals follow `SOUND.SCALE`: `"<i VI III VII>"`. Notes stay lower case (`c4`).
  * **One per cycle** (`<a b c>`): `"<c3 a2 f2 g2>"` walks a chord progression bar by bar.
  * **Speed** (`a*2`, `a/2`): `"c4*4"`, `"[c3 e3 g3 b3]/2"` spreads over two bars.
  * **Repeat / weight / hold** (`a!3`, `a@3`, `_`), **chance** (`a?0.3`), **random choice** (`a | b`).
  * **Euclidean** (`a(k,n,rot)`): `"c2(3,8)"` spreads 3 hits over 8 steps.
  * A playing layer switches to a new pattern at the next cycle; a switch queued with `SOUND.AT` applies on its own frame, also inside a cycle. `SOUND.EVENTS$(pattern$, cycles)` lists the events without playing them.
  * **`SOUND.AT cycle`** queues the commands after it for that cycle, applied on its exact frame: `SOUND.AT 72` then `SOUND.SEQ 0, "bd*4", "VOICE"`, then `SOUND.AT` to apply at once again. A whole arrangement can be sent ahead this way; `SOUND.PENDING()` counts what is still queued. `jdb/demos/sound/score.jdb` wraps it into section verbs. `SOUND.SEEK [cycle]` jumps the clock (or keeps it) and holds what is queued after it until `SOUND.AT` ends the score, so a score can be replaced while it plays (`SCORE.CLEAR` forgets the old one's log). `SOUND.MUTE track, flag` silences a track that keeps running; `SOUND.LEVELS()` gives each track's peak for meters.
  * Full table: languages.md, "Pattern Syntax".
  * **Progressions and hooks**: `jdb/demos/sound/melody.jdb` (`IMPORT MELODY`). `MELODY.PROG$("epic", "A minor")` gives `"Am F C G"` (also anthem, dark, pop, euphoric, house, deep, outrun); `MELODY.HOOK$(seed, prog$, key$, "x.x..x.x x.x.x...", 4)` writes a lead line over it, one bar per chord: chord tones on the beats, scale steps between, a leap and a step back, the form A A' A B, ending on the root or third. Same seed, same hook. `hook_trance.jdb --seed n --prog name` plays one.
  * **Whole tracks**: `jdb/demos/sound/songgen.jdb --genre trance|house|deep|acid|synthwave --seed n [--wav file]` picks key, tempo, progression and hook rhythm from the seed, builds bass, arp or stabs, choir and hook with MELODY and arranges it with SCORE in one of three forms per genre, with a variant of each part (breakdown, build, drop), a fill at each phrase end and the peak in the last drop. It prints the recipe and the form first, so a good seed can be kept; `--recipe` prints them and stops. `MELODY.BASS$`, `MELODY.ARP$` and `MELODY.STABS$` write the parts on their own. `MELODY.COUNTER$(seed, hook$, prog$, key$, octave)` writes an answer between the hook's notes (call and response), `MELODY.VARY$(hook$, prog$, key$, "dense"|"octave"|"ending")` a variation that is still the hook. songgen's last drop goes up a key (1 or 2 semitones, `add(n, ...)` on every tonal part), plays the varied hook and brings in the answer. `MELODY.BORROWED$(key$)` gives a breakdown progression with chords from the parallel key (IV iv bVI bVII in major, iv bVI bVII i in minor); songgen's breakdown plays it and, over 16 bars, goes home to the main progression for the second half. The drums are humanized: a share of the hat and percussion hits softer (`sometimesBy(0.3, gain(0.78), ...)`), a ghost snare before the clap, hats and percussion a few ms behind the kick (`SOUND.NUDGE`). Each track gets a title from the seed and the genre. The master ends in `SOUND.LIMITER` with a drive per genre, so every track lands near -12 LUFS integrated with peaks under -1 dB; `--wav` prints the loudness it measured (`SOUND.LOUDNESS`). `--live` opens a mixing desk instead of the show: a strip per track with a meter, mute and solo, the form as a row of parts (click or 1-9 to jump), the seed (Left/Right), the genre (G) and a reroll of the part that plays (R, new choices for that part only). Each change writes the song again while it plays, so the music does not stop. Synthwave (102-106 BPM, a minor key, often the `outrun` progression VI VII i i) drives a `synth_bass` in straight eighths (`MELODY.BASS$(prog$, "drive", seed)`), puts a gated snare on two and four (the kit snare plus a noise burst that stops dead), fills with toms or a gap, and glues its bus harder.

* **`SOUND.FILTER track, cutoff, [resonance], [mode$]`**: resonant `"LP"`/`"HP"`/`"BP"` when a resonance or mode is given.
* **`SOUND.FENV track, octaves, attack, decay, [sustain], [release]`**: filter envelope per note.
* **`SOUND.MOD track, param$, shape$, lo, hi, cycles`**: e.g. `SOUND.MOD 1, "CUTOFF", "SINE", 300, 3000, 8` sweeps over eight bars.

### Available Scale Modes

| Scale Mode | Musical Character |
| --- | --- |
| `"MAJOR"` | Bright, happy |
| `"MINOR"` | Sad, emotional |
| `"DORIAN"` | Jazzy, sophisticated |
| `"PHRYGIAN"` | Dark, exotic, Spanish-style |
| `"LYDIAN"` | Dreamy, sci-fi |
| `"MIXOLYDIAN"` | Bluesy, classic rock |
| `"PENT_MIN"` | Rock and Blues riffs |
| `"ARABIC"` | Middle-Eastern Hijaz flavor |

## 3. Track-Specific Sound Design

These commands allow you to design the unique "Voice" of each track.

* **`SOUND.VOICE track, wave$, a, d, s, r`**: Sets the waveform and ADSR envelope.
* **`SOUND.GAIN track, volume`**: Sets the track volume (default 1.0).
* **`SOUND.PAN track, pos`**: Sets stereo panning (0.0 Left, 0.5 Center, 1.0 Right).
* **`SOUND.FILTER track, cutoff_hz`**: Sets a per-track low-pass filter frequency.
* **`SOUND.REVERBSEND track, amount`**: Sets the signal level sent to the global Reverb (0.0-1.0).
* **`SOUND.DELAYSEND track, amount`**: Sets the signal level sent to the global Delay (0.0-1.0).
* **`SOUND.SIDECHAIN target, source, amount`**: Ducks the volume of the `target` when the `source` plays.

### Per-Track Modulation

These shape the timbre of a track's voice and must be set before the pattern plays.

* **`SOUND.EQ track, low, mid, high`**: 3-band equalizer gains (1.0 = flat).
* **`SOUND.LFO track, freq, depth`**: Vibrato: pitch modulation at `freq` Hz with `depth`.
* **`SOUND.FM track, amount, ratio`**: Frequency modulation for metallic / bell tones.
* **`SOUND.UNISON track, voices, detune, spread`**: Stacks `voices` (1-16) detuned copies for a super-saw; `detune` 0.0-1.0, `spread` 0.0-1.0 stereo.
* **`SOUND.BITCRUSH track, bits, rate`**: Lo-fi effect; `bits` 1-16 resolution, `rate` 0.0-1.0 sample-rate reduction.
* **`SOUND.RINGMOD track, freq, mix`**: Ring modulation for robotic / sci-fi tones.
* **`SOUND.OSC2 track, wave$, [semitones], [mix], [cents]`**: A second oscillator per voice, e.g. a square an octave below a saw.
* **Patches**: `jdb/demos/sound/patch.jdb` (`IMPORT PATCH`), `PATCH.APPLY track, "neon_pluck"` sets a whole sound in one call. The Neon sounds (`neon_pluck` for arps, `neon_lead` for hooks, `neon_soft` for lines behind the hook, `neon_voice` for the answer to a hook) are the jdBasic signature; also `supersaw`, `pad`, `choir`, `organ`, `fm_keys`, `vox_chop`, `bass_roll`, `bass_house`, `sub`, `acid`, `synth_bass`. `PATCH.CUTOFF(name$)`, `PATCH.RESONANCE(name$)` and `PATCH.FILTER_TYPE$(name$)` return the filter a patch sets, for a sweep to return to.
* **`SOUND.FORMANT track, vowel$, mix, [breath]`**: Vowel formants (`"ah"`, `"eh"`, `"ee"`, `"oh"`, `"oo"` or 0-4) for "ooh"/"aah" pads and choirs; `breath` adds air. `SOUND.MOD track, "VOWEL", ...` morphs between vowels.
* **`SOUND.GLIDE track, ms, [mode$]`**: Portamento; `"SLIDE"` glides only into notes marked `^` (`"c2 c3^ c2"`), the 303 way.
* **`SOUND.CHORUS track, mix, [rate], [depth]`** and **`SOUND.WIDTH track, width`**: Stereo chorus and width on the track's sum.
* `SOUND.MOD track, "PITCH", "SAW", 0, 12, 8` rises an octave over eight bars: a riser on an effect track. On a melody or arpeggio the sweep pulls every note off the scale; step it with `add(12, ...)` instead.

## 4. PCM Samples and SFX

jdBasic can load and play WAV files.

### Global SFX and Music

* **`SFX.LOAD id, "filepath.wav"`**: Loads a WAV file into memory slot `id`.
* **`SFX.PLAY id`**: Plays a loaded WAV file once (fire-and-forget).
* **`MUSIC.PLAY id, [loop_bool]`**: Plays a loaded WAV file as background music.
* **`MUSIC.STOP`**: Immediately stops the background music.

### Samples as Sequencer Instruments

You can use a sample, or a recording of your own, as a sound source for a track and play it from patterns or from a keyboard (section 8).

* **`SOUND.SAMPLE(track, source, [base], [loop], [rate]) -> seconds`**: `source` is a loaded `SFX` id, a WAV path, or an array of mono samples at `rate` (default 44100). `base` is the note the sample sounds at, a name (`"C3"`) or a MIDI number; each key plays it faster or slower from there, read between frames by linear interpolation. `loop` repeats it while the note is held.
* **`SOUND.PITCHOF(samples, [rate]) -> Hz`**: finds the root of a recording, so a guitar note played into a microphone lands on the right keys: `SOUND.SAMPLE 1, take, 69 + 12 * LOG(hz / 440) / LOG(2), FALSE, 48000`.
* **`SOUND.SAMPLELOOP track, start_s, end_s, [xfade_s]`**: a held note loops over a region with a crossfade at the seam, so a short recording sustains.
* **`SOUND.GRAIN track, size_ms, [density], [spray]`**: granular playback. The key sets the pitch, the `GRAINPOS` parameter (0..1) the place in the recording, so a knob or an LFO on `GRAINPOS` freezes, scrubs or stretches it.

```basic
DIM secs = SOUND.SAMPLE(1, "guitar_a.wav", "A2")
SOUND.SAMPLELOOP 1, 0.4, 1.2, 0.05
SOUND.GRAIN 1, 90, 40, 0.03
SOUND.CCMAP 22, 1, "GRAINPOS", 0, 1      ' knob 1 scrubs through the note
```

## 5. Global Effects (Master Bus)

* **`SOUND.REVERB size, damp, width, wet`**: Configures the global reverb room.
* **`SOUND.DELAY active, time, feedback, mix, [orbit], [pingpong]`**: Configures the global stereo delay; `time` in ms or a note value locked to the tempo (`"1/8."`, `"1/4"`, `"1/8t"`), `pingpong` bounces the repeats.
* **`SOUND.COMPRESSOR thresh, ratio, attack, release, gain`**: Master dynamics; `thresh` 0.0-1.0, `ratio` 1.0-20.0, `attack`/`release` in ms.
* **`SOUND.LIMITER [ceiling_db, [drive_db], [release_ms]]`**: Lookahead peak limiter at the end of the master: `drive_db` lifts the mix into it, no sample passes `ceiling_db`; no argument switches it off.
* **`SOUND.LOUDNESS([reset])`**: Map with the output's loudness in LUFS (`INTEGRATED`, `MOMENTARY`, `SHORT`, `SHORT_MAX`), `PEAK` in dBFS and the limiter's `REDUCTION`/`REDUCTION_MAX`.
* **`SOUND.MASTER mode$, [headroom]`**: `"AUTO"` (default) divides the mix by sqrt(voices); `"FIXED"` scales each voice by `headroom` (0.5), so layers keep their level. Output soft-clips above 0.8.
* **`SOUND.DISTORTION amount`**: Applies master saturation/overdrive.

## 6. Visualization

* **`SOUND.GET_WAVE()`**: Returns an array of the master mix for oscilloscopes.
* **`SOUND.GET_BUS_WAVE(id)`**: Returns the "wet-only" signal from a bus (`0` for Reverb, `1` for Delay).

## 7. Embedded / Pull-Mode Rendering

Normally `SOUND.INIT` opens an SDL audio device and a background thread plays
the mix. When jdBasic is built **without** a device (the `SOUND` build flag,
`/DSOUND_DSP`, used by embed hosts such as Godot), the same sequencer runs but
nothing is sent to the speakers automatically. The host pulls the mix instead:

* **`SOUND.RENDER(frames) -> array`**: Renders the next `frames` stereo frames
  of the live sequencer and returns them as a flat `[L0, R0, L1, R1, ...]`
  array (length `frames * 2`). Advances the sequencer by `frames` at 44100 Hz.

The host feeds those samples to its own audio engine each frame. In Godot:

```basic
' once: an AudioStreamGenerator at 44100 Hz on an AudioStreamPlayer
DIM avail = GDX.CALL(playback, "get_frames_available")
DIM buf = SOUND.RENDER(avail)
GDX.CALL(playback, "push_buffer", GDX.PACKED_VEC2(buf))
```

All design commands (`SOUND.VOICE` / `SEQ` / `NOTE` / `FM` / `UNISON` / `REVERB`
/ ...) work identically in pull mode; only `SFX.LOAD` and `SOUND.PLAYBUFFER`
(which need the SDL device and WAV decoder) require a `GFX` build.

## 8. Playing the Synth from a MIDI Keyboard

The sequencer's tracks are also a playable synthesizer. A MIDI keyboard (or
pads, or any controller) reaches them through the `MIDI` build flag. Notes go
straight to the audio thread, so the script loop adds no latency, and the
same tracks keep playing their `SOUND.SEQ` patterns underneath.

### Ports and routing

* **`MIDI.PORTS()`**: `{in: [names], out: [names]}`, the connected ports.
* **`MIDI.OPEN_IN(index)`** / **`MIDI.OPEN_OUT(index)`**: a handle, or -1.
* **`MIDI.ROUTE(handle, [on], [channel])`**: the port plays the synth directly. `on` is `TRUE`, `FALSE`, or `"CC"` (controllers only, for a port whose pads must not play notes); `channel` moves every message to that channel, so a second port can play its own zone. `MIDI.POLL` still sees every message, for recording or for buttons the script handles itself.
* **`MIDI.POLL(handle)`**: the messages since the last call, `[[status, data1, data2], ...]`. **`MIDI.SEND(handle, status, [d1], [d2])`** sends one. **`MIDI.CLOSE handle`**.
* **`SOUND.MIDIIN status, [d1], [d2]`**: plays one message as if it came from a routed port, for tests and for scripts that filter first.
* `jdb/demos/audio/midi_sniff.jdb` prints what each key, pad and knob of a controller sends.

### Zones: which keys play which track

* **`SOUND.ZONE track, [lo], [hi], [channel], [transpose], [options]`**: keys `lo`..`hi` on `channel` (0 = any) play `track`, shifted by `transpose`. Zones may overlap (layers). `SOUND.ZONE -1` clears them; without zones every key plays track 0.
* Options (a map): `vel_lo`/`vel_hi` (velocity split), `bend` (semitones), `sustain` (pedal), `mono`, `legato` (mono glide without restarting the envelope), `vel_cutoff` (softer keys close the filter), `mod_cutoff` and `mod_vibrato` (what the mod wheel does).
* **Chords**: `chord` makes one key play several notes: semitones (`[0, 7, 12]`), a name (`"maj"`, `"min"`, `"dim"`, `"aug"`, `"sus2"`, `"sus4"`, `"7"`, `"maj7"`, `"min7"`, `"power"`, `"oct"`), or `"triad"` / `"seventh"` / `"ninth"`, stacked from the track scale, so each white key in C major gives its own chord (D gives D minor, G gives G7).
* **Scale lock**: `scale: TRUE` moves every note to the nearest note of the track's `SOUND.SCALE`.
* **Arpeggiator**: `arp` plays the held keys (and their chords) one after another: `"up"`, `"down"`, `"updown"`, `"downup"`, `"random"`, `"played"`. `arp_rate` is steps per bar or a note value (`"1/16"`, `"1/8T"`), `arp_octaves` 1-4, `arp_gate` the share of each step that sounds, `arp_latch` keeps it running after the keys go up. The steps run on the sequencer clock, so the arp stays on the beat of the patterns and of `MIDI.CLOCK`.
* **`SOUND.ZONESET index, options`**: changes a zone (0-based, in the order made) while it plays; held keys and a running arp carry on.
* A zone on a kit track (`SOUND.KIT`) plays drums: each note hits its General MIDI sound (36 bd, 38 sd, 42 hh ...). **`SOUND.PADMAP track, note, name$`** names other notes.
* **`SOUND.LASTNOTE()`**: the last key (`note`, `velocity`, `channel`, `count`), for learning a split point.

```basic
SOUND.INIT
SOUND.KIT 2, "SYNTH"
SOUND.SCALE 1, "A3", "MINOR"
SOUND.ZONE 0, 0, 47, 0, -12, {"legato": TRUE}                    ' mono bass
SOUND.ZONE 1, 48, 127, 0, 0, {"chord": "triad", "arp": "updown", "arp_rate": "1/16"}
SOUND.ZONE 2, 0, 127, 10                                         ' pads on channel 10
DIM keys = MIDI.OPEN_IN(0)
MIDI.ROUTE(keys)
```

### Knobs, faders and modulation

* **`SOUND.CCMAP cc, track, param$, lo, hi, [curve$], [channel], [pickup]`**: a controller sets a parameter (`"EXP"` for cutoff and times). The value glides in about 10 ms; with `pickup` a knob takes over only once it passes the present value. `SOUND.CCMAP -1` clears.
* **`SOUND.PARAM(track, param$, [value])`**: reads or sets `CUTOFF`, `RESONANCE`, `GAIN`, `PAN`, `DETUNE`, `FM`, `CRUSH`, `REVERB`, `DELAY`, `FENV`, `VOWEL`, `PITCH`, `ATTACK`, `DECAY`, `SUSTAIN`, `RELEASE`, `LFORATE`, `LFODEPTH`, `WTPOS`.
* **`SOUND.MATRIX source$, track, param$, amount`**: a modulation slot. Sources: `LFO1`, `LFO2`, `MODWHEEL`, `AFTERTOUCH`, `VELOCITY`, `NOTE`, `RANDOM`, `CC0`-`CC127`. Knobs and slots stack on the same parameter. **`SOUND.MODLFO n, hz, [shape$]`** sets the two LFOs, **`SOUND.MODSRC(source$)`** reads a source for a meter.
* **Wavetables**: **`SOUND.WAVETABLE track, [frames]`** gives a track a band-limited wavetable oscillator, built in (`"BASIC"`, `"PWM"`, `"HARMONIC"`, `"VOCAL"`) or drawn as jdBasic arrays; `WTPOS` (0..1) scans through it, by knob, LFO or `SOUND.MOD`.
* **Patches**: `lib/synthpatch.jdb` (`IMPORT SYNTHPATCH`) loads a bank of sounds from JSON (`LOAD_BANK`, `FIND`), sets one in a call (`APPLY`), reads back what the knobs did (`CAPTURE`) and blends two (`MORPH`). See `lib/synthpatch_lib_readme.md`.

### Voices: singing and vocoder

* **`SOUND.SING track, lyric$`** on a keyboard track: each key sings the next syllable, the vowel lasts while the key is held, the closing consonants come when it goes up. A `chord` zone sings one syllable on all its notes (a choir), an `arp` zone one syllable a step.
* **`SOUND.VOCODER track, [source], [options]`**: each note says what a recording says, at the pitch of the key: speech as the modulator, the track's oscillator as the carrier, 4-32 bands. `source` is an array (a take recorded with `WAV.RECSTART`) or a WAV path; `{"loop": TRUE}` repeats it while the key is held. Play a chord on a saw track for the classic robot choir.
* The sampler takes of section 4 (`SOUND.SAMPLE`, `SOUND.GRAIN`) play from the keys the same way.

### Clock and latency

* **`MIDI.CLOCK(out, mode)`**: sends MIDI clock (24 per beat) in step with the sequencer as it leaves the speaker; `"START"` also sends Start on the next bar line, `"STOP"` sends Stop. A hardware arpeggiator or drum machine set to external sync follows `SOUND.BPM`.
* **`MIDI.CLOCKIN(in)`**: the clock a port receives, `{bpm, running, ticks}`, to follow another device's tempo.
* **`SOUND.CYCLE()`**: the clock in bars, for a looper that records on the grid.
* **`MON.SYNTH([frames], [device], [exclusive])`** after `SOUND.INIT "PULL"` plays the synth on a low-latency device (`MINIAUDIO` flag): on Windows exclusive mode brings key-to-speaker under 10 ms. **`SOUND.MIDISTATS()`** measures it.

### The Oxygen demo

`jdb/demos/audio/oxygen_synth.jdb [seconds] [fast] [panel]` turns an M-Audio
Oxygen 61 (or any keyboard, ports in `oxygen61.json`) into a synth and looper:
split bass and lead, drums on the pads, knobs and faders mapped, a patch bank
with morphing, a modulation matrix, the arp and chord panel, a looper that
records pads and keys in 1-8 bars, and MIDI clock out. `oxygen_live.jdb`
next to it runs again each time it is saved, so the sound and the patterns
change while the keyboard plays. `patch_designer.jdb "glassy bell pad"` lets
an LLM design a patch, renders and measures each try and saves the result
into the demo's bank.

---

## Comprehensive Command Table

| Category | Command | Parameters | Description |
| --- | --- | --- | --- |
| **System** | `SOUND.INIT` | none | Start audio engine |
|  | `SOUND.BPM` | `bpm` | Set sequencer speed |
|  | `SOUND.RESET` | none | Clear all sound/patterns |
| **Sequencer** | `SOUND.SEQ` | `track, pattern$, wave$` | Define rhythmic pattern |
|  | `SOUND.SCALE` | `track, root$, mode$` | Quantize pattern notes to scale |
|  | `SOUND.AT` | `[cycle]` | Queue the following commands for a cycle |
|  | `SOUND.PENDING` | none | Commands still queued |
| **Track FX** | `SOUND.GAIN` | `track, volume` | Per-track volume |
|  | `SOUND.PAN` | `track, position` | Per-track stereo position |
|  | `SOUND.FILTER` | `track, frequency` | Per-track low-pass filter |
|  | `SOUND.EQ` | `track, low, mid, high` | 3-band equalizer |
|  | `SOUND.LFO` | `track, freq, depth` | Vibrato (pitch modulation) |
|  | `SOUND.FM` | `track, amount, ratio` | Frequency modulation |
|  | `SOUND.UNISON` | `track, voices, detune, spread` | Super-saw voice stacking |
|  | `SOUND.BITCRUSH` | `track, bits, rate` | Lo-fi bit/rate reduction |
|  | `SOUND.RINGMOD` | `track, freq, mix` | Ring modulation |
|  | `SOUND.REVERBSEND` | `track, amount` | Reverb send level |
|  | `SOUND.DELAYSEND` | `track, amount` | Delay send level |
|  | `SOUND.SIDECHAIN` | `target, source, amt` | Ducking (Drums vs Bass) |
| **Samples** | `SFX.LOAD` | `id, "file.wav"` | Load WAV into memory |
|  | `SOUND.SAMPLE` | `track, id, base$, loop` | Use WAV as track instrument |
| **Global FX** | `SOUND.REVERB` | `size, damp, width, wet` | Set master reverb room |
|  | `SOUND.DELAY` | `on, time, feed, mix` | Set master delay settings |
|  | `SOUND.COMPRESSOR` | `thresh, ratio, atk, rel, gain` | Master dynamics |
|  | `SOUND.LIMITER` | `[ceil_db, drive_db, rel]` | Peak limiter, master end |
|  | `SOUND.MASTER` | `mode$, [headroom]` | AUTO or FIXED mix level |
|  | `SOUND.DISTORTION` | `amount` | Master saturation |
| **MIDI** | `MIDI.OPEN_IN` / `MIDI.ROUTE` | `index` / `handle, [on], [ch]` | A keyboard plays the synth directly |
|  | `SOUND.ZONE` | `track, [lo], [hi], [ch], [transpose], [opts]` | Key ranges, chords, scale lock, arpeggiator |
|  | `SOUND.ZONESET` | `index, opts` | Change a zone while it plays |
|  | `SOUND.CCMAP` | `cc, track, param$, lo, hi, ...` | Knob or fader to a parameter |
|  | `SOUND.MATRIX` | `source$, track, param$, amount` | Modulation slot |
|  | `SOUND.WAVETABLE` | `track, [frames]` | Wavetable oscillator, `WTPOS` scans |
|  | `MIDI.CLOCK` / `MIDI.CLOCKIN` | `out, mode` / `in` | Clock out in step, clock in |
| **Analysis** | `SOUND.GET_WAVE` | none | Get master waveform array |
|  | `SOUND.RENDER` | `frames` | Pull stereo samples (embed/no-device) |
|  | `SOUND.GET_BUS_WAVE` | `bus_id` | Get wet-only waveform |
|  | `SOUND.LOUDNESS` | `[reset]` | LUFS, peak, limiter reduction |

---

## 9. Examples

### Sidechain Pumping

```basic
SOUND.INIT
SOUND.BPM 124

' Track 0: Kick
SOUND.VOICE 0, "SQUARE", 0.01, 0.1, 0.0, 0.1
SOUND.SEQ 0, "c2 ~ c2 ~", "VOICE"

' Track 1: Bass Pad
SOUND.VOICE 1, "SAW", 0.2, 0.5, 0.4, 0.5
SOUND.SEQ 1, "c3 c3 c3 c3", "VOICE"

' Apply Ducking: Synth pumps when kick hits
SOUND.SIDECHAIN 1, 0, 0.8

```

### Using Custom Samples

```basic
' Load a drum loop and play it as an instrument
SFX.LOAD 10, "samples/amen_break.wav"
SOUND.SAMPLE 2, 10, "C3", TRUE
SOUND.SEQ 2, "c3 ~ ~ ~", "VOICE"

```

## Instrument Presets

### Plucked & Percussive Leads

*Best for arpeggios and fast melodies.*

1.  **Trance Pluck** (Bright, snappy, classic EDM)
    ```basic
    SOUND.VOICE 0, "SAW", 0.001, 0.3, 0.0, 0.2
    SOUND.FILTER 0, 2000  ' Open filter for brightness
    ```
2.  **Soft Bell** (Pure tone, long fade out)
    ```basic
    SOUND.VOICE 0, "SINE", 0.001, 1.5, 0.0, 1.5
    SOUND.FILTER 0, 20000 ' Full open
    ```
3.  **Marimba / Woodblock** (Hollow, very short)
    ```basic
    SOUND.VOICE 0, "TRIANGLE", 0.001, 0.1, 0.0, 0.05
    SOUND.FILTER 0, 800   ' Muffled to sound "woody"
    ```
4.  **Electric Piano** (Classic keys sound)
    ```basic
    SOUND.VOICE 0, "TRIANGLE", 0.01, 0.8, 0.0, 0.4
    SOUND.LFO 0, 2, 2     ' Very subtle tremolo/vibrato
    ```
5.  **Koto / Sitar-ish** (Bright, metallic pluck)
    ```basic
    SOUND.VOICE 0, "SAW", 0.001, 0.2, 0.0, 0.1
    SOUND.FILTER 0, 5000
    SOUND.LFO 0, 15, 5    ' Fast wobble adds metallic texture
    ```

### Sustained Leads

*Best for main melodies and solos.*

6.  **Chiptune / 8-Bit** (Nintendo-style lead)
    ```basic
    SOUND.VOICE 0, "SQUARE", 0.01, 0.1, 0.8, 0.1
    SOUND.FILTER 0, 20000 ' No filter for raw digital sound
    ```
7.  **Violin / Strings** (Slow attack, vibrato)
    ```basic
    SOUND.VOICE 0, "SAW", 0.3, 0.2, 0.8, 0.5
    SOUND.FILTER 0, 1500  ' Remove harsh highs
    SOUND.LFO 0, 6, 3     ' Classic vibrato (6Hz)
    ```
8.  **Flute** (Hollow, soft attack)
    ```basic
    SOUND.VOICE 0, "TRIANGLE", 0.1, 0.2, 0.9, 0.2
    SOUND.FILTER 0, 1200  ' Smooth out the tone
    SOUND.LFO 0, 4, 2     ' Gentle breath wobble
    ```
9.  **Super Saw Lead** (The "Anthem" sound)
    ```basic
    SOUND.VOICE 0, "SAW", 0.01, 0.4, 0.6, 0.4
    SOUND.LFO 0, 3, 5     ' Detuning effect via LFO
    ```
10. **Cheap Organ** (Hollow, constant volume)
    ```basic
    SOUND.VOICE 0, "SQUARE", 0.05, 0.0, 1.0, 0.05
    SOUND.FILTER 0, 3000
    ```
11. **Theremin / Whistle** (Pure sliding sine)
    ```basic
    SOUND.VOICE 0, "SINE", 0.1, 0.1, 1.0, 0.2
    SOUND.LFO 0, 6, 5     ' Heavy vibrato is key
    ```
12. **Brass / Horn** (Punchy start, sustained)
    ```basic
    SOUND.VOICE 0, "SAW", 0.08, 0.2, 0.7, 0.3
    SOUND.FILTER 0, 1000  ' Darker tone
    ```

### Bass Leads

*Use these on lower octaves (C1 - C3).*

13. **Reese Bass** (Moving, dark bass)
    ```basic
    SOUND.VOICE 0, "SAW", 0.05, 0.5, 0.8, 0.5
    SOUND.FILTER 0, 400   ' Low pass is essential
    SOUND.LFO 0, 1, 3     ' Slow movement
    ```
14. **Slap Bass** (Fast attack pluck)
    ```basic
    SOUND.VOICE 0, "SQUARE", 0.001, 0.2, 0.0, 0.1
    SOUND.FILTER 0, 800
    ```
15. **Sub Bass** (Pure low end, felt more than heard)
    ```basic
    SOUND.VOICE 0, "SINE", 0.05, 0.2, 1.0, 0.5
    SOUND.FILTER 0, 200
    ```

### FX & Experimental

*Good for transitions or weird textures.*

16. **Sci-Fi Siren** (Pitch sweeping up and down)
    ```basic
    SOUND.VOICE 0, "SINE", 0.5, 0.5, 1.0, 1.0
    SOUND.LFO 0, 0.5, 200 ' LFO Depth of 200 creates huge pitch sweep
    ```
17. **Telephone / Lo-Fi** (Filtered Square)
    ```basic
    SOUND.VOICE 0, "SQUARE", 0.01, 0.1, 0.8, 0.1
    SOUND.FILTER 0, 800   ' Filters out high fidelity
    ```
18. **Metallic FM** (Fast LFO creates new harmonics)
    ```basic
    SOUND.VOICE 0, "SINE", 0.01, 0.5, 0.0, 0.5
    SOUND.LFO 0, 50, 100  ' 50Hz LFO acts as a modulator oscillator
    ```
19. **Wind / Ocean** (Atmospheric Noise)
    ```basic
    SOUND.VOICE 0, "NOISE", 2.0, 2.0, 0.5, 4.0
    SOUND.FILTER 0, 600   ' Dark noise
    ```
20. **Snare Drum** (Short burst of bright noise)
    ```basic
    SOUND.VOICE 0, "NOISE", 0.001, 0.15, 0.0, 0.1
    SOUND.FILTER 0, 5000  ' Keep it bright
    ```

### 40 Common Patterns

I have organized them by category. You can copy the **Pattern String** directly into your `SOUND.SEQ` command.

> **Note on Syntax:**
>
>   * The sequencer plays notes in a loop (a "Cycle").
>   * `c2 c2 c2 c2` plays 4 equal quarter notes.
>   * `[c2 c2]` plays two notes in the space of one (8th notes).
>   * `[[c2 c2] c2]` plays two 16th notes followed by an 8th note.
>   * `~` is a rest.

#### 1-10: Drum Rhythms

*Assumes Track 0 is Kick, Track 1 is Snare/Clap, Track 2 is Hi-Hat.*

1.  **Classic Techno Kick (4-on-the-floor)**
    `"c1 c1 c1 c1"`
2.  **Basic Rock Kick** (Kick on 1, 2-and, 3)
    `"c1 [~ c1] c1 ~"`
3.  **Boots and Cats** (Kick/Hat alternating)
    `"c1 c4 c1 c4"`
4.  **Trap Hi-Hats** (Rolling triplets and speeds)
    `"c4 [c4 c4 c4] c4 [c4 c4 c4 c4]"`
5.  **Motorik Beat (Krautrock)** (Steady 8th pulse)
    `"[c1 c1] [c1 c1] [c1 c1] [c1 c1]"`
6.  **Reggaeton Kick** (Dem Bow rhythm)
    `"c1 [~ c1] ~ [~ c1]"`
7.  **Drum & Bass Break** (Kick/Snare interplay; use tempo \> 160)
    `"c1 ~ c2 [~ c1]"`
8.  **Offbeat Open Hats** (The "House" lift)
    `"~ c4 ~ c4"`
9.  **Hip Hop Swing** (Kick pattern with a rest)
    `"c1 ~ ~ [~ c1]"`
10. **Disco Claps** (On the 2 and 4)
    `"~ c2 ~ c2"`

#### 11-20: Basslines

*Best used with `SAW` or `SQUARE` waveforms and a Low-Pass Filter.*

11. **The Gallop** (Iron Maiden / Psytrance style)
    `"~ [c2 c2] ~ [c2 c2]"`
12. **Octave Jumper** (Disco/Synthwave: Root then Octave)
    `"[c2 c3] [c2 c3] [c2 c3] [c2 c3]"`
13. **Acid House** (Syncopated 16ths)
    `"c2 [~ c2] [c3 ~] [c2 c2]"`
14. **Walking Bass** (Jazzy quarter notes)
    `"c2 e2 g2 a2"`
15. **Offbeat Bass** (Classic Trance)
    `"~ c2 ~ c2"`
16. **Sub Drop** (Long sustained notes)
    `"c1 ~ ~ ~"`
17. **Funk Groove** (Heavy on the 'one', syncopated end)
    `"c2 ~ ~ [c2 c2]"`
18. **Pedal Point** (Repeated root note with moving melody on top)
    `"c2 c2 c2 c2"`
19. **Drop Rhythm** (Dubstep-ish wobble pacing)
    `"c1 [~ c1] [c1 c1 c1] ~"`
20. **Pump Bass** (Sidechain simulation, missing the 'one')
    `"~ c2 c2 c2"`

#### 21-30: Arpeggios & Chords

*Arpeggios written as patterns. A track also plays real chords: a stack `[c3,e3,g3]` or a chord symbol `"Cmaj7"` in a pattern, and from a keyboard the `chord` and `arp` options of `SOUND.ZONE` (section 8).*

21. **Major Triad Arp** (Root, 3rd, 5th)
    `"[c3 e3] [g3 c4] [c3 e3] [g3 c4]"`
22. **Minor Arp Up** (Root, b3, 5)
    `"[c3 d#3] [g3 c4] ~ ~"`
23. **Fast Bubbles** (Random-sounding 16ths)
    `"[c3 g3] [e3 c4] [g3 e3] [c3 g3]"`
24. **Alberti Bass** (Classic Piano Accompaniment: Low-High-Mid-High)
    `"[c3 g3] [e3 g3] [c3 g3] [e3 g3]"`
25. **Power Chords** (Root and Fifth only)
    `"[c3 g3] ~ [f3 c4] ~"`
26. **Sus4 Resolution** (Tension and release)
    `"[c3 f3] [g3 c3] ~ ~"`
27. **Video Game Rise** (Fast triplet climb)
    `"[c3 e3 g3] [c4 e4 g4] [c5 ~ ~] ~"`
28. **Descending Bell**
    `"c5 b4 g4 e4"`
29. **Stranger Things Style** (Continuous 8th note run)
    `"[c3 e3] [g3 b3] [c4 b3] [g3 e3]"`
30. **Strum** (Very fast chord simulation)
    `"[[c3 e3 g3] ~] ~ ~ ~"`

#### 31-40: Melodic Riffs & Leads

*Best with `SQUARE` or `TRIANGLE` and `SOUND.LFO` for vibrato.*

31. **Blues Lick** (Pentatonic scale)
    `"c3 d#3 f3 [f#3 g3]"`
32. **The "Sandstorm" Rhythm** (Fast distinct repeated notes)
    `"[c4 c4 c4 c4] [~ c4 c4 ~]"`
33. **Call and Response** (Phrase A, Rest, Phrase B)
    `"[c4 e4] ~ [d4 f4] ~"`
34. **Chromatic slide**
    `"[c3 c#3] [d3 d#3] e3 ~"`
35. **Simple Whistle**
    `"c4 ~ e4 [d4 c4]"`
36. **Trill** (Rapid alteration between two notes)
    `"[c4 d4] [c4 d4] [c4 d4] [c4 d4]"`
37. **Siren** (Up and down slow)
    `"c4 ~ c5 ~"`
38. **Pop Hook** (Simple, repetitive, catchy)
    `"e4 [d4 c4] e4 g4"`
39. **Question & Answer** (High pitch then low pitch)
    `"[c5 c5] ~ [c3 c3] ~"`
40. **Euclidean-style** (3 notes spread over 4 steps)
    `"c4 [~ c4] c4 ~"`
