#!/bin/sh
# S5 of the self-hosting plan: the compiler is a fixpoint.
#
#   stage 1   the interpreter runs jdbc.jdb over jdbc.jdb
#   stage 2   the binary from stage 1 compiles jdbc.jdb
#   stage 3   the binary from stage 2 compiles jdbc.jdb
#
# stage 2 and stage 3 must be byte-identical: a compiler that translates its
# own source into a compiler that translates its own source the same way has
# nothing left to prove about that source. stage 1 is checked against them
# too, because here it is the same source running under the interpreter, so
# it has no licence to differ either.
#
# The compiled compiler is also run against the interpreted one over the
# corpus, in both of its reading modes, so a divergence is located at the
# stage that introduced it rather than only showing up as an IR diff.
#
# Needs a NATIVEC build for build/jdb_runtime.obj and the clang in libs/LLVM.
#
#   tests/selfhost/selfhost_bootstrap_test.sh [path/to/jdBasic.exe]

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${1:-$ROOT/build/jdBasic.exe}
CLANG=$ROOT/libs/LLVM/bin/clang.exe

skip() { echo "SKIP: $1"; exit 0; }

[ -x "$JDB" ] || skip "no jdBasic at $JDB"
[ -f "$CLANG" ] || skip "no clang in libs/LLVM/bin"
[ -f "$ROOT/build/jdb_runtime.obj" ] || skip "no build/jdb_runtime.obj, needs a NATIVEC build"
[ -f "$ROOT/selfhost/jdbc.jdb" ] || skip "no selfhost/jdbc.jdb"

cd "$ROOT"
mkdir -p tmp

SRC=selfhost/jdbc.jdb
bad=0

link() { # link <stage.ll> <stage.exe>
    rm -f "$2"
    "$CLANG" "$1" build/jdb_runtime.obj build/jdbrt.lib -o "$2" \
        > tmp/bs_clang.txt 2>&1 || true
    if [ ! -f "$2" ]; then
        echo "  FAIL: $1 did not link"
        grep -v 'overriding the module target' tmp/bs_clang.txt | head -6
        return 1
    fi
    return 0
}

echo "  stage 1: the interpreter compiles the compiler"
rm -f tmp/bs1.ll
timeout 600 "$JDB" "$SRC" "$SRC" tmp/bs1.ll > tmp/bs_msg.txt 2>&1 || true
if [ ! -s tmp/bs1.ll ]; then
    echo "  FAIL: stage 1 produced no IR"
    head -3 tmp/bs_msg.txt
    echo "SELFHOST BOOTSTRAP TESTS FAILED"
    exit 1
fi
link tmp/bs1.ll tmp/bs1.exe || { echo "SELFHOST BOOTSTRAP TESTS FAILED"; exit 1; }

echo "  stage 1 and the interpreter agree over the corpus"
for f in selfhost/fixtures/*.jdb selfhost/gen_tokens.jdb "$SRC"; do
    for mode in --tokens --ast; do
        timeout 300 "$JDB" "$SRC" $mode "$f" > tmp/bs_a.txt 2>&1 || true
        timeout 300 ./tmp/bs1.exe $mode "$f" > tmp/bs_b.txt 2>&1 || true
        if ! cmp -s tmp/bs_a.txt tmp/bs_b.txt; then
            echo "    FAIL: $mode $f"
            diff tmp/bs_a.txt tmp/bs_b.txt | head -4
            bad=$((bad + 1))
        fi
    done
done
[ "$bad" = 0 ] && echo "    ok"

echo "  stage 2: the compiler compiles itself"
rm -f tmp/bs2.ll
timeout 600 ./tmp/bs1.exe "$SRC" tmp/bs2.ll > tmp/bs_msg.txt 2>&1 || true
if [ ! -s tmp/bs2.ll ]; then
    echo "  FAIL: stage 2 produced no IR"
    head -3 tmp/bs_msg.txt
    echo "SELFHOST BOOTSTRAP TESTS FAILED"
    exit 1
fi
link tmp/bs2.ll tmp/bs2.exe || { echo "SELFHOST BOOTSTRAP TESTS FAILED"; exit 1; }

echo "  stage 3: and again"
rm -f tmp/bs3.ll
timeout 600 ./tmp/bs2.exe "$SRC" tmp/bs3.ll > tmp/bs_msg.txt 2>&1 || true
if [ ! -s tmp/bs3.ll ]; then
    echo "  FAIL: stage 3 produced no IR"
    head -3 tmp/bs_msg.txt
    echo "SELFHOST BOOTSTRAP TESTS FAILED"
    exit 1
fi

if cmp -s tmp/bs2.ll tmp/bs3.ll; then
    echo "  ok: stage 2 and stage 3 are byte-identical"
else
    echo "  FAIL: stage 2 and stage 3 differ, the compiler is not a fixpoint"
    diff tmp/bs2.ll tmp/bs3.ll | head -8
    bad=$((bad + 1))
fi

if cmp -s tmp/bs1.ll tmp/bs2.ll; then
    echo "  ok: stage 1 matches them too"
else
    echo "  FAIL: stage 1 differs from stage 2"
    diff tmp/bs1.ll tmp/bs2.ll | head -8
    bad=$((bad + 1))
fi

rm -f tmp/bs_clang.txt tmp/bs_msg.txt tmp/bs_a.txt tmp/bs_b.txt

if [ "$bad" = 0 ]; then
    echo "ALL SELFHOST BOOTSTRAP TESTS PASSED!"
    exit 0
fi
echo "SELFHOST BOOTSTRAP TESTS FAILED"
exit 1
