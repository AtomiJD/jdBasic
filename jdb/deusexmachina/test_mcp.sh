#!/usr/bin/env bash
# deusexmachina / mcp_server — end-to-end test des --tools <dir>-Pfads.
# Pipes 4 JSON-RPC-Requests in den Server, parsed die Responses und
# prüft auf erwartete Strings. Lauft direkt aus dem Repo-Root.

set -e
cd "$(dirname "$0")/../.."  # repo root

EXE=build/jdBasic.exe
TOOLS_DIR=jdb/deusexmachina/tools
LOG=/tmp/deus_mcp_test.log

if [[ ! -x "$EXE" ]]; then
    echo "BUILD FIRST: $EXE missing"; exit 1
fi

echo "── deusexmachina MCP roundtrip ──"

REQ='{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"deus_echo","arguments":{"message":"hallo welt"}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"deus_health","arguments":{}}}'

OUT=$(printf '%s\n' "$REQ" | "$EXE" --mcp --tools "$TOOLS_DIR" 2>"$LOG")

PASS=0
FAIL=0

check() {
    local label="$1" needle="$2"
    if echo "$OUT" | grep -q "$needle"; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        echo "  FAIL: $label — needle '$needle' not in output"
    fi
}

check "initialize returns server name"      "jdbasic-mcp-stdio"
check "tools/list contains jdb_eval"        '"jdb_eval"'
check "tools/list contains deus_echo"       '"deus_echo"'
check "tools/list contains deus_health"     '"deus_health"'
check "tools/list contains deus_now"        '"deus_now"'
check "deus_echo replied with prefix"       "echo from deusexmachina: hallo welt"
check "deus_health JSON has server tag"     "deusexmachina"
check "deus_health JSON has phase A"        '\\"phase\\":\\"A\\"'

echo ""
echo "══════════════════════════════════════════"
echo " MCP USER-TOOLS: PASS=$PASS  FAIL=$FAIL"
echo "══════════════════════════════════════════"
if [[ $FAIL -eq 0 ]]; then
    echo " ALL TESTS PASSED"
    exit 0
else
    echo " SOME TESTS FAILED"
    echo " server log: $LOG"
    exit 1
fi
