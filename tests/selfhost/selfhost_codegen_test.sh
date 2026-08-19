#!/bin/sh
# S3 of the self-hosting plan: what the compiler generates must behave the
# same as what the interpreter runs.
#
# For each fixture: jdbc.jdb emits LLVM IR, clang links it against the C++
# runtime, and the binary's output is compared against the interpreter's. The
# comparison is on behaviour rather than on the IR, because the IR is ours to
# choose and only the answer has to agree.
#
# Needs a NATIVEC build for build/jdb_runtime.obj and the clang in libs/LLVM.
#
#   tests/selfhost/selfhost_codegen_test.sh [path/to/jdBasic.exe]

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${1:-$ROOT/build/jdBasic.exe}
CLANG=$ROOT/libs/LLVM/bin/clang.exe

skip() { echo "SKIP: $1"; exit 0; }

[ -x "$JDB" ] || skip "no jdBasic at $JDB"
[ -f "$CLANG" ] || skip "no clang in libs/LLVM/bin"
[ -f "$ROOT/build/jdb_runtime.obj" ] || skip "no build/jdb_runtime.obj, needs a NATIVEC build"

cd "$ROOT"
mkdir -p tmp

ok=0
bad=0
for f in selfhost/fixtures/gen_*.jdb; do
    name=$(basename "$f" .jdb)

    "$JDB" selfhost/jdbc.jdb "$f" "tmp/$name.ll" > tmp/gen_msg.txt 2>&1
    if [ ! -s "tmp/$name.ll" ]; then
        echo "  FAIL: $name produced no IR"
        head -3 tmp/gen_msg.txt
        bad=$((bad + 1))
        continue
    fi

    rm -f "tmp/$name.exe"
    "$CLANG" "tmp/$name.ll" build/jdb_runtime.obj build/jdbrt.lib \
        -o "tmp/$name.exe" > tmp/clang_msg.txt 2>&1 || true
    if [ ! -f "tmp/$name.exe" ]; then
        echo "  FAIL: $name did not link"
        grep -v 'overriding the module target' tmp/clang_msg.txt | head -4
        bad=$((bad + 1))
        continue
    fi

    # A wrong codegen can loop forever, so the binary is given a deadline
    # rather than being allowed to hang the suite.
    timeout 20 "./tmp/$name.exe" > tmp/out_compiled.txt 2>&1 || true
    "$JDB" "$f" > tmp/out_interp.txt 2>&1 || true

    if cmp -s tmp/out_compiled.txt tmp/out_interp.txt; then
        ok=$((ok + 1))
        echo "  ok: $name"
    else
        bad=$((bad + 1))
        echo "  FAIL: $name, compiled output differs from interpreted"
        diff tmp/out_interp.txt tmp/out_compiled.txt | head -6
    fi
done
rm -f tmp/gen_msg.txt tmp/clang_msg.txt tmp/out_compiled.txt tmp/out_interp.txt

echo "  $ok agree, $bad differ"

if [ "$bad" = 0 ]; then
    echo "ALL SELFHOST CODEGEN TESTS PASSED!"
    exit 0
fi
echo "SELFHOST CODEGEN TESTS FAILED"
exit 1
