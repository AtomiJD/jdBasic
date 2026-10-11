#!/bin/sh
# Compiled user functions: a definition and a call on one line, then recursion
# across lines.
#
# Key names are US positions; the guest's German layout maps them, so
# shift-minus is ?, shift-8 / shift-9 are the parens, alt_r-7 / alt_r-0 the
# braces, slash is -, bracket_right is +, less is < and shift-comma is ;.
#
#   jitfn_keys.sh <dump-path>

DUMP=${1:-/tmp/jitfntest.bin}

emit() {
    for k in $1; do
        echo "sendkey $k"
        sleep 0.08
    done
}

line() {
    emit "$1"
    echo "sendkey ret"
    sleep 0.7
}

sleep 4

# ?? func dbl(x) { return x*2 }; dbl(21)
line "shift-minus shift-minus spc f u n c spc d b l shift-8 x shift-9 spc alt_r-7 spc r e t u r n spc x shift-bracket_right 2 spc alt_r-0 shift-comma spc d b l shift-8 2 1 shift-9"

# ?? func fib(n) { if n < 2 { return n } return fib(n-1) + fib(n-2) }
line "shift-minus shift-minus spc f u n c spc f i b shift-8 n shift-9 spc alt_r-7 spc i f spc n spc less spc 2 spc alt_r-7 spc r e t u r n spc n spc alt_r-0 spc r e t u r n spc f i b shift-8 n slash 1 shift-9 spc bracket_right spc f i b shift-8 n slash 2 shift-9 spc alt_r-0"

# ?? fib(20)      the definition survives the line that made it
line "shift-minus shift-minus spc f i b shift-8 2 0 shift-9"

# ?? dbl(fib(10))  a call inside a call
line "shift-minus shift-minus spc d b l shift-8 f i b shift-8 1 0 shift-9 shift-9"

sleep 0.5
echo "pmemsave 0xb8000 4000 \"$DUMP\""
sleep 0.8
echo quit
