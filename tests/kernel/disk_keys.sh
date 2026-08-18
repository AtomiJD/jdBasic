#!/bin/sh
# Writes a program in the editor, stores it on the RAM disk, empties the
# buffer, lists the disk, then loads and runs it again.
#
# Key names are US positions; the guest's German layout maps them.
#
#   disk_keys.sh <dump-path>

DUMP=${1:-/tmp/disktest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.1
    done
}

line() {
    emit "$1"
    echo "sendkey ret"
    sleep 0.35
}

sleep 4

# Write two lines in the editor, then leave it.
echo "sendkey f2"
sleep 0.8
line "n spc shift-0 spc 7"
emit "shift-minus spc n bracket_right 5"
sleep 0.3
echo "sendkey esc"
sleep 0.8

# SAVE demo
line "shift-s shift-a shift-v shift-e spc d e m o"
# NEW empties the buffer, LIST then shows nothing
line "shift-n shift-e shift-w"
# DIR lists the stored name
line "shift-d shift-i shift-r"
sleep 0.3
echo "pmemsave 0xb8000 4000 \"$DUMP.dir\""
sleep 0.6

# RUN demo loads it back and executes it: 7 + 5 is 12
line "shift-r shift-u shift-n spc d e m o"
sleep 0.6
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 0.6
echo quit
