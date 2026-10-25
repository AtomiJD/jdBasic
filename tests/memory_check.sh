#!/usr/bin/env bash
# The memory check a change to the native compiler or its runtime has to
# pass before it is committed (tools/hooks/check_memory.ps1 runs it):
#
#   1. build/ is newer than every source it is made of, so the check tests
#      the code that is about to be committed;
#   2. tests/leak_check.sh: no leak pattern grows the heap (exit 1 = a new
#      leak);
#   3. the ownership regressions, compiled with -c and run twice: as they
#      are and under JDB_RC_POISON=1, which overwrites what the counts free
#      so a holder they missed reads garbage at once.
#
#   tests/memory_check.sh            prints MEMCHECK PASSED or MEMCHECK FAILED
#
# A new ownership rule ships with a test here or a pattern in
# tests/leak/leak_patterns.jdb.

set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
cd "$repo"

jdb="build/jdBasic.exe"
[ -x "$jdb" ] || jdb="build/jdbasic"
rt="build/jdbrt.dll"
[ -f "$rt" ] || rt="build/libjdbrt.so"
if [ ! -x "$jdb" ] || [ ! -f "$rt" ]; then
    echo "memory check: no build in build/"
    echo "MEMCHECK FAILED"
    exit 1
fi

stale=""
for f in src/*.cpp src/*.h; do
    if [ "$f" -nt "$jdb" ] || [ "$f" -nt "$rt" ]; then stale="$stale $f"; fi
done
if [ -n "$stale" ]; then
    echo "memory check: build/ is older than:$stale"
    echo "rebuild the compiler and the runtime first (build.bat and build_rt.bat with the same flags)"
    echo "MEMCHECK FAILED"
    exit 1
fi

failed=0

echo "== leak check"
if ! bash tests/leak_check.sh > tmp_memcheck_leak.log 2>&1; then
    grep -E "NEW-LEAK|NO-OUTPUT|RESULT|failed" tmp_memcheck_leak.log
    failed=1
else
    tail -1 tmp_memcheck_leak.log
fi
rm -f tmp_memcheck_leak.log

tests=(
    tests/regression/test_rc_*.jdb
    tests/regression/test_udt_local_free.jdb
    tests/regression/test_text_ownership.jdb
    tests/regression/test_elementwise_cell_kind.jdb
    tests/regression/test_func_string_local.jdb
    tests/regression/native_handle_to_string.jdb
    tests/regression/test_stored_lambda_hof.jdb
    tests/regression/test_udt_method_cache.jdb
    tests/ffi/test_ffi_double.jdb
    tests/jdlibs/jdweb_selftest.jdb
)

echo "== ownership regressions, plain and under JDB_RC_POISON"
work="$(mktemp -d)"
for t in "${tests[@]}"; do
    [ -f "$t" ] || continue
    name="$(basename "${t%.jdb}")"
    exe="$work/$name.exe"
    if ! "$jdb" -c -o "$exe" "$t" > "$work/$name.compile" 2>&1 || [ ! -f "$exe" ]; then
        echo "  $name: COMPILE FAILED"
        tail -3 "$work/$name.compile"
        failed=1
        continue
    fi
    cp "$rt" "$work/" 2>/dev/null
    for mode in plain poison; do
        if [ "$mode" = poison ]; then
            JDB_RC_POISON=1 timeout 120 "$exe" > "$work/$name.$mode" 2>&1
        else
            timeout 120 "$exe" > "$work/$name.$mode" 2>&1
        fi
        code=$?
        if [ "$code" -ne 0 ] || grep -qE '(^|[[:space:]])FAIL([^A-Za-z]|$)|[1-9][0-9]* failed' "$work/$name.$mode"; then
            echo "  $name ($mode): exit $code"
            grep -E 'FAIL|failed|exception|error' "$work/$name.$mode" | head -3
            failed=1
        fi
    done
done
rm -rf "$work"

if [ "$failed" -ne 0 ]; then
    echo "MEMCHECK FAILED"
    exit 1
fi
echo "MEMCHECK PASSED"
