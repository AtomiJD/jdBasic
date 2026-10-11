#!/bin/sh
# Capture the stdout of every jdb/ example that runs to completion, so two
# builds can be compared on what they printed and not merely on whether they
# exited zero. Same verdict with different output is exactly the kind of
# breakage a exit-code sweep misses.
#
# Usage: ./capture_jdb_output.sh <binary> <outdir> [seconds]

BIN=${1:-build/jdBasic.exe}
DIR=${2:-/tmp/jdb_out}
T=${3:-8}
ROOT=$(pwd)
rm -rf "$DIR"; mkdir -p "$DIR"

for f in $(find jdb -name '*.jdb' | sort); do
    d=$(dirname "$f"); b=$(basename "$f")
    out=$(cd "$d" && timeout "$T" "$ROOT/$BIN" "$b" 2>&1)
    [ $? -eq 0 ] || continue                      # only the ones that finish
    # Strip what legitimately differs between two runs of the same binary:
    # colour codes, timings, dates, addresses and anything counted off the
    # directory, which the sweep itself changes by writing files.
    printf '%s' "$out" \
      | sed 's/\x1b\[[0-9;]*m//g' \
      | sed -E 's/[0-9]+\.[0-9]+ ?ms/<ms>/g; s/[0-9]{4}-[0-9]{2}-[0-9]{2}/<date>/g; s/0x[0-9a-fA-F]+/<addr>/g' \
      > "$DIR/$(printf '%s' "$f" | tr '/' '_').txt"
done

printf 'captured %s\n' "$(ls "$DIR" | wc -l)"
