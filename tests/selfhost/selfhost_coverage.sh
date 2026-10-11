#!/bin/sh
# S6 of the self-hosting plan: how much of the real tree the self-hosted
# compiler handles, and what stands in the way of the rest.
#
# The four gate suites are written in the whole language, and the compiler is
# written in jdBasic-0, so "run the gate through it" is not one step. This
# measures the distance instead, per file and per stage:
#
#   lex     the token stream matches jdbasic --dump-tokens
#   parse   the tree matches jdbasic --dump-ast
#   codegen the compiler produced IR for it without complaint
#
# and it counts, over every file that did not get through, the construct that
# stopped it. That histogram is the order to build things in.
#
# A file the reference itself cannot dump is reported as noref rather than as
# a failure: --dump-ast declines IMPORT ("no file reader"), so for those there
# is nothing to compare against.
#
# The per-file result is compared against tests/selfhost/coverage_baseline.tsv
# and any file that got worse fails the run. Reaching further is free; losing
# ground is not.
#
#   tests/selfhost/selfhost_coverage.sh [path ...]      default: tests/ and selfhost/
#   tests/selfhost/selfhost_coverage.sh --all           every tracked .jdb
#   tests/selfhost/selfhost_coverage.sh --update-baseline
#
# Writes tmp/coverage.tsv (one row per file) and prints the summary.

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${JDBASIC:-$ROOT/build/jdBasic.exe}

skip() { echo "SKIP: $1"; exit 0; }
[ -x "$JDB" ] || skip "no jdBasic at $JDB"
[ -f "$ROOT/selfhost/jdbc.jdb" ] || skip "no selfhost/jdbc.jdb"

cd "$ROOT"
mkdir -p tmp

BASELINE=tests/selfhost/coverage_baseline.tsv
update=0
case "$1" in
    --all)             FILES=$(git ls-files '*.jdb') ;;
    --update-baseline) update=1; FILES=$(git ls-files 'tests/*.jdb' 'selfhost/*.jdb') ;;
    "")                FILES=$(git ls-files 'tests/*.jdb' 'selfhost/*.jdb') ;;
    *)                 FILES=$(git ls-files "$@") ;;
esac

TSV=tmp/coverage.tsv
: > "$TSV"
: > tmp/cov_blockers.txt

n=0
lex_ok=0
parse_ok=0
parse_diff=0
parse_err=0
noref=0
cgen_ok=0

for f in $FILES; do
    n=$((n + 1))

    # lex
    "$JDB" --dump-tokens "$f" > tmp/cov_lref.txt 2>/dev/null || true
    timeout 300 "$JDB" selfhost/jdbc.jdb --tokens "$f" > tmp/cov_lmine.txt 2>/dev/null || true
    if [ -s tmp/cov_lref.txt ] && cmp -s tmp/cov_lref.txt tmp/cov_lmine.txt; then
        lex="ok"; lex_ok=$((lex_ok + 1))
    else
        lex="no"
    fi

    # parse
    "$JDB" --dump-ast "$f" > tmp/cov_aref.txt 2>tmp/cov_aerr.txt || true
    timeout 300 "$JDB" selfhost/jdbc.jdb --ast "$f" > tmp/cov_amine.txt 2>&1 || true
    why=""
    if [ ! -s tmp/cov_aref.txt ]; then
        parse="noref"; noref=$((noref + 1))
        why=$(head -1 tmp/cov_aerr.txt)
    elif cmp -s tmp/cov_aref.txt tmp/cov_amine.txt; then
        parse="ok"; parse_ok=$((parse_ok + 1))
    else
        first=$(head -1 tmp/cov_amine.txt)
        case "$first" in
            *"PARSE ERROR"*|*"Error #"*|*"LEX ERROR"*)
                parse="err"; parse_err=$((parse_err + 1)); why=$first ;;
            *)
                parse="diff"; parse_diff=$((parse_diff + 1))
                why=$(diff tmp/cov_aref.txt tmp/cov_amine.txt | sed -n '2p') ;;
        esac
    fi

    # codegen: accepted or not, the fixtures are what check the behaviour
    cgen="-"
    if [ "$parse" = "ok" ]; then
        rm -f tmp/cov.ll
        timeout 300 "$JDB" selfhost/jdbc.jdb "$f" tmp/cov.ll > tmp/cov_cmsg.txt 2>&1 || true
        if [ -s tmp/cov.ll ]; then
            cgen="ok"; cgen_ok=$((cgen_ok + 1))
        else
            cgen="no"
            why=$(head -1 tmp/cov_cmsg.txt)
        fi
    fi

    printf '%s\t%s\t%s\t%s\t%s\n' "$f" "$lex" "$parse" "$cgen" "$why" >> "$TSV"
    [ -n "$why" ] && echo "$why" >> tmp/cov_blockers.txt
done

rm -f tmp/cov_lref.txt tmp/cov_lmine.txt tmp/cov_aref.txt tmp/cov_amine.txt \
      tmp/cov_aerr.txt tmp/cov_cmsg.txt tmp/cov.ll

echo
echo "  files              $n"
echo "  lex agrees         $lex_ok"
echo "  parse agrees       $parse_ok"
echo "  parse differs      $parse_diff"
echo "  parse stops        $parse_err"
echo "  no reference tree  $noref"
echo "  codegen accepts    $cgen_ok"
echo
echo "  what stops it, most common first:"
sed -E 's/ at line [0-9]+.*//; s/^Parse error: Parse error: //; s/[0-9]+/N/g' tmp/cov_blockers.txt \
    | sort | uniq -c | sort -rn | head -20 | sed 's/^/   /'
echo
echo "  per-file rows in $TSV"

if [ "$update" = 1 ]; then
    cut -f1-4 "$TSV" > "$BASELINE"
    echo "  wrote $BASELINE"
    exit 0
fi

[ -f "$BASELINE" ] || { echo "  no baseline yet, --update-baseline to record one"; exit 0; }

# ok is further than anything else, and a file that cannot be compared at all
# still ranks above one whose tree disagrees.
worse=$(awk -F'\t' '
  function rank(stage, v) {
    if (stage == "lex")   return (v == "ok") ? 1 : 0
    if (stage == "parse") return (v == "ok") ? 3 : (v == "noref") ? 2 : (v == "diff") ? 1 : 0
    return (v == "ok") ? 2 : (v == "no") ? 1 : 0
  }
  NR == FNR { bl[$1] = $2 "\t" $3 "\t" $4; next }
  {
    if (!($1 in bl)) next
    split(bl[$1], b, "\t")
    if (rank("lex", $2) < rank("lex", b[1]) ||
        rank("parse", $3) < rank("parse", b[2]) ||
        rank("cgen", $4) < rank("cgen", b[3]))
      printf "    %s: %s/%s/%s was %s/%s/%s\n", $1, $2, $3, $4, b[1], b[2], b[3]
  }' "$BASELINE" "$TSV")

if [ -n "$worse" ]; then
    echo
    echo "  these got worse than the baseline:"
    echo "$worse"
    echo "SELFHOST COVERAGE REGRESSED"
    exit 1
fi

echo
echo "ALL SELFHOST COVERAGE AT OR ABOVE BASELINE!"
