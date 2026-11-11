#!/usr/bin/env bash
# Runs on the box. Starts the world server of a folder twice on spare ports,
# each on its own copy of the live state, and plays a round against it:
# once without a model, once with a model that cannot be reached.
#
#   box_preflight.sh <folder with server.jdb, game/, tools/> [patch file] [title file]
#
# With a patch file the patch is sent to the test server first, so it is
# tried on top of the patches the live world already has. Exit code 0 only
# when every step passed; the live service is not touched.
SRC=$1
PATCH=$2
TITLE=$3
JDB=/opt/wunschwelt/jdBasic
[ -x "$JDB" ] || JDB=$HOME/jdbasic-1.2/build/jdbasic
LIVE_STATE=/var/lib/wunschwelt
WORK=$(mktemp -d)
SERVER_PID=
trap '[ -n "$SERVER_PID" ] && kill $SERVER_PID 2>/dev/null; rm -rf "$WORK"' EXIT

run() {  # mode, port, model address
    local mode=$1 port=$2 rc=0
    # a port someone else holds would let the round run against a stranger,
    # and on Linux a second server starts on such a port without complaint
    command -v ss > /dev/null || { echo "preflight ($mode): no ss to look at the ports"; return 1; }
    if ss -ltn | grep -q ":$port "; then
        echo "preflight ($mode): port $port is in use"
        return 1
    fi
    mkdir "$WORK/$mode"
    if sudo -n test -d $LIVE_STATE; then
        sudo -n cp -r $LIVE_STATE/. "$WORK/$mode/"
        sudo -n chown -R "$(id -u):$(id -g)" "$WORK/$mode"
    fi
    WW_GAME_DIR=$SRC/game WW_STATE_DIR=$WORK/$mode WW_PATCH_KEY=preflight WW_LLM_URL=$3 \
        "$JDB" "$SRC/server.jdb" "$port" 60 > "$WORK/$mode.log" 2>&1 &
    SERVER_PID=$!
    sleep 2
    if ! kill -0 $SERVER_PID 2>/dev/null || ! grep -q "Wunschwelt on 127.0.0.1:$port," "$WORK/$mode.log"; then
        echo "preflight ($mode): the test server did not start on port $port"
        tail -3 "$WORK/$mode.log"
        kill $SERVER_PID 2>/dev/null
        SERVER_PID=
        return 1
    fi
    if grep -q "refused\|could not be read" "$WORK/$mode.log"; then
        echo "preflight ($mode): a stored patch or the saved world did not come back"
        rc=1
    fi
    if [ $rc -eq 0 ] && [ -n "$PATCH" ]; then
        WW_PATCH_KEY=preflight "$JDB" "$SRC/tools/patch.jdb" "$PATCH" "$(cat "$TITLE")" "http://127.0.0.1:$port" > "$WORK/patch.log" 2>&1 || rc=1
        [ $rc -eq 0 ] || { echo "preflight ($mode): the patch was $(tail -1 "$WORK/patch.log")"; }
    fi
    if [ $rc -eq 0 ]; then
        "$JDB" "$SRC/tools/preflight.jdb" "http://127.0.0.1:$port" "$mode" || rc=1
    fi
    kill $SERVER_PID 2>/dev/null
    SERVER_PID=
    [ $rc -eq 0 ] || { echo "server log:"; tail -5 "$WORK/$mode.log"; }
    return $rc
}

run reply 8121 "" && run failed 8122 http://127.0.0.1:9/none
