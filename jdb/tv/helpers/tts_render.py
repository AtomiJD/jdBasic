#!/usr/bin/env python3
"""
tts_render.py — render a single line of text to a WAV via edge-tts.

Usage:
    python tts_render.py <out.wav> <voice> "<text>"

Exits 0 on success. Prints the duration in seconds to stdout (so the
jdBasic director can sleep that long while OBS records the screen).
"""
import sys, asyncio, pathlib, wave, contextlib

if len(sys.argv) < 4:
    print("usage: tts_render.py <out.wav> <voice> <text> [pitch] [rate]", file=sys.stderr)
    sys.exit(2)

out_path = pathlib.Path(sys.argv[1])
voice    = sys.argv[2]
text     = sys.argv[3]
pitch    = sys.argv[4] if len(sys.argv) > 4 else "+0Hz"    # e.g. "+40Hz"
rate     = sys.argv[5] if len(sys.argv) > 5 else "+0%"     # e.g. "+10%"

try:
    import edge_tts
except ImportError:
    print("ERROR: pip install edge-tts", file=sys.stderr)
    sys.exit(3)

async def render():
    # edge-tts writes MP3 by default. We grab MP3, then transcode to WAV
    # with ffmpeg so jdBasic's audio (and ffmpeg-mix downstream) gets a
    # known-good PCM stream.
    mp3_path = out_path.with_suffix(".mp3")
    comm = edge_tts.Communicate(text, voice, pitch=pitch, rate=rate)
    await comm.save(str(mp3_path))
    # Transcode MP3 -> WAV (44.1 kHz stereo). ffmpeg must be in PATH.
    import subprocess
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error",
         "-i", str(mp3_path),
         "-ar", "44100", "-ac", "2",
         str(out_path)],
        check=True,
    )
    mp3_path.unlink(missing_ok=True)

asyncio.run(render())

# Probe duration via wave (cheap, no ffprobe round-trip).
with contextlib.closing(wave.open(str(out_path), "rb")) as w:
    frames = w.getnframes()
    rate   = w.getframerate()
    dur    = frames / float(rate) if rate else 0.0

print(f"{dur:.3f}")
