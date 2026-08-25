#!/bin/sh
# Cut jdm.jdb into pieces the board can compile, in the order it can take them.
#
# The RP2350 throws std::bad_alloc long before its heap runs out, and the
# limit is structural rather than a byte count. Measured on a PicoCalc with
# 120376 free: P_SERIES alone (1559 bytes) compiles, P_LEGEND alone (576)
# compiles, the two together (2038) do not, while an unrelated 2848-byte
# demo does. Every load also costs about seven times its source permanently
# and leaves the heap more broken up, so a part that fits third may not fit
# fourth.
#
# Hence: one part per small group of functions, largest first. Each part
# carries only the globals its own bodies read - the compiler must see a
# name as global in the chunk that reads it, or the body binds a local
# instead - and pltinit.jdb sets the real defaults once everything is in.
#
# EXECUTE is not an option for chaining these. `EXECUTE TXTREADER$(part)`
# keeps the outer chunk, the source string and the tokens alive together
# and fails where a plain RUN of the same file succeeds.
#
# Usage: ./mkparts.sh [outdir]     (default: parts/)

set -e
here=$(dirname "$0")
src="$here/jdm.jdb"
out=${1:-"$here/parts"}
mkdir -p "$out"
: > "$out/LOAD.txt"

# Body without comments or blank lines: the lexer drops them anyway, and on
# the board they are the difference between fitting and not.
strip() {
    grep -v -E "^[[:space:]]*'" "$src" | grep -v -E "^[[:space:]]*$"
}

block() {
    strip | awk -v names="$1" '
        BEGIN { n = split(names, want, " "); for (i = 1; i <= n; i++) keep[want[i]] = 1 }
        /^(SUB|FUNC)[ ]/ {
            name = $2
            sub(/\(.*/, "", name)
            on = (name in keep)
        }
        on { print }
        /^(ENDSUB|ENDFUNC)$/ { on = 0 }
    '
}

# A branch that is never taken still registers its names: the compiler's
# global prepass walks into IF bodies before anything runs. A plain DIM
# would work too, but it also assigns NONE - and NONE compares equal to
# anything, so the part loaded after the defaults would quietly wipe them.
#
# One DIM per line, and it matters: `DIM a, b, c` registers only `a`.
# Measured on the board - a later chunk's function body then binds locals
# for b and c and reads NONE. That costs about fifty bytes a part over the
# comma form, which is enough to push P_SERIES over the compile cliff; it
# is the one part that does not currently load. Splitting P_SERIES into an
# outer loop and a per-point drawing SUB is the way out, and belongs in
# jdm.jdb rather than here.
part() {
    file="$out/$1"
    name="$1"
    shift
    dims="$1"
    shift
    {
        echo "IF 0 THEN"
        for d in $dims; do echo "DIM $d"; done
        echo "ENDIF"
    } > "$file"
    block "$*" >> "$file"
    printf 'RUN %s\n' "$name" >> "$out/LOAD.txt"
    printf '%-14s %5s bytes   %s\n' "$name" "$(wc -c < "$file")" "$1"
}

part plt1.jdb \
    "PX0  PY0  PX1  PY1  PXLO  PXHI  PYLO  PYHI  PTITLE\$  PXLAB\$  PYLAB\$  PAUTO  PXMIN  PXMAX  PYMIN  PYMAX  PLOGX  PLOGY  PNS  PSX  PSY  PSS" \
    "P_TRANS P_RANGE"

part plt2.jdb \
    "PNS  PSX  PSY  PSC  PSS  PLOGX  PLOGY  PX0  PY0  PX1  PY1  PXLO  PXHI  PYLO  PYHI" \
    "P_SERIES"

part plt3.jdb \
    "PSX  PSY  PSC  PSS  PSN  PNS  PBOARD  PWINOPEN  PWSCALE" \
    "PLOT PLOTXY PLOTADD PLOTADDXY PLOTSTYLE PLOTNAME PLOTR PLOTHELP P_CLEAR P_OPEN"

part plt4.jdb \
    "PXLAB\$  PYLAB\$  PGRID  PX0  PY0  PX1  PY1  PXLO  PXHI  PYLO  PYHI  PLOGX  PLOGY" \
    "P_GRID"

part plt5.jdb \
    "PCR  PCG  PCB  PBG" \
    "P_PEN P_INK P_CENTER P_TICK\$ P_VTEXT"

part plt6.jdb \
    "PNS  PSN  PSC  PLEG  PX0  PY0  PX1" \
    "P_LEGEND"

part plt7.jdb \
    "PBG  PTITLE\$  PX0  PY0  PX1  PY1" \
    "P_FRAME"

# First, and it has to be first: by the seventh load the heap is too broken
# up to compile even half a kilobyte, and the defaults are the one part that
# must not be the one that fails. Nothing loaded after it touches these
# values - the parts declare their globals in a branch that never runs.
cat > "$out/pltinit.jdb" <<'INIT'
PTITLE$ = ""
PXLAB$ = ""
PYLAB$ = ""
PLOGX = 0
PLOGY = 0
PGRID = 1
PLEG = 1
PBG = 0
PAUTO = 1
PXMIN = 0
PXMAX = 0
PYMIN = 0
PYMAX = 0
PSX = []
PSY = []
PSC = []
PSS = []
PSN = []
PNS = 0
PX0 = 0
PY0 = 0
PX1 = 0
PY1 = 0
PXLO = 0
PXHI = 0
PYLO = 0
PYHI = 0
PCR = [48, 80, 248, 248, 224, 248, 248, 96]
PCG = [252, 216, 232, 144, 96, 80, 248, 128]
PCB = [48, 248, 64, 48, 224, 80, 248, 248]
PBOARD = 1
PWINOPEN = 0
PWSCALE = 2
PFREE = 0
TRY
    PFREE = SYS.FREE()
CATCH
    PBOARD = 0
ENDTRY
PRINT "jdPlot ready - PLOTHELP lists the verbs"
INIT
printf '%-14s %5s bytes   %s\n' pltinit.jdb "$(wc -c < "$out/pltinit.jdb")" defaults
# Biggest first. The whole heap is only there for the first load, and by the
# last one half a kilobyte is a gamble - so the order is purely by size, and
# the declarations above are what make that safe: nothing a part loads can
# clobber a value another part already set.
ls -S "$out"/plt*.jdb 2>/dev/null | sed 's|.*/|RUN |' > "$out/LOAD.txt"
echo
echo "Send each file with the REPL's RECV, then paste $out/LOAD.txt at the prompt."
