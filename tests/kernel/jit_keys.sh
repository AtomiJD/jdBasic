#!/bin/sh
# Exercises the in-OS JIT: expressions, a variable shared with the
# interpreter, and a loop that has to backpatch its jumps.
#
# Key names are US positions; the guest's German layout maps them, so
# shift-minus is ?, shift-bracket_right is *, bracket_right is +, shift-0 is =,
# shift-comma is ;, less is < and alt_r-7 / alt_r-0 are the braces.
#
#   jit_keys.sh <dump-path>

DUMP=${1:-/tmp/jittest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.1
    done
}

line() {
    emit "$1"
    echo "sendkey ret"
    sleep 0.5
}

sleep 4

# ?? 6*7            compiled arithmetic
line "shift-minus shift-minus spc 6 shift-bracket_right 7"

# x = 5             set by the interpreter
line "x spc shift-0 spc 5"

# ?? x+1            the JIT reads the interpreter's variable
line "shift-minus shift-minus spc x bracket_right 1"

# ?? n = 0; while n < 10 { n = n+1 }; n     compiled loop with backpatching
line "shift-minus shift-minus spc n spc shift-0 spc 0 shift-comma spc w h i l e spc n spc less spc 1 0 spc alt_r-7 spc n spc shift-0 spc n bracket_right 1 spc alt_r-0 shift-comma spc n"

# ? n               the interpreter sees what the compiled loop left behind
line "shift-minus spc n"

sleep 0.5
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 0.8
echo quit
