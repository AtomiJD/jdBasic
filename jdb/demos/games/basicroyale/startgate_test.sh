#!/usr/bin/env bash
# Nobody arrives at a match that is already running.
#
#   ./startgate_test.sh [path-to-server-source-dir]
#
# 1. the clock waits for the countdown, and the countdown waits for the second
#    seat, so a player sitting alone is never counted in
# 2. a rematch is an agreement: one press does not restart the room
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$HERE}"
JDB="${JDB:-$HERE/../../../../build/jdBasic.exe}"
PORT=8097
BASE="http://127.0.0.1:$PORT"
RUN="${TMPDIR:-/tmp}/royale_startgate_test"

for p in $(netstat -ano | grep ":$PORT " | grep LISTENING | awk '{print $NF}' | sort -u); do
    taskkill //F //PID "$p" > /dev/null 2>&1
done
sleep 1
rm -rf "$RUN"; mkdir -p "$RUN"
cp "$SRC/server.jdb" "$SRC/arena.jdb" "$SRC/cards.json" "$SRC/lang.json" "$RUN/"
printf 'testkey' > "$RUN/admin.txt"

( cd "$RUN" && "$JDB" server.jdb "$PORT" > server.log 2>&1 ) &
SRVPID=$!
trap 'kill "$SRVPID" 2>/dev/null' EXIT
for _ in $(seq 1 40); do curl -s -m 1 "$BASE/cards" > /dev/null 2>&1 && break; sleep 0.5; done
if ! curl -s -m 2 "$BASE/cards" > /dev/null 2>&1; then
    echo "FAIL: server kam nicht hoch"; cat "$RUN/server.log"; exit 1
fi

jval() { grep -o "\"$2\":\"[^\"]*\"" <<< "$1" | head -1 | sed "s/\"$2\":\"//;s/\"$//"; }
num()  { grep -o "\"$2\":[0-9.-]*" <<< "$1" | head -1 | sed "s/\"$2\"://"; }

login() { curl -s -m 5 -H 'Content-Type: application/json' -d "{\"NAME\":\"$1\",\"PIN\":\"1234\"}" "$BASE/login"; }
join()  { curl -s -m 5 -H 'Content-Type: application/json' -d "{\"AUTH\":\"$1\",\"ROOM\":\"g\"}" "$BASE/join"; }
state() { curl -s -m 5 "$BASE/state?room=g&tok=$1"; }
rematch() { curl -s -m 5 -H 'Content-Type: application/json' -d "{\"ROOM\":\"g\",\"TOK\":\"$1\"}" "$BASE/rematch"; }

FAIL=0
ok()   { echo "  ok   $1"; }
bad()  { echo "  FAIL $1"; FAIL=1; }

A=$(jval "$(login GateA)" AUTH)
B=$(jval "$(login GateB)" AUTH)

echo "== ein Spieler allein wird nicht eingezaehlt =="
TA=$(jval "$(join "$A")" TOK)
sleep 4
S=$(state "$TA")
C=$(num "$S" COUNT); T=$(num "$S" TICKNO)
echo "     COUNT=$C TICKNO=$T"
[ "$C" = "3" ] && ok "der Countdown steht bei 3" || bad "COUNT ist $C, erwartet 3"
[ "${T:-0}" = "0" ] && ok "die Uhr steht" || bad "TICKNO ist $T, erwartet 0"

echo "== mit dem zweiten Sitz laeuft der Countdown, die Uhr noch nicht =="
TB=$(jval "$(join "$B")" TOK)
sleep 1
S=$(state "$TA")
C=$(num "$S" COUNT); T=$(num "$S" TICKNO)
echo "     COUNT=$C TICKNO=$T"
awk -v c="$C" 'BEGIN{exit !(c>0 && c<3)}' && ok "der Countdown laeuft ($C s)" || bad "COUNT ist $C"
[ "${T:-0}" = "0" ] && ok "die Uhr steht noch" || bad "TICKNO ist $T, erwartet 0"

R=$(curl -s -m 5 -H 'Content-Type: application/json' \
    -d "{\"ROOM\":\"g\",\"TOK\":\"$TA\",\"KIND\":\"DROID\",\"X\":9,\"Y\":20}" "$BASE/play")
grep -q 'msg_count' <<< "$R" && ok "Karten sind waehrend des Countdowns gesperrt" || bad "play sagte: $R"

echo "== danach laeuft die Partie =="
sleep 3
S=$(state "$TA")
C=$(num "$S" COUNT); T=$(num "$S" TICKNO)
echo "     COUNT=$C TICKNO=$T"
[ "$C" = "0" ] && ok "der Countdown ist durch" || bad "COUNT ist $C, erwartet 0"
awk -v t="${T:-0}" 'BEGIN{exit !(t>0)}' && ok "die Uhr laeuft ($T Ticks)" || bad "TICKNO ist $T"

echo "== ein Rematch braucht beide =="
curl -s -m 5 -H 'Content-Type: application/json' -d '{"KEY":"testkey","ROOM":"g"}' "$BASE/forceend" > /dev/null
sleep 1
state "$TA" > /dev/null
S=$(state "$TA")
[ "$(jval "$S" PHASE)" = "over" ] && ok "die Partie ist beendet" || bad "Phase ist $(jval "$S" PHASE)"

R=$(rematch "$TA")
grep -q 'msg_rematchwait' <<< "$R" && ok "der erste Druck wartet" || bad "rematch sagte: $R"
sleep 3
S=$(state "$TA")
[ "$(jval "$S" PHASE)" = "over" ] && ok "der Raum bleibt beendet, solange nur einer will" \
                                  || bad "Phase ist $(jval "$S" PHASE), erwartet over"

R=$(rematch "$TB")
grep -q 'msg_rematchwait' <<< "$R" && bad "der zweite Druck haette starten muessen: $R" || ok "der zweite Druck startet"
sleep 1
S=$(state "$TA")
C=$(num "$S" COUNT)
[ "$(jval "$S" PHASE)" = "play" ] && ok "die neue Partie steht" || bad "Phase ist $(jval "$S" PHASE)"
awk -v c="${C:-0}" 'BEGIN{exit !(c>0)}' && ok "und faengt wieder mit dem Countdown an ($C s)" || bad "COUNT ist $C"

echo "== wer nicht im Raum sitzt, startet auch nichts =="
R=$(curl -s -m 5 -H 'Content-Type: application/json' -d '{"ROOM":"g","TOK":"nicht-mein-token"}' "$BASE/rematch")
grep -q 'msg_notseated' <<< "$R" && ok "fremder Token wird abgewiesen" || bad "rematch sagte: $R"

kill "$SRVPID" 2>/dev/null
wait "$SRVPID" 2>/dev/null
echo
if [ "$FAIL" -eq 0 ]; then echo "startgate_test: alles gruen"; else echo "startgate_test: rot"; fi
exit "$FAIL"
