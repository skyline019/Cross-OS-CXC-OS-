    .text
    .globl gdt_load
    .def gdt_load
    .scl 2
    .type 32
    .endef
gdt_load:
    # Microsoft x64 ABI: RCX points to the packed GDTR.
    lgdt (%rcx)

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    # Far return reloads CS without changing the original call stack.
    pushq $0x08
    leaq .Lgdt_done(%rip), %rax
    pushq %rax
    lretq
.Lgdt_done:
    ret
