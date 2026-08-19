#!/bin/sh
# S2 of the self-hosting plan: the parser written in jdBasic must build the
# same tree as the one in C++.
#
# Comparison is byte for byte against `jdbasic --dump-ast`, which prints every
# node with its kind, line and the fields that differ from their default.
#
# The corpus is the part of the tree that is written in jdBasic-0, the subset
# the compiler itself uses: the fixtures, the lexer, and the parser's own
# source. That last one is the milestone; the parser parses itself.
#
#   tests/selfhost/selfhost_parser_test.sh [path/to/jdBasic.exe]

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${1:-$ROOT/build/jdBasic.exe}

skip() { echo "SKIP: $1"; exit 0; }

[ -x "$JDB" ] || skip "no jdBasic at $JDB"
[ -f "$ROOT/selfhost/jdbc_parse.jdb" ] || skip "no selfhost/jdbc_parse.jdb"

cd "$ROOT"
mkdir -p tmp

corpus="$(ls selfhost/fixtures/*.jdb) selfhost/jdbc_lex.jdb selfhost/jdbc_parse.jdb"

ok=0
bad=0
for f in $corpus; do
    "$JDB" --dump-ast "$f" > tmp/ast_ref.txt 2>/dev/null || {
        echo "  FAIL: the reference could not parse $f"
        bad=$((bad + 1))
        continue
    }
    timeout 300 "$JDB" selfhost/jdbc_parse.jdb "$f" > tmp/ast_mine.txt 2>/dev/null || true
    if cmp -s tmp/ast_ref.txt tmp/ast_mine.txt; then
        ok=$((ok + 1))
        echo "  ok: $f"
    else
        bad=$((bad + 1))
        echo "  FAIL: $f"
        diff tmp/ast_ref.txt tmp/ast_mine.txt | head -6
    fi
done
rm -f tmp/ast_ref.txt tmp/ast_mine.txt

echo "  $ok identical, $bad differ"

if [ "$bad" = 0 ]; then
    echo "ALL SELFHOST PARSER TESTS PASSED!"
    exit 0
fi
echo "SELFHOST PARSER TESTS FAILED"
exit 1
