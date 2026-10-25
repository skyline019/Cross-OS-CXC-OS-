// Position-independent SMP startup blob. This file is assembled separately
// and converted to a flat binary; it is not linked into the PE image as code.
// The blob is copied to a SIPI page below 1 MiB. The following page contains
// parameters written by the BSP. EBP carries the code page physical base.

    .section .smp_trampoline,"ax"

    .equ data_page,       0x1000
    .equ data_cr3,        data_page + 0x10
    .equ data_cr4,        data_page + 0x14
    .equ data_efer_low,   data_page + 0x18
    .equ data_efer_high,  data_page + 0x1c
    .equ data_cr0,        data_page + 0x20
    .equ data_cpu_index,  data_page + 0x24
    .equ data_stack_top,  data_page + 0x28
    .equ data_high_entry, data_page + 0x30
    .equ temporary_stack, data_page + 0x800

    .code16
    .global smp_trampoline_start
smp_trampoline_start:
    cli
    cld

    // SIPI enters with CS = code_page >> 4 and IP = 0. Preserve the physical
    // code base in EBP before switching DS to the following parameter page.
    xorl %ebp, %ebp
    movw %cs, %bp
    shll $4, %ebp
    movw %cs, %ax
    addw $0x100, %ax
    movw %ax, %ds

    // The BSP wrote a six-byte GDTR at data+0 and a 32-bit far pointer at
    // data+8. Both reference read-only GDT/code in the SIPI page.
    lgdt 0x0000
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0

    // ljmpl *0x0008 with a 16-bit absolute address and DS=data segment.
    .byte 0x66, 0xff, 0x2e
    .word 0x0008

    // Fixed offsets are shared with smp.cpp. No runtime code patching or RWX
    // mapping is needed.
    .org 0x100
    .align 8
smp_trampoline_gdt:
    .quad 0x0000000000000000  // null
    .quad 0x00cf9a000000ffff  // 0x08: 32-bit code
    .quad 0x00cf92000000ffff  // 0x10: flat data
    .quad 0x00af9a000000ffff  // 0x18: 64-bit code

    .org 0x180
    .code32
smp_trampoline_protected:
    movl $0x10, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl data_cr4(%ebp), %eax
    movl %eax, %cr4
    movl data_cr3(%ebp), %eax
    movl %eax, %cr3

    movl $0xc0000080, %ecx  // IA32_EFER
    movl data_efer_low(%ebp), %eax
    movl data_efer_high(%ebp), %edx
    wrmsr

    // Paging starts with the low code/data pages identity mapped. The next
    // instruction therefore remains at the same physical address.
    movl data_cr0(%ebp), %eax
    movl %eax, %cr0

    leal temporary_stack(%ebp), %esp
    pushl $0x18
    leal smp_trampoline_long - smp_trampoline_start(%ebp), %eax
    pushl %eax
    lretl

    .code64
smp_trampoline_long:
    movl $0x10, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    cld

    movq data_stack_top(%rbp), %rsp
    andq $-16, %rsp
    subq $32, %rsp
    movl data_cpu_index(%rbp), %ecx
    movq data_high_entry(%rbp), %rax
    call *%rax
    ud2

    .global smp_trampoline_end
smp_trampoline_end:
