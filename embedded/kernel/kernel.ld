/* Link a jdBasic kernel image at the conventional multiboot load address.
   The multiboot header has to sit in the first 8 KiB for GRUB to find it. */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    .boot ALIGN(4K) : { *(.multiboot) }

    .text ALIGN(4K) : { *(.text .text.*) }

    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }

    .data ALIGN(4K) : { *(.data .data.*) }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
