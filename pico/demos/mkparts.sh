#!/bin/sh
# Cut jdm.jdb into pieces small enough for the board to compile.
#
# The RP2350 dies with std::bad_alloc somewhere above 3 KB of source in one
# chunk: the lexer's token vector and the syntax tree are alive at the same
# time, and the heap cannot serve a block that large however much of it is
# free. Measured on a PicoCalc with 117 KB free: 2848 bytes compiles, 3540
# does not.
#
# So each part carries only the globals its own functions touch - the
# compiler must see a name as global in the chunk that reads it, or the
# function body binds a local instead - and jdm_boot.jdb walks the parts
# with EXECUTE, one parse peak at a time, then sets the defaults.
#
# Usage: ./mkparts.sh [outdir]     (default: parts/)

set -e
here=$(dirname "$0")
src="$here/jdm.jdb"
out=${1:-"$here/parts"}
mkdir -p "$out"

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

part() {
    file="$out/$1"
    shift
    dims="$1"
    shift
    printf 'DIM %s\n' "$dims" > "$file"
    block "$*" >> "$file"
    printf '%-20s %s bytes\n' "$1" "$(wc -c < "$file")"
}

part jdmp1.jdb \
    "PSX, PSY, PSC, PSS, PSN, PNS, PBOARD, PWINOPEN, PWSCALE" \
    "PLOT PLOTXY PLOTADD PLOTADDXY PLOTSTYLE PLOTNAME PLOTR PLOTHELP P_CLEAR P_OPEN"

part jdmp2.jdb \
    "PX0, PY0, PX1, PY1, PXLO, PXHI, PYLO, PYHI, PTITLE\$, PXLAB\$, PYLAB\$, PAUTO, PXMIN, PXMAX, PYMIN, PYMAX, PLOGX, PLOGY, PNS, PSX, PSY, PSS" \
    "P_TRANS P_RANGE"

part jdmp3.jdb \
    "PCR, PCG, PCB, PBG" \
    "P_PEN P_INK P_CENTER P_TICK\$ P_VTEXT"

part jdmp4.jdb \
    "PBG, PTITLE\$, PXLAB\$, PYLAB\$, PGRID, PX0, PY0, PX1, PY1, PXLO, PXHI, PYLO, PYHI, PLOGX, PLOGY" \
    "P_FRAME P_GRID"

part jdmp5.jdb \
    "PNS, PSX, PSY, PSC, PSS, PSN, PLOGX, PLOGY, PX0, PY0, PX1, PY1, PXLO, PXHI, PYLO, PYHI, PLEG" \
    "P_SERIES P_LEGEND"

# The defaults come last: a part that DIMs a name it reads leaves it NONE,
# and NONE compares equal to anything, so a caption test would misfire.
cat > "$out/jdm_boot.jdb" <<'BOOT'
EXECUTE TXTREADER$("jdmp1.jdb")
EXECUTE TXTREADER$("jdmp2.jdb")
EXECUTE TXTREADER$("jdmp3.jdb")
EXECUTE TXTREADER$("jdmp4.jdb")
EXECUTE TXTREADER$("jdmp5.jdb")
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
BOOT
printf '%-20s %s bytes\n' jdm_boot.jdb "$(wc -c < "$out/jdm_boot.jdb")"
