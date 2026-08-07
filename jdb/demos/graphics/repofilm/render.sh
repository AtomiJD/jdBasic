#!/bin/sh
# Turns the PNG sequence written by repofilm.jdb into an mp4.
#
#   ./render.sh [frame_dir] [out_file]
#
# Defaults match the defaults of repofilm.jdb --frames.

FRAMES=${1:-tmp/frames}
OUT=${2:-tmp/repofilm.mp4}

ffmpeg -y -framerate 30 -start_number 0 -i "$FRAMES/f%05d.png" \
    -c:v libx264 -pix_fmt yuv420p -crf 18 -preset slow "$OUT"

echo "wrote $OUT"
