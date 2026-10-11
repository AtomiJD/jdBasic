#!/bin/sh
# Writes a multi-line program in the editor, compiles the whole buffer with
# COMP and enters it with CALL.
#
# Key names are US positions; the guest's German layout maps them.
#
#   comp_keys.sh <dump-path>

DUMP=${1:-/tmp/comptest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.08
    done
}

line() {
    emit "$1"
    echo "sendkey ret"
    sleep 0.4
}

sleep 4

echo "sendkey f2"
sleep 0.8

# func sq(x) { return x*x }
line "f u n c spc s q shift-8 x shift-9 spc alt_r-7 spc r e t u r n spc x shift-bracket_right x spc alt_r-0"

# t = 0
line "t spc shift-0 spc 0"

# i = 1
line "i spc shift-0 spc 1"

# while i <= 5 { t = t + sq(i); i = i+1 }
line "w h i l e spc i spc less shift-0 spc 5 spc alt_r-7 spc t spc shift-0 spc t spc bracket_right spc s q shift-8 i shift-9 shift-comma spc i spc shift-0 spc i bracket_right 1 spc alt_r-0"

# t
emit "t"
sleep 0.4

echo "sendkey esc"
sleep 0.8

# COMP then CALL: 1+4+9+16+25 is 55
line "shift-c shift-o shift-m shift-p"
line "shift-c shift-a shift-l shift-l"

sleep 0.5
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 0.8
echo quit
