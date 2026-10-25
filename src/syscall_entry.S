    .text
    .globl syscall_entry
    .def syscall_entry
    .scl 2
    .type 32
    .endef
    # M66: SYSCALL lands here (RCX=user RIP, R11=RFLAGS, RSP=user).
    # Build the same InterruptFrame as int 0x80, then reuse interrupt_dispatch
    # and iretq (SYSRET left for a later slice).
    .extern syscall_kernel_rsp
syscall_entry:
    movq %rsp, syscall_user_rsp_scratch(%rip)
    movq syscall_kernel_rsp(%rip), %rsp

    pushq $0x1b
    pushq syscall_user_rsp_scratch(%rip)
    pushq %r11
    pushq $0x23
    pushq %rcx
    pushq $0
    pushq $0x80

    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %r15
    movq %r15, %rcx
    andq $-16, %rsp
    subq $32, %rsp
    call interrupt_dispatch

    movq %rax, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq

    .data
    .align 8
syscall_user_rsp_scratch:
    .quad 0
