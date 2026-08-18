#!/bin/sh
# End-to-end regression test for the KERNEL target.
#
# Compiles the kernel/ sample programs with --target=kernel, links them into
# bootable images and boots each one under QEMU, comparing the serial console
# against the expected output.
#
# Environment-dependent, like tests/rag and tests/http: it needs a KERNEL build
# plus nasm, ld, objcopy and qemu-system-x86_64, so it is not part of the gate.
# Skips with exit 0 when the toolchain is absent, fails with exit 1 on a real
# mismatch.
#
#   tests/kernel/kernel_boot_test.sh [path/to/jdBasic.exe]

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
JDB=${1:-$ROOT/build/jdBasic.exe}
KDIR=$ROOT/kernel
BOOT_TIMEOUT=${BOOT_TIMEOUT:-15}

skip() { echo "SKIP: $1"; exit 0; }

[ -x "$JDB" ] || skip "no jdBasic at $JDB"

if ! "$JDB" --target=kernel 2>&1 | grep -qv 'needs a build with the KERNEL'; then
    skip "jdBasic was built without the KERNEL flag"
fi

# The bare-metal half runs under Linux. On Windows the driver hops into WSL.
if [ "$(uname -o 2>/dev/null)" = "Msys" ] || [ -n "$WINDIR" ]; then
    command -v wsl.exe >/dev/null 2>&1 || skip "no WSL to run the linker and QEMU"
    SH="wsl.exe -e sh -c"
    # Git-Bash reports /d/usr/..., a native shell D:\usr\...; WSL wants /mnt/d/usr/...
    WKDIR=$(echo "$KDIR" | tr '\\' '/' \
        | sed -e 's|^\([A-Za-z]\):|/\1|' \
              -e 's|^/\([A-Za-z]\)/|/mnt/\1/|' \
        | sed -e 's|^/mnt/\(.\)|/mnt/\L\1|')
else
    SH="sh -c"
    WKDIR=$KDIR
fi

for t in nasm ld objcopy qemu-system-x86_64; do
    $SH "command -v $t >/dev/null 2>&1" || skip "missing $t"
done

fail=0

check() {
    name=$1
    shift
    echo "--- $name ---"

    "$JDB" --target=kernel -o "$KDIR/$name.o" "$KDIR/$name.jdb" >/dev/null 2>&1 || {
        echo "FAIL: $name did not compile to a kernel object"
        fail=1
        return
    }

    $SH "cd '$WKDIR' && ./build_kernel.sh $name" >/dev/null 2>&1 || {
        echo "FAIL: $name did not link into an image"
        fail=1
        return
    }

    out=$($SH "cd '$WKDIR' && timeout $BOOT_TIMEOUT qemu-system-x86_64 -kernel $name.bin -display none -serial stdio -no-reboot" 2>/dev/null | tr -d '\r')

    for want in "$@"; do
        if echo "$out" | grep -qF "$want"; then
            echo "  ok: $want"
        else
            echo "  FAIL: expected '$want' on the serial console"
            fail=1
        fi
    done
}

# Integers, a loop and PRINT reach the screen with no OS underneath.
check hello \
    "jdBasic on bare metal" \
    "500500"

# Every SYS.* primitive round-trips, including a write straight into the VGA
# text buffer that is read back through SYS.PEEKW.
check sysio \
    "165" \
    "4660" \
    "305419896" \
    "3912" \
    "outb done"

if [ "$fail" = 0 ]; then
    echo "ALL KERNEL BOOT TESTS PASSED!"
else
    echo "KERNEL BOOT TESTS FAILED"
fi
exit $fail
