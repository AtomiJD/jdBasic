#!/bin/sh
# S1 of the self-hosting plan: the lexer written in jdBasic must produce the
# same token stream as the one in C++.
#
# Comparison is byte for byte against `jdbasic --dump-tokens`, which prints the
# type, line, column and escaped text of every token. Agreement on a file means
# agreement on all four for every token in it.
#
# The gate suites are the routine corpus, which takes a few seconds. Set
# SELFHOST_FULL=1 to sweep every .jdb in the tree instead; that is the run that
# found the two defects this test exists to keep out.
#
#   tests/selfhost/selfhost_lexer_test.sh [path/to/jdBasic.exe]

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${1:-$ROOT/build/jdBasic.exe}

skip() { echo "SKIP: $1"; exit 0; }

[ -x "$JDB" ] || skip "no jdBasic at $JDB"
[ -f "$ROOT/selfhost/jdbc.jdb" ] || skip "no selfhost/jdbc.jdb"

cd "$ROOT"
mkdir -p tmp

if [ "$SELFHOST_FULL" = "1" ]; then
    corpus=$(find . -name '*.jdb' -not -path './tmp/*' -not -path './build/*')
    echo "--- full tree ---"
else
    corpus=$(ls tests/gate/*.jdb)
    echo "--- gate suites (SELFHOST_FULL=1 for the whole tree) ---"
fi

ok=0
bad=0
for f in $corpus; do
    "$JDB" --dump-tokens "$f" > tmp/lex_ref.txt 2>/dev/null || {
        # A file the reference itself will not lex says nothing about ours.
        continue
    }
    timeout 120 "$JDB" selfhost/jdbc.jdb --tokens "$f" > tmp/lex_mine.txt 2>/dev/null || true
    if cmp -s tmp/lex_ref.txt tmp/lex_mine.txt; then
        ok=$((ok + 1))
    else
        bad=$((bad + 1))
        if [ "$bad" -le 3 ]; then
            echo "  FAIL: $f"
            diff tmp/lex_ref.txt tmp/lex_mine.txt | head -4
        fi
    fi
done
rm -f tmp/lex_ref.txt tmp/lex_mine.txt

echo "  $ok identical, $bad differ"

if [ "$bad" = 0 ]; then
    echo "ALL SELFHOST LEXER TESTS PASSED!"
    exit 0
fi
echo "SELFHOST LEXER TESTS FAILED"
exit 1
