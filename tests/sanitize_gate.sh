#!/usr/bin/env bash
# The gate under AddressSanitizer and UndefinedBehaviorSanitizer, on a Linux
# machine whose build was made with
#
#   SANITIZE=1 HTTP=1 GFX=1 IMGUI=1 NATIVEC=1 ./build.sh
#
# Runs the four suites in both backends, the harness, the leak patterns and
# the rc/str regression tests. Every sanitizer report lands as a file under
# tmp/san/, and the end of the run sums them up by message. Exit 1 when a
# step failed or a report was written.
#
#   tests/sanitize_gate.sh [rounds]        (leak_check rounds, default 5000)

set -u
cd "$(dirname "$0")/.." || exit 1
jdb=./build/jdbasic
[ -x "$jdb" ] || jdb=./build/jdBasic
if [ ! -x "$jdb" ]; then
    echo "build/jdbasic is missing" >&2
    exit 1
fi
rounds="${1:-5000}"
rm -rf tmp/san
mkdir -p tmp/san
export ASAN_OPTIONS="detect_leaks=0:print_stacktrace=1:log_path=$PWD/tmp/san/asan"
export UBSAN_OPTIONS="print_stacktrace=1:log_path=$PWD/tmp/san/ubsan"
export JDB_SANITIZE=1
fail=0

# run <label> <command...>: one line per step with the exit code and the
# summary line the step printed.
run() {
    local label="$1"; shift
    local out rc summary
    out=$("$@" 2>&1); rc=$?
    summary=$(printf '%s\n' "$out" | grep -aE 'PASSED|RESULTS:|RESULT:|FAIL|^OK$' | tail -1)
    printf '%-44s rc=%-4s %s\n' "$label" "$rc" "$summary"
    [ "$rc" -ne 0 ] && fail=1
    # A report a child process wrote to its own stderr instead of log_path
    # (the harness and leak_check run the binaries themselves).
    local inline
    inline=$(printf '%s\n' "$out" | grep -aE 'runtime error:|ERROR: AddressSanitizer|SUMMARY: .*Sanitizer' | sort | uniq -c | sort -rn | head -10)
    if [ -n "$inline" ]; then
        printf '%s\n' "$inline" | sed 's/^/    /'
        fail=1
    fi
}

for t in comprehensive_test native_test test_apl_complete test_apl_pipelines; do
    run "interp $t" "$jdb" "tests/gate/$t.jdb"
done
for t in comprehensive_test native_test.strict test_apl_complete test_apl_pipelines; do
    exe="tests/gate/$t.san"
    rm -f "$exe"
    "$jdb" -c -o "$exe" "tests/gate/$t.jdb" >/dev/null 2>&1
    if [ -x "$exe" ]; then
        run "native $t" "$exe"
    else
        printf '%-44s COMPILE FAILED\n' "native $t"
        fail=1
    fi
done
run "harness" "$jdb" tools/diff_backends.jdb
run "leak_check" bash tests/leak_check.sh "$rounds"
for t in tests/regression/test_rc_*.jdb tests/regression/test_str_*.jdb \
         tests/regression/test_array_results_outlive_args.jdb tests/regression/test_json_locale.jdb; do
    [ -f "$t" ] || continue
    run "interp $(basename "$t")" "$jdb" "$t"
    exe="${t%.jdb}.san"
    rm -f "$exe"
    "$jdb" -c -o "$exe" "$t" >/dev/null 2>&1
    [ -x "$exe" ] && run "native $(basename "$t")" "$exe"
done

n=$(ls tmp/san 2>/dev/null | wc -l)
echo "sanitizer reports: $n (tmp/san)"
if [ "$n" -gt 0 ]; then
    grep -ahE 'SUMMARY: |runtime error:' tmp/san/* | sed 's/^.*SUMMARY: //' | sort | uniq -c | sort -rn | head -40
    fail=1
fi
exit $fail
