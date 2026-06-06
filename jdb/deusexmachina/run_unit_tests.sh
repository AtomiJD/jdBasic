#!/usr/bin/env bash
# Runs alle deusexmachina-Unit-Tests (ohne LLM-/RAG-Integration).
# Das Cwd-Wechseln in deusexmachina/ macht die data/-Pfade stabil und
# legt sqlitebridge.dll zur Hand.
set -e
cd "$(dirname "$0")"

EXE=../build/jdBasic.exe
[[ -x "$EXE" ]] || { echo "BUILD FIRST: $EXE missing"; exit 1; }

# sqlitebridge.dll muss neben dem Test-EXE liegen (LoadLibrary search)
cp -u ../bridges/sqlitebridge/sqlitebridge.dll . 2>/dev/null || true
mkdir -p data

ok=0; fail=0
for t in test_bus.jdb test_config.jdb test_persist.jdb; do
    echo "── $t ──"
    if "$EXE" "$t" 2>&1 | tail -5 | grep -q "ALL TESTS PASSED"; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "  ^^ FAILED"
    fi
done

echo ""
echo "── test_mcp.sh ──"
if ./test_mcp.sh 2>&1 | tail -3 | grep -q "ALL TESTS PASSED"; then
    ok=$((ok+1))
else
    fail=$((fail+1))
    echo "  ^^ FAILED"
fi

echo ""
echo "══════════════════════════════════════════"
echo " UNIT-SUITES: $ok ok, $fail failed"
echo "══════════════════════════════════════════"
[[ $fail -eq 0 ]] || exit 1
