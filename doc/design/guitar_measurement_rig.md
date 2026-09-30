# Guitar measurement rig

Notes for the physical half of the guitar tone work. The software effect chain
(`FX.*`, `MON.*`) covers everything that happens to a signal once it is a signal.
This is about the parts that are not signal processing at all, and that a plugin
therefore cannot fake.

## What the rig is for

Three things the effect chain cannot reproduce, no matter how good the algorithms
get:

**1. Pickups in series are not a sum.** A guitar with three pickups wired in
series, each with its own on/off and phase switch, is the Red Special layout. Two
coils in series mean twice the inductance and twice the resistance, so the
resonant peak formed with the cable capacitance moves down in frequency. That is
why the sound gets thicker and not merely louder. Summing two recordings in
software adds the signals but not the electrical network, and the network is where
the character comes from.

**2. Speaker plus microphone is a real acoustic channel.** Two microphones whose
path lengths differ by `d` produce a comb filter with its first cancellation at
`c / (2 * d)`, with `c` about 343 m/s. At 5 cm that is 3430 Hz. This is the
cheapest possible self-test for the whole chain: predict the notch from a tape
measure, then measure it.

**3. Two amplifiers in a room sum acoustically, not electrically.** The classic
three-amp cascade with different delay times owes a large part of its size to the
fact that each source reaches each point in the room at a different time. One
stereo output does not produce that.

## Software status

Shipped, ticket #210, commit `f9956c0`:

- `WAV.RECORD(seconds, [opts])` opens its own capture device, records the raw
  input at any channel count, and returns the same map `WAV.READ` does. Options:
  `channels`, `rate`, `device`, `seconds`, `play`, `playchannels`, `playdevice`.
- `WAV.RECSTART` / `WAV.RECLEN` / `WAV.RECSTOP` are the non-blocking form.
- A `play` array switches the device to duplex, so output and capture run off one
  clock. The offset between the played sweep and the recorded answer is then the
  hardware round trip alone, constant, and measurable once with a loopback cable.
- `WAV.READ` reads PCM 8/16/24/32, IEEE float 32/64 and `WAVE_FORMAT_EXTENSIBLE`.
  Before this it handled 8/16-bit PCM and float32 only, which excluded both the
  format WASAPI recorders and ffmpeg write by default and the 24 bits an audio
  interface delivers.
- Samples come back interleaved: frame `f` channel `c` is
  `samples[f * channels + c]`. Two microphones de-interleave with a stride of 2.

Verified by `tests/wav_format_test.jdb` (64 assertions; it builds each WAV format
byte by byte with `BINWRITER`, so it needs no recorder installed).

Build flags required: `FX` for the WAV functions, `MINIAUDIO` for capture. The
current local build carries `HTTP GFX IMGUI FORMS FX MCPSERVER NATIVEC MINIAUDIO
SERIAL`.

### Still to write

A measurement script: generate a log sine sweep, send it through the duplex path
while recording, deconvolve to an impulse response, save it, and hand it to a
`cabinet` node in the FX chain. The deconvolution is a complex division per bin.
`FFT` returns `[N][2]` real/imaginary pairs, so an elementwise `/` divides the
parts separately and does not give a quotient; it has to be written out.

That script is the piece that turns the rig into a cabinet simulation, and it can
be written and tested before any hardware arrives by looping the output back to
the input in software.

## Shopping list

**Microphones: two MAX4466 electret amplifier modules.** Electret capsule plus a
low-noise opamp, gain set by a trimmer between 25x and 125x, output biased to half
the supply, so it goes straight into a line input or an ADC. Two identical ones,
because one microphone measures frequency response while two measure time, phase
and comb filtering, and the second is the whole point.

Avoid the MAX9814: it has automatic gain control, which changes the gain
dynamically and destroys exactly the amplitude relationships being measured.

Bare electret capsules are fine as a stock item but are only the transducer: they
need a bias resistor (2.2k to 10k to 3-5 V), a coupling capacitor (1-10 uF) and a
preamplifier of 40-50 dB, which is one TL072 or NE5532.

**Not suitable: the KY-038 style "sound detection module".** Electret plus an
LM393 comparator and a threshold trimmer, built to answer "loud or quiet" on a
digital pin. The analogue output has no useful gain and the comparator injects
switching noise. It cannot capture a waveform.

**An audio interface with two real inputs**, unless the PC has a stereo line
input. The microphone jack on a PC is mono and carries bias. This is the one
purchase that actually raises the measurement floor, because it replaces a
hobby-grade ADC with a real one.

**Already available:** PAM8403 class-D amplifier boards (3 W + 3 W at 5 V), 3 W
8 ohm mini speakers, opamps, relays or 4066 analogue switches, capacitors.

**For stage 3:** three cheap single-coil pickups, one guitar string, a board and
two bridges. The pickups must be **magnetic**, not piezo. A piezo hears the body,
not the position along the string, and position is the entire experiment: a pickup
is deaf to every harmonic that has a node where it sits.

## PAM8403 traps

1. **The output is bridge-tied.** Neither speaker terminal sits at ground. Never
   ground one of them, and never feed the output directly into a line input or a
   ground-referenced scope probe.
2. **The class-D carrier is on the output pins**, around 250 kHz. A speaker
   filters it away acoustically; an ADC does not, and it aliases into the audio
   band. Measure electrically only differentially and through an LC filter, or
   measure acoustically, which is what the rig does anyway.
3. **Clean 5 V and a large capacitor** (470 to 1000 uF) at the module. Class-D
   draws current spikes and bare USB power produces audible chirping.
4. **Feedback.** Microphone and speaker in one room will howl. Start with low
   gain, put the microphone off-axis.
5. **Ground loops** between PC audio, the amplifier supply and the microphone
   supply. Power everything from one supply, or put a small transformer in the
   input.

## Stages

**Stage 0, with hardware already on hand.** Sound card out, PAM8403, mini speaker.
Log sweep from jdBasic, record, `FFT`. This establishes the noise floor and proves
the chain before anything depends on it.

**Stage 1, impulse response.** Sweep out, record, deconvolve. The result is the
impulse response of amplifier plus speaker plus enclosure plus room plus
microphone, which is exactly what a cabinet simulation is. Convolve it in the FX
chain and the effect chain has a real speaker in front of it. Putting the small
speaker into a closed box adds an enclosure resonance immediately; two boxes give
two audibly different impulse responses.

A PAM8403 driven into clipping is a small cheap transistor amplifier at its limit
into a small speaker, which is closer to the small-amp recording trick than it has
any right to be.

**Stage 2, the A/B split.** Two PAM8403, two speakers placed apart in the room.
Stereo out, dry to one, delayed to the other. One or two microphones in the room.
Measuring a pair of impulse responses turns the multi-amp cascade into a
convolution.

**Stage 3, monochord with three pickups.** A board, two bridges, one guitar
string, a wing nut to tune it. Three pickups at different positions, wired with an
on/off and a phase switch each, all in series.

jdBasic has `SERIAL.*`, so the switch matrix can go on relays or 4066 switches
driven by a microcontroller: step through every combination and capture an impulse
response for each. Three pickups with on/off and phase is a small enough space to
sweep exhaustively and end up with a table instead of an opinion.

A small exciter coil next to a pickup, fed with a sweep, measures the electrical
resonance directly: single coil, two in series, three in series, with and without
the cable. Those numbers are what a software model of the pickup needs.

## Where this stopped

Software side is done and green as described above, except the measurement script.
Hardware side is waiting on the microphones. Stage 0 and stage 1 need nothing else
once they arrive.
