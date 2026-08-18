#!/bin/sh
# Build a bootable image from a jdBasic source.
#
# Runs under WSL/Linux. The jdBasic object is produced on the Windows side with
#   jdBasic.exe --target=kernel -o <name>.o <name>.jdb
# and dropped into this directory; everything from there on is gcc/nasm/ld.
#
# Usage: ./build_kernel.sh <name>      (expects <name>.o to be present)

set -e

NAME=${1:-hello}
NASM=${NASM:-nasm}
CC=${CC:-gcc}
LD=${LD:-ld}
QEMU=${QEMU:-qemu-system-x86_64}

if [ ! -f "$NAME.o" ]; then
    echo "missing $NAME.o - compile it first with jdBasic.exe --target=kernel" >&2
    exit 1
fi

"$NASM" -f elf64 boot.asm -o boot.o

# Small code model: the image is linked into identity-mapped low memory, not
# the negative half that -mcmodel=kernel assumes. SSE stays on because every
# jdBasic double is passed in an xmm register.
"$CC" -c jdb_runtime_bare.c -o jdb_runtime_bare.o \
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -fno-pic \
    -mno-red-zone -O2 -Wall -Wextra

"$LD" -n -T kernel.ld -o "$NAME.elf64" boot.o jdb_runtime_bare.o "$NAME.o"

# Multiboot1 loaders, QEMU's -kernel included, only accept a 32-bit ELF. The
# payload stays 64-bit; only the container is rewritten.
objcopy -O elf32-i386 "$NAME.elf64" "$NAME.bin"

echo "built $NAME.bin"

if [ "$2" = "run" ]; then
    timeout 10 "$QEMU" -kernel "$NAME.bin" -display none \
        -serial stdio -no-reboot -no-shutdown || true
fi
