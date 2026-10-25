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
    # Do not reload FS/GS: a MOV to those selectors loads the descriptor
    # base into the hidden cache and zeroes IA32_FS/GS_BASE (MSR). SMP and
    # the rest of the kernel keep per-CPU state in GS.base; TSS reloads of
    # the GDT must preserve it (M37: PF at CR2=0x8 in current_cpu_index).

    # Far return reloads CS without changing the original call stack.
    pushq $0x08
    leaq .Lgdt_done(%rip), %rax
    pushq %rax
    lretq
.Lgdt_done:
    ret

    .globl tss_load
    .def tss_load
    .scl 2
    .type 32
    .endef
tss_load:
    # RCX = selector (Microsoft x64); LTR only uses the low 16 bits.
    mov %cx, %ax
    ltr %ax
    ret

    .globl ring3_enter_iretq
    .def ring3_enter_iretq
    .scl 2
    .type 32
    .endef
    # RCX = user RIP, RDX = user RSP. Saves Microsoft x64 non-volatiles so
    # int 0x80 → kernel resume does not restore user RSI/RBX/… into the
    # caller's frame (M38: PF writing result.entered via user RSI=msg).
ring3_enter_iretq:
    cli
    pushq %rbx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    mov %rsp, ring3_saved_kernel_rsp(%rip)
    pushq $0x1b
    pushq %rdx
    pushq $0x2
    pushq $0x23
    pushq %rcx
    iretq

    .globl ring3_after_user
    .def ring3_after_user
    .scl 2
    .type 32
    .endef
ring3_after_user:
    # exit/probe rewrites the interrupt frame to land here with RSP equal to
    # ring3_saved_kernel_rsp (non-volatiles still pushed).
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rbx
    ret

    .data
    .align 8
    .globl ring3_saved_kernel_rsp
ring3_saved_kernel_rsp:
    .quad 0
