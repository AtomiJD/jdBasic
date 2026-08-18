#!/bin/sh
# Emits QEMU monitor commands that type three lines into the kernel REPL, then
# dump the VGA text buffer.
#
# Key names are US keyboard positions. The guest runs a German layout, so
# shift-minus is ?, shift-bracket_right is *, shift-7 is /, bracket_right is +,
# shift-0 is =, shift-comma is ;, less is <, alt_r-7 is { and alt_r-0 is }.
#
#   repl_keys.sh <dump-path>

DUMP=${1:-/tmp/repltest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.1
    done
    echo "sendkey ret"
    sleep 0.6
}

sleep 4

# ? 6*7
emit "shift-minus spc 6 shift-bracket_right 7"

# n = 0; while n < 10 { n = n+1 }; ? n
emit "n spc shift-0 spc 0 shift-comma spc w h i l e spc n spc less spc 1 0 spc alt_r-7 spc n spc shift-0 spc n bracket_right 1 spc alt_r-0 shift-comma spc shift-minus spc n"

# ? 1/0
emit "shift-minus spc 1 shift-7 0"

echo "sendkey esc"
sleep 0.6
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 1
echo quit
