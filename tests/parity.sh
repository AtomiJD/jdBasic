#!/usr/bin/env bash
# Interpreter vs native-codegen parity matrix.
# Runs every tracked tests/**/*.jdb through both backends and classifies the pair.
#
#   parity.sh [-t SECONDS] [-j JOBS] [-o OUTFILE] [pattern]
#
# Result columns: TEST | INTERP | NATIVE | VERDICT
#   INTERP/NATIVE: PASS (assert marker) OK (exit 0) FAIL FAIL:<code> TIMEOUT CFAIL
#   VERDICT:       OK | GAP (interp green, native not) | BOTH_RED | NATIVE_ONLY

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$REPO/build/jdBasic.exe" ] || REPO="D:/usr/dev/cc"
JDB="$REPO/build/jdBasic.exe"

TIMEOUT=20
JOBS=1
OUT=""
PATTERN=""

while [ $# -gt 0 ]; do
    case "$1" in
        -t) TIMEOUT="$2"; shift 2 ;;
        -j) JOBS="$2"; shift 2 ;;
        -o) OUT="$2"; shift 2 ;;
        *)  PATTERN="$1"; shift ;;
    esac
done

WORK="${PARITY_WORK:-${TMPDIR:-/tmp}/jdb_parity}"
mkdir -p "$WORK/exe" "$WORK/log"

# Classify one run: exit code + captured output -> status word.
classify() {
    local code="$1" log="$2"
    if [ "$code" = "124" ] || [ "$code" = "137" ]; then echo "TIMEOUT"; return; fi
    # The pass marker wins: a suite may print "Error #" while exercising TRY/CATCH
    # and still be green.
    if grep -qE 'ALL TESTS PASSED|0 failed' "$log" 2>/dev/null; then echo "PASS"; return; fi
    if grep -qE '(^|[[:space:]])FAIL:|[1-9][0-9]* failed' "$log" 2>/dev/null; then echo "FAIL"; return; fi
    if [ "$code" = "0" ]; then echo "OK"; return; fi
    echo "FAIL:$code"
}

green() { case "$1" in PASS|OK) return 0 ;; *) return 1 ;; esac; }

run_one() {
    local rel="$1"
    local id="${rel//\//_}"; id="${id%.jdb}"
    local dir="$REPO/$(dirname "$rel")"
    local abs="$REPO/$rel"
    local ilog="$WORK/log/$id.interp.txt"
    local clog="$WORK/log/$id.compile.txt"
    local nlog="$WORK/log/$id.native.txt"

    # Capture each exit status into a plain variable on its own line: a `local`
    # declaration is itself a command and would overwrite $? before it is read.
    local istat nstat verdict irc crc nrc

    ( cd "$dir" && timeout -k 2 "$TIMEOUT" "$JDB" "$abs" ) >"$ilog" 2>&1 </dev/null
    irc=$?
    istat=$(classify "$irc" "$ilog")

    timeout -k 2 90 "$JDB" -c -o "$WORK/exe/$id.exe" "$abs" >"$clog" 2>&1 </dev/null
    crc=$?
    if [ "$crc" -ne 0 ] || [ ! -f "$WORK/exe/$id.exe" ]; then
        nstat="CFAIL"
        : >"$nlog"
    else
        ( cd "$dir" && timeout -k 2 "$TIMEOUT" "$WORK/exe/$id.exe" ) >"$nlog" 2>&1 </dev/null
        nrc=$?
        nstat=$(classify "$nrc" "$nlog")
    fi

    case "$rel" in
        *_bad.jdb|*_invalid.jdb)
            # Negative tests: STRICT/EXPLICIT must reject them at compile time.
            if [ "$nstat" = "CFAIL" ]; then
                printf '%s\t%s\t%s\t%s\n' "$rel" "$istat" "$nstat" "XFAIL"
                return
            fi ;;
    esac
    if green "$istat" && green "$nstat"; then verdict="OK"
    elif green "$istat"; then verdict="GAP"
    elif green "$nstat"; then verdict="NATIVE_ONLY"
    else verdict="BOTH_RED"
    fi

    printf '%s\t%s\t%s\t%s\n' "$rel" "$istat" "$nstat" "$verdict"
}

export -f run_one classify green
export REPO JDB WORK TIMEOUT

cd "$REPO" || exit 1
LIST=$(git ls-files -- tests | grep '\.jdb$')
[ -n "$PATTERN" ] && LIST=$(printf '%s\n' "$LIST" | grep -- "$PATTERN")

TOTAL=$(printf '%s\n' "$LIST" | grep -c .)
echo "parity: $TOTAL tests, timeout ${TIMEOUT}s, jobs $JOBS, work $WORK" >&2

RESULTS="$WORK/results.tsv"
if [ "$JOBS" -gt 1 ]; then
    printf '%s\n' "$LIST" | xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {} > "$RESULTS"
else
    : > "$RESULTS"
    n=0
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        n=$((n+1))
        printf '\r[%d/%d] %-60s' "$n" "$TOTAL" "$rel" >&2
        run_one "$rel" >> "$RESULTS"
    done <<< "$LIST"
    echo >&2
fi

sort -o "$RESULTS" "$RESULTS"
[ -n "$OUT" ] && cp "$RESULTS" "$OUT"

echo
echo "=== SUMMARY ==="
awk -F'\t' '{v[$4]++} END {for (k in v) printf "%-12s %d\n", k, v[k]}' "$RESULTS" | sort
echo
echo "=== PARITY GAPS (interp green, native red) ==="
awk -F'\t' '$4=="GAP" {printf "%-52s interp=%-6s native=%s\n", $1, $2, $3}' "$RESULTS"
echo
echo "results: $RESULTS"
