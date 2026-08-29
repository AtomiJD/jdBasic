; Multiboot entry and long-mode bring-up for a jdBasic kernel image.
;
; GRUB hands control over in 32-bit protected mode with paging off. This sets
; up identity-mapped paging for the first gigabyte, switches to long mode and
; calls the compiled program's main().

bits 32

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 4096
pml4:          resb 4096
pdpt:          resb 4096
pd:            resb 4096
align 16
stack_bottom:  resb 65536
stack_top:

section .text
global _start
extern main

_start:
    cli
    mov esp, stack_top

    ; The multiboot spec does not promise a zeroed .bss for ELF images, and a
    ; stale bit in a page table entry faults on the first translation.
    mov edi, pml4
    mov ecx, 3072
    xor eax, eax
    rep stosd

    mov eax, pdpt
    or  eax, 0x03
    mov [pml4], eax

    mov eax, pd
    or  eax, 0x03
    mov [pdpt], eax

    ; 512 entries of 2 MiB each, present + writable + huge
    xor ecx, ecx
.map_pd:
    mov eax, 0x200000
    mul ecx
    or  eax, 0x83
    mov [pd + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_pd

    mov eax, pml4
    mov cr3, eax

    ; PAE, plus OSFXSR and OSXMMEXCPT so SSE2 is usable. Every jdBasic double
    ; lands in an xmm register, so this must happen before any compiled code.
    mov eax, cr4
    or  eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    ; EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 1 << 8
    wrmsr

    ; CR0.PG
    mov eax, cr0
    or  eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode

section .rodata
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
bits 64
long_mode:
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top

    ; main(argc, argv) under SysV
    xor edi, edi
    xor esi, esi
    call main

.halt:
    cli
    hlt
    jmp .halt
