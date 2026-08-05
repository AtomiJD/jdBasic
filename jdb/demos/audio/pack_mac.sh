#!/usr/bin/env bash
# Pack the FX rack into a folder that can be double-clicked, plus the zip to
# hand someone who does not use a terminal.
#
#   ./pack_mac.sh [outdir]        default outdir: ./dist
#
# Expects jdbasic to be built already, with the audio and GUI features:
#
#   FX=1 MINIAUDIO=1 MIDI=1 GFX=1 IMGUI=1 HTTP=0 ./build.sh
#
# On macOS 10.15 the Command Line Tools carry a newer compiler and SDK than
# the Xcode that xcode-select points at, and SDL3 needs the newer one:
#
#   export DEVELOPER_DIR=/Library/Developer/CommandLineTools
#   export SDKROOT=$DEVELOPER_DIR/SDKs/MacOSX11.1.sdk
#   export CXX=$DEVELOPER_DIR/usr/bin/clang++
set -e

[ "$(uname -s)" = "Darwin" ] || { echo "this packs a macOS build - run it on the Mac"; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="${1:-$HERE/dist}"
NAME="jdPedal"
BIN="$ROOT/build/jdbasic"

[ -x "$BIN" ] || { echo "no $BIN - build it first, see the header of this script"; exit 1; }

# A build carrying stale objects links fine and silently lacks the features
# (build.sh keys its incremental compile off timestamps, not off the flags),
# so ask the binary itself what it can do before wrapping it up.
PROBE="${TMPDIR:-/tmp}/jdpack_probe_$$.jdb"
cat > "$PROBE" <<'EOF'
DIM fxOk = 1
TRY
    DIM ch = FX.NEW()
    DIM freed = FX.FREE(ch)
CATCH
    fxOk = 0
ENDTRY
DIM monOk = 1
TRY
    DIM d = MON.DEVICES()
CATCH
    monOk = 0
ENDTRY
DIM httpOk = 1
TRY
    HTTP.CLEARHEADERS()
CATCH
    httpOk = 0
ENDTRY
PRINT "FX="; fxOk; " MON="; monOk; " HTTP="; httpOk
EOF
CAPS="$("$BIN" "$PROBE" | tail -1)"
rm -f "$PROBE"
echo "runtime: $CAPS"
case "$CAPS" in
    *FX=1*) ;;
    *) echo "this jdbasic has no FX engine - the rack is nothing without it"; exit 1;;
esac
case "$CAPS" in
    *MON=1*) ;;
    *) echo "this jdbasic has no live monitor (MINIAUDIO) - it could not play a note"; exit 1;;
esac

rm -rf "$OUT/$NAME" "$OUT/$NAME-mac.zip"
mkdir -p "$OUT/$NAME"

cp "$BIN" "$OUT/$NAME/"
cp "$HERE/fx_rack.jdb" "$HERE/fx_effects.json" "$HERE/fx_presets.json" "$OUT/$NAME/"
# the tone designer needs HTTP; without it the rack hides the panel and the
# provider list would only be dead weight
case "$CAPS" in
    *HTTP=1*) cp "$HERE/fx_ai.json" "$OUT/$NAME/" ;;
esac

# The templates may arrive with CRLF from a Windows checkout, which a mac
# shell reads as part of the command. tr, not sed: BSD sed has no \r escape
# and would strip a trailing letter r instead.
tr -d '\r' < "$HERE/dist_mac/launcher.command" > "$OUT/$NAME/Pedalboard starten.command"
tr -d '\r' < "$HERE/dist_mac/LIESMICH.txt"     > "$OUT/$NAME/LIESMICH.txt"
chmod +x "$OUT/$NAME/Pedalboard starten.command" "$OUT/$NAME/jdbasic"

(cd "$OUT" && zip -qr "$NAME-mac.zip" "$NAME")

echo "packed: $OUT/$NAME-mac.zip ($(du -h "$OUT/$NAME-mac.zip" | cut -f1))"
echo "contents:"
unzip -l "$OUT/$NAME-mac.zip" | sed -n '4,$p' | head -12
