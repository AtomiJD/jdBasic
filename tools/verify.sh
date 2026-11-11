#!/usr/bin/env bash
# verify.sh - one command after a change: build what is out of date, then
# run the gate with the GUI smokes.
#
#   tools/verify.sh [--build] [--no-smokes] [--parity]
#
# Run from anywhere in the repository. The interpreter and the runtime
# library are each rebuilt when a source under src/ is newer than them, or
# both with --build; a change to .jdb files alone builds nothing. Then
# tools/gate_run.jdb runs the four suites on both backends and the smokes,
# one GATE {...} line per step, and stamps the marker the commit hook reads
# when all of it is green. Exit 0 = green, 1 = a gate step failed, 2 = the
# build failed.
#
# Build flags on Windows: JDB_BUILD_FLAGS, default
# "HTTP NET COM GFX IMGUI NATIVEC MCPSERVER". Elsewhere ./build.sh runs
# with the environment as it is.
set -u
cd "$(dirname "$0")/.."

force=0
smokes=--smokes
parity=
for arg in "$@"; do
    case "$arg" in
        --build) force=1 ;;
        --no-smokes) smokes= ;;
        --parity) parity=--parity ;;
        *) echo "verify.sh: unknown argument $arg" >&2; exit 2 ;;
    esac
done

# True when the file is missing or a source is newer than it.
out_of_date() {
    [ ! -f "$1" ] && return 0
    [ -n "$(find src -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -newer "$1" -print -quit)" ]
}

# Runs one build script; a locked binary gets a second try.
build_step() {
    local log
    log=$("$@" 2>&1)
    if ! printf '%s\n' "$log" | grep -q "BUILD OK"; then
        sleep 5
        log=$("$@" 2>&1)
    fi
    if printf '%s\n' "$log" | grep -q "BUILD OK"; then
        echo "VERIFY built with $1"
        return 0
    fi
    printf '%s\n' "$log" | tail -15
    echo "VERIFY $1 did not end with BUILD OK"
    return 1
}

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        jdb=build/jdBasic.exe
        read -r -a flags <<< "${JDB_BUILD_FLAGS:-HTTP NET COM GFX IMGUI NATIVEC MCPSERVER}"
        if [ "$force" = 1 ] || out_of_date build/jdBasic.exe; then
            build_step ./build.bat "${flags[@]}" || exit 2
        fi
        if [ "$force" = 1 ] || out_of_date build/jdbrt.dll; then
            build_step ./build_rt.bat "${flags[@]}" || exit 2
        fi
        ;;
    *)
        jdb=build/jdBasic
        if [ "$force" = 1 ] || out_of_date "$jdb"; then
            ./build.sh || { echo "VERIFY build.sh failed"; exit 2; }
        fi
        ;;
esac

"$jdb" --version | sed -n 's/^Features: */VERIFY features: /p'
# unquoted: an option that is off leaves no argument behind
"$jdb" tools/gate_run.jdb $smokes $parity
