#!/usr/bin/env bash
# Two matches in one room, with an admin reset between them. The second
# recording must contain only the second match's plays.
#
#   ./logreset_test.sh <path-to-server.jdb-source-dir>
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-$HERE}"
JDB="${JDB:-$HERE/../../../../build/jdBasic.exe}"
PORT=8099
BASE="http://127.0.0.1:$PORT"
# outside the repo: the server writes stats, replays and profiles as it runs
RUN="${TMPDIR:-/tmp}/royale_logreset_test"

# a server left over from an earlier run holds both the port and the directory.
# Killed by port, never by image name - other jdBasic processes are not ours.
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

for _ in $(seq 1 40); do
    curl -s -m 1 "$BASE/cards" > /dev/null 2>&1 && break
    sleep 0.5
done
if ! curl -s -m 2 "$BASE/cards" > /dev/null 2>&1; then
    echo "FAIL: server kam nicht hoch"; cat "$RUN/server.log"; exit 1
fi

jval() { grep -o "\"$2\":\"[^\"]*\"" <<< "$1" | head -1 | sed "s/\"$2\":\"//;s/\"$//"; }

login() {
    curl -s -m 5 -H 'Content-Type: application/json' \
        -d "{\"NAME\":\"$1\",\"PIN\":\"1234\"}" "$BASE/login"
}
join() {
    curl -s -m 5 -H 'Content-Type: application/json' \
        -d "{\"AUTH\":\"$1\",\"ROOM\":\"t\"}" "$BASE/join"
}
play() {
    curl -s -m 5 -H 'Content-Type: application/json' \
        -d "{\"ROOM\":\"t\",\"TOK\":\"$1\",\"KIND\":\"DROID\",\"X\":9,\"Y\":$2}" "$BASE/play"
}

AUTH_A=$(jval "$(login TestA)" AUTH)
AUTH_B=$(jval "$(login TestB)" AUTH)
[ -n "$AUTH_A" ] && [ -n "$AUTH_B" ] || { echo "FAIL: kein Login"; exit 1; }

echo "== Partie 1: drei Karten =="
TOK_A=$(jval "$(join "$AUTH_A")" TOK)
TOK_B=$(jval "$(join "$AUTH_B")" TOK)
[ -n "$TOK_A" ] && [ -n "$TOK_B" ] || { echo "FAIL: kein Join"; exit 1; }
sleep 5
play "$TOK_A" 20 > /dev/null
play "$TOK_B" 9 > /dev/null
sleep 5
play "$TOK_A" 21 > /dev/null

curl -s -m 5 -H 'Content-Type: application/json' \
     -d '{"KEY":"testkey","ROOM":"t"}' "$BASE/forceend" > /dev/null
sleep 1
curl -s -m 5 "$BASE/state?room=t&tok=$TOK_A" > /dev/null
sleep 1

echo "== Raum zuruecksetzen (der Pfad, der das Log stehen liess) =="
curl -s -m 5 "$BASE/reset?room=t&key=testkey" > /dev/null

echo "== Partie 2: eine Karte =="
TOK_A=$(jval "$(join "$AUTH_A")" TOK)
TOK_B=$(jval "$(join "$AUTH_B")" TOK)
sleep 5
play "$TOK_A" 20 > /dev/null

curl -s -m 5 -H 'Content-Type: application/json' \
     -d '{"KEY":"testkey","ROOM":"t"}' "$BASE/forceend" > /dev/null
sleep 1
curl -s -m 5 "$BASE/state?room=t&tok=$TOK_A" > /dev/null
sleep 1

kill "$SRVPID" 2>/dev/null
wait "$SRVPID" 2>/dev/null

echo
if [ ! -f "$RUN/replays.jsonl" ]; then
    echo "FAIL: keine replays.jsonl geschrieben"; tail -20 "$RUN/server.log"; exit 1
fi

N=$(wc -l < "$RUN/replays.jsonl")
echo "Aufzeichnungen: $N"
LAST=$(tail -1 "$RUN/replays.jsonl")
PLAYS=$(grep -o '"K":"[A-Z_]*"' <<< "$LAST" | wc -l)
TICKS=$(grep -o '"T":[0-9]*' <<< "$LAST" | sed 's/"T"://' | tr '\n' ' ')

echo "Zuege in der zweiten Aufzeichnung: $PLAYS"
echo "Tickfolge: $TICKS"

FAIL=0
[ "$N" -eq 2 ] || { echo "FAIL: zwei Aufzeichnungen erwartet, $N bekommen"; FAIL=1; }
[ "$PLAYS" -eq 1 ] || { echo "FAIL: die zweite Partie hatte einen Zug, aufgezeichnet sind $PLAYS"; FAIL=1; }

MONO=$(grep -o '"T":[0-9]*' <<< "$LAST" | sed 's/"T"://' | \
       awk 'NR>1 && $1<prev {bad=1} {prev=$1} END {print (bad?"NEIN":"ja")}')
[ "$MONO" = "ja" ] || { echo "FAIL: die Tickfolge springt zurueck"; FAIL=1; }

if [ "$FAIL" -eq 0 ]; then echo "OK - das Log der ersten Partie ist nicht in die zweite gelaufen"; fi
exit "$FAIL"
