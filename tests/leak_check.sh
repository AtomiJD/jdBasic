#!/usr/bin/env bash
# Runs every pattern of tests/leak/leak_patterns.jdb in both backends and
# judges whether the C heap grows with the passes (SYS.HEAP after 1/3, 2/3
# and all of them). A pattern leaks when both thirds grow by more than half
# a byte per pass; a one-off bump that is given back does not count.
#
#   tests/leak_check.sh [rounds]        (default 20000)
#
# Exit 1 when a pattern leaks that tests/leak/known_leaks.txt does not list.
# A listed pattern that has gone flat is reported as FIXED, to be taken off
# the list.

set -u
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
jdb="$repo/build/jdBasic.exe"
[ -x "$jdb" ] || jdb="$repo/build/jdbasic"
[ -x "$jdb" ] || jdb="$repo/build/jdBasic"
src="$here/leak/leak_patterns.jdb"
known="$here/leak/known_leaks.txt"
rounds="${1:-20000}"

if [ ! -x "$jdb" ]; then
    echo "build/jdBasic is missing" >&2
    exit 1
fi

work="$repo/build/_leak_check"
rm -rf "$work"
mkdir -p "$work"
cp "$src" "$work/leak_patterns.jdb"
( cd "$work" && "$jdb" -c leak_patterns.jdb >"$work/compile.log" 2>&1 )
native=""
for cand in "$work/leak_patterns.exe" "$work/leak_patterns"; do
    [ -x "$cand" ] && native="$cand" && break
done
if [ -z "$native" ]; then
    echo "native compile failed:" >&2
    cat "$work/compile.log" >&2
    exit 1
fi

patterns=$(sed -n 's/^\(ELSE\)\{0,1\}IF pattern\$ = "\([a-z_]*\)" THEN.*/\2/p' "$src")
patterns="$patterns number"

is_known() {
    grep -qE "^$1[[:space:]]+$2([[:space:]]|\$)" "$known"
}

# judge <h1> <h2> <h3> -> "LEAK <bytes/pass>" or "flat"
judge() {
    local d1=$(( $2 - $1 )) d2=$(( $3 - $2 )) floor=$(( rounds / 2 ))
    if [ "$d1" -gt "$floor" ] && [ "$d2" -gt "$floor" ]; then
        echo "LEAK $(( ($3 - $1) / (2 * rounds) ))"
    else
        echo "flat"
    fi
}

# run_pattern <pattern> <backend> -> the HEAP line, or nothing
run_pattern() {
    if [ "$2" = interp ]; then
        ( cd "$work" && "$jdb" leak_patterns.jdb "$1" "$rounds" 2>&1 | grep '^HEAP ' )
    else
        ( cd "$work" && "$native" "$1" "$rounds" 2>&1 | grep '^HEAP ' )
    fi
}

new=0
printf '%-10s %-8s %-12s %s\n' pattern backend verdict "bytes/pass"
for p in $patterns; do
    for backend in interp native; do
        line=$(run_pattern "$p" "$backend")
        set -- $line
        if [ $# -ne 5 ]; then
            printf '%-10s %-8s %-12s\n' "$p" "$backend" "NO-OUTPUT"
            new=1
            continue
        fi
        read -r verdict per <<<"$(judge "$3" "$4" "$5")"
        # A leak grows on every run. A run that grows once can catch a
        # passing allocation in both windows (closurefunc did, now and then,
        # with a bump of ~23 KB that is given back by the next window), so
        # a growing pattern runs twice more and counts only when both grow.
        if [ "$verdict" = LEAK ]; then
            first="$3 $4 $5"
            for again in 1 2; do
                set -- $(run_pattern "$p" "$backend")
                if [ $# -ne 5 ] || [ "$(judge "$3" "$4" "$5" | cut -d' ' -f1)" != LEAK ]; then
                    verdict=flat
                    printf '%-10s %-8s %-12s heap %s once, flat on run %d\n' "$p" "$backend" "noise" "$first" "$((again + 1))"
                    break
                fi
            done
        fi
        if [ "$verdict" = LEAK ]; then
            if is_known "$backend" "$p"; then
                status="known"
            else
                status="NEW-LEAK"
                new=1
            fi
            printf '%-10s %-8s %-12s %s\n' "$p" "$backend" "$status" "$per"
        else
            status="ok"
            is_known "$backend" "$p" && status="FIXED"
            printf '%-10s %-8s %-12s\n' "$p" "$backend" "$status"
        fi
    done
done

rm -rf "$work"
if [ "$new" -ne 0 ]; then
    echo "RESULT: new leak (or a pattern that printed nothing)"
    exit 1
fi
echo "RESULT: no new leak"
