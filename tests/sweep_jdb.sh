#!/bin/sh
# Run the jdb/ example tree through a binary and classify every file.
#
# tests/parity.sh covers the regression bank under tests/. The examples in
# jdb/ are the other half of the repo and nothing sweeps them, so a change
# in the VM core can pass the gate and still break a demo nobody runs until
# a recording session.
#
# Files are run from their own directory, because IMPORT resolves relative
# to the script. Anything that needs a model, the network, a window, a TTY
# or a device is expected to fail - the point is not that everything is
# green, it is that the same files behave the same way before and after a
# change. Diff two runs of this, do not read one in isolation.
#
# Usage: ./sweep_jdb.sh <binary> <outfile> [seconds]

BIN=${1:-build/jdBasic.exe}
OUT=${2:-/tmp/jdb_sweep.tsv}
T=${3:-8}
ROOT=$(pwd)
: > "$OUT"

for f in $(find jdb -name '*.jdb' | sort); do
    dir=$(dirname "$f")
    base=$(basename "$f")
    out=$(cd "$dir" && timeout "$T" "$ROOT/$BIN" "$base" 2>&1)
    rc=$?
    case $rc in
        0)   verdict=OK ;;
        124) verdict=TIMEOUT ;;      # servers, event loops, anything waiting
        139) verdict=SEGV ;;
        *)   verdict=ERR ;;
    esac
    # First line of the complaint, so two runs diff cleanly.
    msg=$(printf '%s' "$out" | grep -iE "^Error|error #|Undefined|Cannot|STRICT|Parse error" | head -1 | tr -d '\r' | cut -c1-90)
    printf '%s\t%s\t%s\t%s\n' "$f" "$verdict" "$rc" "$msg" >> "$OUT"
done

printf 'OK      %s\n' "$(grep -c '	OK	' "$OUT")"
printf 'ERR     %s\n' "$(grep -c '	ERR	' "$OUT")"
printf 'TIMEOUT %s\n' "$(grep -c '	TIMEOUT	' "$OUT")"
printf 'SEGV    %s\n' "$(grep -c '	SEGV	' "$OUT")"
printf 'total   %s\n' "$(wc -l < "$OUT")"
