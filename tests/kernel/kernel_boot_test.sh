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

# The screen and keyboard driver is interactive, so it is driven through the
# QEMU monitor and read back out of the text buffer rather than the serial
# line. pmemsave needs the path quoted: the size argument is parsed as an
# expression and would otherwise swallow the leading slash as a division.
check_os() {
    echo "--- os ---"

    "$JDB" --target=kernel -o "$KDIR/os.o" "$KDIR/os.jdb" >/dev/null 2>&1 || {
        echo "FAIL: os did not compile to a kernel object"
        fail=1
        return
    }
    $SH "cd '$WKDIR' && ./build_kernel.sh os" >/dev/null 2>&1 || {
        echo "FAIL: os did not link into an image"
        fail=1
        return
    }

    # h a l l o on a German layout puts the US-Y key out as z; alt_r is AltGr.
    $SH "cd '$WKDIR' && rm -f /tmp/kbtest.bin && {
        sleep 4
        for k in h a l l o y; do echo \"sendkey \$k\"; sleep 0.25; done
        echo 'sendkey shift-a'; sleep 0.3
        echo 'sendkey alt_r-q'; sleep 0.3
        echo 'sendkey backspace'; sleep 0.3
        echo 'sendkey esc'; sleep 0.6
        echo 'pmemsave 0xb8000 4000 \"/tmp/kbtest.bin\"'; sleep 1
        echo quit
    } | timeout $((BOOT_TIMEOUT + 20)) qemu-system-x86_64 -kernel os.bin \
        -display none -monitor stdio -serial null >/dev/null 2>&1" || true

    out=$($SH "od -An -v -tu1 /tmp/kbtest.bin 2>/dev/null | tr -s ' ' '\n' | grep -v '^\$' | awk 'NR%2==1' | awk '{printf \"%c\", (\$1>=32 && \$1<127) ? \$1 : 32}' | tr -s ' '")

    for want in "screen and keyboard in jdBasic" "> hallozA" "keys typed: 8" "halted."; do
        if echo "$out" | grep -qF "$want"; then
            echo "  ok: $want"
        else
            echo "  FAIL: expected '$want' in the text buffer"
            fail=1
        fi
    done
}

check_os

if [ "$fail" = 0 ]; then
    echo "ALL KERNEL BOOT TESTS PASSED!"
else
    echo "KERNEL BOOT TESTS FAILED"
fi
exit $fail
