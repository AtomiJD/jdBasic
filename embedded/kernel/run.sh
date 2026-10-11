#!/bin/sh
# Boot a kernel image in QEMU.
#
#   ./run.sh                  boot hello.bin in a window
#   ./run.sh invaders         boot invaders.bin
#   ./run.sh -s hello         headless, serial only in this terminal
#   ./run.sh ~/dev/lallang/mein_os.iso
#   ./run.sh -l               list the images found here
#
# The argument may be a bare name from this directory, a path, or an .iso.
# Quit a windowed guest with Ctrl+Alt+Q, a headless one with Ctrl+C.

set -e

DIR=$(dirname "$0")
QEMU=${QEMU:-qemu-system-x86_64}
MEM=${MEM:-128M}
SERIAL_ONLY=0
TIMEOUT=${TIMEOUT:-0}

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

list_images() {
    echo "images in $DIR:"
    found=0
    for f in "$DIR"/*.bin "$DIR"/*.iso; do
        [ -e "$f" ] || continue
        printf '  %-24s %s\n' "$(basename "$f")" "$(du -h "$f" | cut -f1)"
        found=1
    done
    [ "$found" = 1 ] || echo "  (none - build one with ./build_kernel.sh <name>)"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        -s|--serial)  SERIAL_ONLY=1; shift ;;
        -t|--timeout) TIMEOUT=$2; shift 2 ;;
        -l|--list)    list_images ;;
        -h|--help)    usage ;;
        *)            break ;;
    esac
done

NAME=${1:-hello}

# Bare name, name.bin, or an explicit path all resolve here.
if   [ -f "$NAME" ];            then IMAGE="$NAME"
elif [ -f "$DIR/$NAME" ];       then IMAGE="$DIR/$NAME"
elif [ -f "$DIR/$NAME.bin" ];   then IMAGE="$DIR/$NAME.bin"
else
    echo "no image for '$NAME'" >&2
    if [ -f "$DIR/$NAME.o" ]; then
        echo "  $NAME.o exists - run ./build_kernel.sh $NAME first" >&2
    fi
    echo "  ./run.sh -l lists what is here" >&2
    exit 1
fi

# WSLg gives a window; without a display fall back to serial rather than fail.
if [ "$SERIAL_ONLY" = 0 ] && [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "(no display, using serial)" >&2
    SERIAL_ONLY=1
fi

case "$IMAGE" in
    *.iso) BOOT="-cdrom $IMAGE" ;;
    *)     BOOT="-kernel $IMAGE" ;;
esac

if [ "$SERIAL_ONLY" = 1 ]; then
    VIEW="-display none -serial stdio"
else
    VIEW="-serial mon:stdio"
fi

RUN="$QEMU $BOOT -m $MEM $VIEW -no-reboot"
[ "$TIMEOUT" != 0 ] && RUN="timeout $TIMEOUT $RUN"

echo "booting $IMAGE"
$RUN || true
