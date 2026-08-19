#!/bin/sh
# Loads the game that was baked onto the RAM disk at boot, compiles it, plays
# it for a while and leaves with ESC.
#
# ESC ending the game is itself the proof that compiled code reads the
# keyboard: nothing else stops the loop while lives remain.
#
#   game_keys.sh <dump-path>

DUMP=${1:-/tmp/gametest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.08
    done
}

line() {
    emit "$1"
    echo "sendkey ret"
    sleep 0.6
}

sleep 4

# DIR shows what boot put there.
line "shift-d shift-i shift-r"
sleep 0.3
echo "pmemsave 0xb8000 4000 \"$DUMP.dir\""
sleep 0.5

line "shift-l shift-o shift-a shift-d spc p o n g"
line "shift-c shift-o shift-m shift-p"
sleep 0.3
echo "pmemsave 0xb8000 4000 \"$DUMP.comp\""
sleep 0.5

# CALL enters the game loop.
line "shift-c shift-a shift-l shift-l"
sleep 4

# Nudge the paddle, then look at the running screen.
for i in 1 2 3 4; do
    echo "sendkey a"
    sleep 0.4
done
sleep 2
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 0.6

# ESC is the only way out while lives remain.
echo "sendkey esc"
sleep 2
echo "pmemsave 0xb8000 4000 \"$DUMP.after\""
sleep 0.6
echo quit
