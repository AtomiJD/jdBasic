#!/bin/sh
# Turns the PNG sequence written by repofilm.jdb into an mp4, with the
# soundtrack if one was rendered.
#
#   ./render.sh [frame_dir] [out_file] [wav_file]
#   PRESET=medium ./render.sh tmp/frames tmp/out.mp4 tmp/repofilm.wav
#
# The x264 preset is worth lowering for 4K, where "slow" costs a lot of
# minutes for very little on a mostly black picture.

FRAMES=${1:-tmp/frames}
OUT=${2:-tmp/repofilm.mp4}
AUDIO=${3:-}
PRESET=${PRESET:-slow}

if [ -n "$AUDIO" ]; then
    ffmpeg -y -framerate 30 -start_number 0 -i "$FRAMES/f%05d.png" -i "$AUDIO" \
        -af loudnorm=I=-16:TP=-1.5:LRA=11 \
        -c:v libx264 -pix_fmt yuv420p -crf 18 -preset "$PRESET" \
        -c:a aac -b:a 192k -shortest "$OUT"
else
    ffmpeg -y -framerate 30 -start_number 0 -i "$FRAMES/f%05d.png" \
        -c:v libx264 -pix_fmt yuv420p -crf 18 -preset "$PRESET" "$OUT"
fi

echo "wrote $OUT"
