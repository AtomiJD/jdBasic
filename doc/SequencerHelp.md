
# Sequencer Help

This document provides a detailed reference for the `SOUND` and `SFX` modules in jdBasic. It covers the live-coding sequencer, track-level sound design, global effects, visualization, and custom sample handling.

## 1. Core Sound System

* **`SOUND.INIT`**: Initializes the audio engine. Must be called before any other sound commands.
* **`SOUND.BPM value`**: Sets the tempo for the sequencer in Beats Per Minute.
* **`SOUND.RESET`**: Silences all audio, clears all sequencer layers, and resets effects to default.
* **`SOUND.SHUTDOWN`**: Safely stops the audio device and releases memory.

## 2. Live Coding Sequencer

The sequencer programs rhythmic patterns into 8 available tracks (0-7).

* **`SOUND.SEQ track, pattern$, waveform$`**: Programs a rhythmic pattern into a specific track.
* **`pattern$`**: Space-separated tokens using mini-notation. Use `~` for rests and `[...]` for subdivisions.
* **`waveform$`**: Options include `"SINE"`, `"SQUARE"`, `"SAW"`, `"TRIANGLE"`, `"NOISE"`, or `"VOICE"`.

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
* **`SOUND.REVERBSEND track, amount`**: Sets the signal level sent to the global Reverb (0.0–1.0).
* **`SOUND.DELAYSEND track, amount`**: Sets the signal level sent to the global Delay (0.0–1.0).
* **`SOUND.SIDECHAIN target, source, amount`**: Ducks the volume of the `target` when the `source` plays.

### Per-Track Modulation

These shape the timbre of a track's voice and must be set before the pattern plays.

* **`SOUND.EQ track, low, mid, high`**: 3-band equalizer gains (1.0 = flat).
* **`SOUND.LFO track, freq, depth`**: Vibrato - pitch modulation at `freq` Hz with `depth`.
* **`SOUND.FM track, amount, ratio`**: Frequency modulation for metallic / bell tones.
* **`SOUND.UNISON track, voices, detune, spread`**: Stacks `voices` (1–16) detuned copies for a super-saw; `detune` 0.0–1.0, `spread` 0.0–1.0 stereo.
* **`SOUND.BITCRUSH track, bits, rate`**: Lo-fi effect; `bits` 1–16 resolution, `rate` 0.0–1.0 sample-rate reduction.
* **`SOUND.RINGMOD track, freq, mix`**: Ring modulation for robotic / sci-fi tones.

## 4. PCM Samples and SFX

jdBasic allows you to load and play high-quality WAV files.

### Global SFX and Music

* **`SFX.LOAD id, "filepath.wav"`**: Loads a WAV file into memory slot `id`.
* **`SFX.PLAY id`**: Plays a loaded WAV file once (fire-and-forget).
* **`MUSIC.PLAY id, [loop_bool]`**: Plays a loaded WAV file as background music.
* **`MUSIC.STOP`**: Immediately stops the background music.

### Samples as Sequencer Instruments

You can use a loaded sample as a sound source for a sequencer track.

* **`SOUND.SAMPLE track, sfx_id, [base_note$], [loop_bool]`**: Assigns a loaded `SFX` ID to a track for pitched playback.
* `base_note$`: The root pitch of the original sample (e.g., "C3").
* `loop_bool`: If `TRUE`, the sample loops continuously while the note is active.

## 5. Global Effects (Master Bus)

* **`SOUND.REVERB size, damp, width, wet`**: Configures the global reverb room.
* **`SOUND.DELAY active, time_ms, feedback, mix`**: Configures the global stereo delay.
* **`SOUND.COMPRESSOR thresh, ratio, attack, release, gain`**: Master dynamics; `thresh` 0.0–1.0, `ratio` 1.0–20.0, `attack`/`release` in ms.
* **`SOUND.DISTORTION amount`**: Applies master saturation/overdrive.

## 6. Visualization

* **`SOUND.GET_WAVE()`**: Returns an array of the master mix for oscilloscopes.
* **`SOUND.GET_BUS_WAVE(id)`**: Returns the "wet-only" signal from a bus (`0` for Reverb, `1` for Delay).

## 7. Embedded / Pull-Mode Rendering

Normally `SOUND.INIT` opens an SDL audio device and a background thread plays
the mix. When jdBasic is built **without** a device (the `SOUND` build flag,
`/DSOUND_DSP` - used by embed hosts such as Godot), the same sequencer runs but
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

---

## Comprehensive Command Table

| Category | Command | Parameters | Description |
| --- | --- | --- | --- |
| **System** | `SOUND.INIT` | none | Start audio engine |
|  | `SOUND.BPM` | `bpm` | Set sequencer speed |
|  | `SOUND.RESET` | none | Clear all sound/patterns |
| **Sequencer** | `SOUND.SEQ` | `track, pattern$, wave$` | Define rhythmic pattern |
|  | `SOUND.SCALE` | `track, root$, mode$` | Quantize pattern notes to scale |
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
|  | `SOUND.DISTORTION` | `amount` | Master saturation |
| **Analysis** | `SOUND.GET_WAVE` | none | Get master waveform array |
|  | `SOUND.RENDER` | `frames` | Pull stereo samples (embed/no-device) |
|  | `SOUND.GET_BUS_WAVE` | `bus_id` | Get wet-only waveform |

---

## 7. Examples

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
7.  **Drum & Bass Break** (Kick/Snare interplay - use tempo \> 160)
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
12. **Octave Jumper** (Disco/Synthwave - Root then Octave)
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
20. **Pump Bass** (Sidechain simulation - missing the 'one')
    `"~ c2 c2 c2"`

#### 21-30: Arpeggios & Chords

*Since a single track is monophonic, we simulate chords by playing their notes quickly in sequence (Arpeggios).*

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
