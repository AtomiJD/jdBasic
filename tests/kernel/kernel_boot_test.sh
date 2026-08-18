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
TDIR=$ROOT/tests/kernel
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
    to_wsl() {
        echo "$1" | tr '\\' '/' \
            | sed -e 's|^\([A-Za-z]\):|/\1|' \
                  -e 's|^/\([A-Za-z]\)/|/mnt/\1/|' \
            | sed -e 's|^/mnt/\(.\)|/mnt/\L\1|'
    }
    WKDIR=$(to_wsl "$KDIR")
    WTDIR=$(to_wsl "$TDIR")
else
    SH="sh -c"
    WKDIR=$KDIR
    WTDIR=$TDIR
fi

for t in nasm ld objcopy qemu-system-x86_64; do
    $SH "command -v $t >/dev/null 2>&1" || skip "missing $t"
done

fail=0

# Renders a VGA text-buffer dump as characters, dropping the attribute byte of
# each cell. Coreutils only, so the test carries no python dependency.
text_of() {
    $SH "od -An -v -tu1 $1 2>/dev/null | tr -s ' ' '\n' | grep -v '^\$' | awk 'NR%2==1' | awk '{printf \"%c\", (\$1>=32 && \$1<127) ? \$1 : 32}' | tr -s ' '"
}

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

# The REPL is interactive too. The key sequence lives in repl_keys.sh so the
# monitor commands do not have to survive another level of shell quoting.
check_repl() {
    echo "--- repl ---"

    "$JDB" --target=kernel -o "$KDIR/repl.o" "$KDIR/repl.jdb" >/dev/null 2>&1 || {
        echo "FAIL: repl did not compile to a kernel object"
        fail=1
        return
    }
    $SH "cd '$WKDIR' && ./build_kernel.sh repl" >/dev/null 2>&1 || {
        echo "FAIL: repl did not link into an image"
        fail=1
        return
    }

    $SH "cd '$WKDIR' && rm -f /tmp/repltest.bin && '$WTDIR/repl_keys.sh' /tmp/repltest.bin | timeout $((BOOT_TIMEOUT + 45)) qemu-system-x86_64 -kernel repl.bin -display none -monitor stdio -serial null >/dev/null 2>&1" || true

    out=$(text_of /tmp/repltest.bin)

    for want in "a REPL written in jdBasic" "> ? 6*7 42" "; ? n 10" "error: division by zero"; do
        if echo "$out" | grep -qF "$want"; then
            echo "  ok: $want"
        else
            echo "  FAIL: expected '$want' in the text buffer"
            fail=1
        fi
    done
}

check_repl

# The editor shares the repl image: F2 at the prompt opens it, F5 runs the
# buffer. Two dumps are taken, one of the editor and one of the run output.
check_editor() {
    echo "--- editor ---"

    $SH "cd '$WKDIR' && rm -f /tmp/edtest.bin /tmp/edtest.bin.edit && '$WTDIR/editor_keys.sh' /tmp/edtest.bin | timeout $((BOOT_TIMEOUT + 60)) qemu-system-x86_64 -kernel repl.bin -display none -monitor stdio -serial null >/dev/null 2>&1" || true

    edit=$(text_of /tmp/edtest.bin.edit)
    ran=$(text_of /tmp/edtest.bin)

    for want in "while n < 4 { n = n+1 }" "line 3/3 col 7" "F5 run"; do
        if echo "$edit" | grep -qF "$want"; then
            echo "  ok: $want"
        else
            echo "  FAIL: expected '$want' on the editor screen"
            fail=1
        fi
    done

    # n counts to 4, so the buffer prints 40.
    if echo "$ran" | grep -qF "40"; then
        echo "  ok: F5 ran the buffer"
    else
        echo "  FAIL: F5 did not produce 40"
        fail=1
    fi
}

check_editor

# The RAM disk round trip: write in the editor, SAVE, NEW, DIR, then RUN by
# name, which has to load the program back before executing it.
check_disk() {
    echo "--- disk ---"

    $SH "cd '$WKDIR' && rm -f /tmp/dsktest.bin /tmp/dsktest.bin.dir && '$WTDIR/disk_keys.sh' /tmp/dsktest.bin | timeout $((BOOT_TIMEOUT + 90)) qemu-system-x86_64 -kernel repl.bin -display none -monitor stdio -serial null >/dev/null 2>&1" || true

    dir=$(text_of /tmp/dsktest.bin.dir)
    ran=$(text_of /tmp/dsktest.bin)

    for want in "saved demo" "new" "demo"; do
        if echo "$dir" | grep -qF "$want"; then
            echo "  ok: $want"
        else
            echo "  FAIL: expected '$want' after DIR"
            fail=1
        fi
    done

    # The buffer was emptied, so RUN demo has to load it back. 7 + 5 is 12.
    for want in "loaded demo" "12"; do
        if echo "$ran" | grep -qF "$want"; then
            echo "  ok: $want"
        else
            echo "  FAIL: expected '$want' after RUN"
            fail=1
        fi
    done
}

check_disk

if [ "$fail" = 0 ]; then
    echo "ALL KERNEL BOOT TESTS PASSED!"
else
    echo "KERNEL BOOT TESTS FAILED"
fi
exit $fail
