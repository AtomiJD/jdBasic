#!/usr/bin/env bash
# Compiles bench_native_string_memory.jdb and reports peak resident size for
# three loop counts per mode. A backend that frees its runtime strings shows a
# flat column; today the two string modes grow linearly and the numeric one
# does not.
#
#   tests/bench_native_string_memory.sh
#
# Windows only for the sampling step (it reads tasklist).

set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
build="$repo/build"
work="$build/_bench_strmem"

if [ ! -x "$build/jdBasic.exe" ]; then
    echo "build/jdBasic.exe is missing - build with the NATIVEC flag first" >&2
    exit 1
fi

mkdir -p "$work"
cp "$here/bench_native_string_memory.jdb" "$work/bench.jdb"
( cd "$build" && ./jdBasic.exe -c "$work/bench.jdb" >/dev/null 2>&1 )
if [ ! -f "$work/bench.exe" ]; then
    echo "compile failed" >&2
    exit 1
fi

peak_of() {
    local exe_name="$1"; shift
    "$@" >/dev/null 2>&1 &
    local bg=$!
    local peak=0 sample
    while kill -0 "$bg" 2>/dev/null; do
        sample=$(tasklist //FI "IMAGENAME eq $exe_name" //FO CSV //NH 2>/dev/null \
                 | awk -F'","' '{gsub(/[".K ]/,"",$NF); print $NF}' | head -1)
        case "$sample" in ''|*[!0-9]*) ;; *) [ "$sample" -gt "$peak" ] && peak=$sample ;; esac
    done
    wait "$bg" 2>/dev/null
    echo "$peak"
}

printf '%-8s %12s %10s %10s\n' mode rounds peakMB bytes/round
for mode in concat map func typeof number; do
    for rounds in 5000000 10000000 20000000; do
        peak=$(peak_of "bench.exe" "$work/bench.exe" "$mode" "$rounds")
        printf '%-8s %12s %10s %10s\n' \
            "$mode" "$rounds" "$((peak / 1024))" "$((peak * 1024 / rounds))"
    done
done

rm -rf "$work"
