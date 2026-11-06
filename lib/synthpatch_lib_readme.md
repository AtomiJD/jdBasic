# SYNTHPATCH: synth sounds as JSON, with morphing

`lib/synthpatch.jdb` keeps the sound of a `SOUND` track as a plain map: the
waveform, the envelope, the filter and its envelope, unison, a second
oscillator, LFO, FM, chorus, glide and the sends. A bank is a JSON array of
such maps. The module applies a patch to a track, reads back what the knobs
made of it, and blends two patches. It needs a SOUND build.

Stands in for: the patch memory and the morph control of a hardware synth.

## Quick start

```basic
IMPORT SYNTHPATCH

SOUND.INIT
DIM bank = SYNTHPATCH.LOAD_BANK("jdb/demos/audio/oxygen_patches.json")
SYNTHPATCH.APPLY 1, bank[SYNTHPATCH.FIND(bank, "pad")]

' a fader from 0 to 1 blends into the next patch
SYNTHPATCH.MORPH 1, bank[3], bank[4], 0.5

' after shaping the sound with SOUND.CCMAP knobs, keep it
DIM mine = SYNTHPATCH.CAPTURE(1, bank[3])
mine{"name"} = "my_pad"
SYNTHPATCH.SAVE_BANK "my_patches.json", [mine]
```

## Functions

| Function | What it does |
|---|---|
| `LOAD_BANK(path$)` | The patches of a JSON file, as an array of maps. |
| `SAVE_BANK path$, bank` | Writes an array of patches as JSON. |
| `FIND(bank, name$)` | The index of the patch called `name$` (any case), or -1. |
| `APPLY track, patch` | Sets every part of the sound of `track`. A key the patch leaves out takes the plain default (a sine, open filter, no effects). |
| `CAPTURE(track, base)` | A copy of `base` with the continuous keys read back from the track through `SOUND.PARAM`, so knob changes can be stored. |
| `MORPH track, a, b, x` | Sets the continuous keys of `track` between patch `a` (`x` = 0) and `b` (`x` = 1). Cutoff, attack, decay, release and LFO rate move by ratio, so the middle of 500 Hz and 2000 Hz is 1000 Hz; the rest moves in a straight line. A key missing in `b` stays as in `a`. |

## Patch keys

| Key | Meaning | Default |
|---|---|---|
| `name` | The name `FIND` looks for | |
| `wave` | `SINE`, `SAW`, `SQUARE`, `TRIANGLE`, `NOISE` | `SINE` |
| `attack`, `decay`, `sustain`, `release` | Envelope in seconds, sustain 0..1 | 0.01, 0.1, 0.7, 0.3 |
| `cutoff`, `resonance`, `filter` | Filter in Hz, 0..1, `LP` / `HP` / `BP` | 20000, 0, `LP` |
| `fenv`, `fenv_attack`, `fenv_decay` | Filter envelope: octaves, seconds | 0, 0.005, 0.2 |
| `unison`, `detune`, `spread` | Stacked voices, their detune and stereo spread | 1, 0, 0.5 |
| `osc2`, `osc2_semis`, `osc2_mix` | Second oscillator wave (or `OFF`), interval, level | `OFF` |
| `lforate`, `lfodepth` | Vibrato LFO in Hz | 0, 0 |
| `fm`, `fm_ratio` | FM amount and ratio | 0, 1 |
| `chorus` | Chorus mix | 0 |
| `glide` | Portamento in ms | 0 |
| `reverb`, `delay` | Sends | 0, 0 |
| `gain` | Track level | 0.5 |

The continuous keys, the ones `MORPH` and `CAPTURE` cover, are those
`SOUND.PARAM` reaches: `attack`, `decay`, `sustain`, `release`, `cutoff`,
`resonance`, `fenv`, `detune`, `fm`, `reverb`, `delay`, `gain`, `lforate`,
`lfodepth`.

## Example

`jdb/demos/audio/oxygen_synth.jdb` plays a lead from the bank on a MIDI
keyboard: the MODE buttons step through the patches, a fader morphs into the
next one, and REC stores the knob-shaped sound as a user patch.
