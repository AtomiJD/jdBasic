#!/usr/bin/env bash
# Interpreter vs native-codegen parity matrix.
#
# Scope matters more than coverage here. The pre-commit gate is four curated
# suites under tests/gate (see .claude/skills/jdbgate); the rest of tests/ is,
# in its own README, "part regression bank, part scratch-pad". Sweeping all of
# it reports IMPORT helpers, model-dependent RAG tests and TUI tests from a
# build without the TUI flag as parity failures, which they are not.
#
# So the default is the set that can actually carry a parity signal, and every
# excluded file is counted in the summary rather than silently dropped.
#
#   parity.sh [-t SECONDS] [-j JOBS] [-o OUTFILE] [--all] [pattern]
#
#   --all   include the excluded directories too (needs models, network, a TTY
#           and a TUI-enabled build; expect reds that say nothing about parity)
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
ALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        -t) TIMEOUT="$2"; shift 2 ;;
        -j) JOBS="$2"; shift 2 ;;
        -o) OUT="$2"; shift 2 ;;
        --all) ALL=1; shift ;;
        *)  PATTERN="$1"; shift ;;
    esac
done

# Directories whose reds report the environment, not the backends: RAG/AI need
# local models, http needs the network, tui needs a TTY and a TUI-enabled
# build (without the flag there are no TUI.* symbols at all).
EXCLUDE_DIRS='^tests/(rag|ai|http|tui)/'

WORK="${PARITY_WORK:-${TMPDIR:-/tmp}/jdb_parity}"
mkdir -p "$WORK/exe" "$WORK/log"

# Classify one run: exit code + captured output -> status word.
classify() {
    local code="$1" log="$2"
    if [ "$code" = "124" ] || [ "$code" = "137" ]; then echo "TIMEOUT"; return; fi
    # The pass marker wins: a suite may print "Error #" while exercising TRY/CATCH
    # and still be green.
    # Suites name themselves in the marker ("ALL BNOT TESTS PASSED"), so the
    # pattern has to allow that middle word - matching only the bare form
    # scored seven passing tests as failures.
    if grep -qE 'ALL [A-Z0-9 _-]*TESTS PASSED|0 failed' "$log" 2>/dev/null; then echo "PASS"; return; fi
    if grep -qE '(^|[[:space:]])FAIL:|[1-9][0-9]* failed' "$log" 2>/dev/null; then echo "FAIL"; return; fi
    if [ "$code" = "0" ]; then echo "OK"; return; fi
    echo "FAIL:$code"
}

green() { case "$1" in PASS|OK) return 0 ;; *) return 1 ;; esac; }

run_one() {
    local rel="$1"
    local id="${rel//\//_}"; id="${id%.jdb}"

    local abs="$REPO/$rel"
    local ilog="$WORK/log/$id.interp.txt"
    local clog="$WORK/log/$id.compile.txt"
    local nlog="$WORK/log/$id.native.txt"

    # Capture each exit status into a plain variable on its own line: a `local`
    # declaration is itself a command and would overwrite $? before it is read.
    local istat nstat verdict irc crc nrc

    # Run from the repo root, the way the gate does - tests reference their
    # fixtures repo-relative ("tests/foo.json"), so a per-test cwd breaks
    # working tests and makes them look like parity failures.
    ( cd "$REPO" && timeout -k 2 "$TIMEOUT" "$JDB" "$rel" ) >"$ilog" 2>&1 </dev/null
    irc=$?
    istat=$(classify "$irc" "$ilog")

    timeout -k 2 90 "$JDB" -c -o "$WORK/exe/$id.exe" "$abs" >"$clog" 2>&1 </dev/null
    crc=$?
    if [ "$crc" -ne 0 ] || [ ! -f "$WORK/exe/$id.exe" ]; then
        nstat="CFAIL"
        : >"$nlog"
    else
        ( cd "$REPO" && timeout -k 2 "$TIMEOUT" "$WORK/exe/$id.exe" ) >"$nlog" 2>&1 </dev/null
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
# tests/_scratch holds retired files that run in neither backend (see the
# README there). They are kept for reference, never swept.
FOUND=$(git ls-files -- tests | grep '\.jdb$' | grep -v '^tests/_scratch/')

# A module exists to be IMPORTed. Running one standalone proves nothing about
# either backend, so drop those before anything else.
HELPERS=""
LIST=""
while IFS= read -r f; do
    [ -n "$f" ] || continue
    if head -40 "$f" 2>/dev/null | grep -qiE '^[[:space:]]*(EXPORT[[:space:]]+)?MODULE[[:space:]]'; then
        HELPERS="$HELPERS$f"$'\n'
    else
        LIST="$LIST$f"$'\n'
    fi
done <<< "$FOUND"
N_HELPERS=$(printf '%s' "$HELPERS" | grep -c . || true)

N_ENV=0
if [ "$ALL" -eq 0 ]; then
    N_ENV=$(printf '%s' "$LIST" | grep -cE "$EXCLUDE_DIRS" || true)
    LIST=$(printf '%s' "$LIST" | grep -vE "$EXCLUDE_DIRS" || true)
fi

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
echo "=== SCOPE ==="
printf '%-34s %d\n' "run" "$TOTAL"
printf '%-34s %d\n' "skipped: IMPORT helpers" "$N_HELPERS"
if [ "$ALL" -eq 0 ]; then
    printf '%-34s %d   (rag/ai/http/tui - rerun with --all)\n' \
        "skipped: needs environment" "$N_ENV"
fi
echo
echo "=== SUMMARY ==="
awk -F'\t' '{v[$4]++} END {for (k in v) printf "%-12s %d\n", k, v[k]}' "$RESULTS" | sort
echo
echo "=== PARITY GAPS (interp green, native red) ==="
echo "note: a loose test rejected by the STRICT native compiler is expected -"
echo "      the interpreter is deliberately loose. Look for runtime divergence."
awk -F'\t' '$4=="GAP" {printf "%-52s interp=%-6s native=%s\n", $1, $2, $3}' "$RESULTS"
echo
echo "results: $RESULTS"
