#!/bin/sh
# Types a two-line program into the kernel editor and runs it with F5.
#
# Key names are US keyboard positions; the guest runs a German layout, so
# shift-0 is =, shift-comma is ;, less is <, alt_r-7 is { and alt_r-0 is }.
#
#   editor_keys.sh <dump-path>

DUMP=${1:-/tmp/editortest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.1
    done
}

sleep 4

# F2 at the prompt opens the editor.
echo "sendkey f2"
sleep 0.8

# n = 0
emit "n spc shift-0 spc 0"
echo "sendkey ret"
sleep 0.3

# while n < 4 { n = n+1 }
emit "w h i l e spc n spc less spc 4 spc alt_r-7 spc n spc shift-0 spc n bracket_right 1 spc alt_r-0"
echo "sendkey ret"
sleep 0.3

# ? n*10
emit "shift-minus spc n shift-bracket_right 1 0"
sleep 0.4

# Cursor keys have to reach the editor: go home, then to the end again.
echo "sendkey home"
sleep 0.2
echo "sendkey end"
sleep 0.2
echo "sendkey up"
sleep 0.2
echo "sendkey down"
sleep 0.4

echo "pmemsave 0xb8000 4000 \"$DUMP.edit\""
sleep 0.6

# F5 runs the buffer and waits for a key.
echo "sendkey f5"
sleep 1.5
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 0.6
echo quit
