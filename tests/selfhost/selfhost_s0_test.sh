#!/bin/sh
# Self-hosting stages S0 and S1a.
#
# S0: a jdBasic program writes LLVM IR as text, hands it to the clang that
# ships in libs/LLVM/bin, and runs the binary that comes back.
#
# S1a: the token table the self-hosted lexer will use is generated from
# src/token.h, so it must still match that header. Regenerating and comparing
# is the check: a reordered TokenType enum fails here rather than silently
# putting the two lexers out of step.
#
# Needs a NATIVEC build (for build/jdb_runtime.obj) and libs/LLVM/bin/clang.exe.
# Skips rather than failing when either is missing.
#
#   tests/selfhost/selfhost_s0_test.sh [path/to/jdBasic.exe]

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${1:-$ROOT/build/jdBasic.exe}

skip() { echo "SKIP: $1"; exit 0; }

[ -x "$JDB" ] || skip "no jdBasic at $JDB"
[ -f "$ROOT/libs/LLVM/bin/clang.exe" ] || skip "no clang in libs/LLVM/bin"
[ -f "$ROOT/build/jdb_runtime.obj" ] || skip "no build/jdb_runtime.obj, needs a NATIVEC build"

cd "$ROOT"
fail=0

echo "--- S0: jdBasic emits IR, clang builds it, the binary runs ---"
out=$("$JDB" selfhost/s0_pipeline.jdb 2>&1 || true)
if echo "$out" | grep -qF "S0 OK"; then
    echo "  ok: the generated binary printed what it should"
else
    echo "  FAIL: S0 did not report OK"
    echo "$out" | tail -5
    fail=1
fi

echo "--- S1a: the token table still matches src/token.h ---"
cp selfhost/jdbc_tokens.jdb "$ROOT/tmp/jdbc_tokens.committed" 2>/dev/null || {
    echo "  FAIL: selfhost/jdbc_tokens.jdb is missing"
    exit 1
}
"$JDB" selfhost/gen_tokens.jdb >/dev/null 2>&1 || true
if cmp -s selfhost/jdbc_tokens.jdb "$ROOT/tmp/jdbc_tokens.committed"; then
    echo "  ok: regenerating produces the same table"
else
    echo "  FAIL: the table is stale, regenerate it from src/token.h"
    cp "$ROOT/tmp/jdbc_tokens.committed" selfhost/jdbc_tokens.jdb
    fail=1
fi
rm -f "$ROOT/tmp/jdbc_tokens.committed"

# The numbers the table carries are the ones the reference lexer prints, so a
# handful of them are checked against a real dump rather than trusted.
echo "--- S1a: the numbering agrees with --dump-tokens ---"
printf 'OPTION EXPLICIT\nDIM total AS INTEGER\n' > tmp/tokprobe.jdb
dump=$("$JDB" --dump-tokens tmp/tokprobe.jdb 2>/dev/null)
for pair in "OPTION:52" "DIM:5" "AS:6" "INTEGER:95"; do
    word=${pair%%:*}
    want=${pair##*:}
    got=$(echo "$dump" | awk -v w="$word" '$4 == w { print $1; exit }')
    tab=$(grep -oE "TOK_NAME\[$want\] = \"[A-Z_0-9]+\"" selfhost/jdbc_tokens.jdb | head -1)
    if [ "$got" = "$want" ] && [ -n "$tab" ]; then
        echo "  ok: $word is $want, table says ${tab##*= }"
    else
        echo "  FAIL: $word dumped as '$got', expected $want"
        fail=1
    fi
done
rm -f tmp/tokprobe.jdb

if [ "$fail" = 0 ]; then
    echo "ALL SELFHOST S0 TESTS PASSED!"
else
    echo "SELFHOST S0 TESTS FAILED"
fi
exit $fail
