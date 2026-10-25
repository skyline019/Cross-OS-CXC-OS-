    .text

.macro ISR_NO_ERROR number
    .globl isr\number
    .def isr\number
    .scl 2
    .type 32
    .endef
isr\number:
    pushq $0
    pushq $\number
    jmp isr_common
.endm

.macro ISR_ERROR number
    .globl isr\number
    .def isr\number
    .scl 2
    .type 32
    .endef
isr\number:
    pushq $\number
    jmp isr_common
.endm

    ISR_NO_ERROR 0
    ISR_NO_ERROR 1
    ISR_NO_ERROR 2
    ISR_NO_ERROR 3
    ISR_NO_ERROR 4
    ISR_NO_ERROR 5
    ISR_NO_ERROR 6
    ISR_NO_ERROR 7
    ISR_ERROR    8
    ISR_NO_ERROR 9
    ISR_ERROR    10
    ISR_ERROR    11
    ISR_ERROR    12
    ISR_ERROR    13
    ISR_ERROR    14
    ISR_NO_ERROR 15
    ISR_NO_ERROR 16
    ISR_ERROR    17
    ISR_NO_ERROR 18
    ISR_NO_ERROR 19
    ISR_NO_ERROR 20
    ISR_ERROR    21
    ISR_NO_ERROR 22
    ISR_NO_ERROR 23
    ISR_NO_ERROR 24
    ISR_NO_ERROR 25
    ISR_NO_ERROR 26
    ISR_NO_ERROR 27
    ISR_NO_ERROR 28
    ISR_ERROR    29
    ISR_ERROR    30
    ISR_NO_ERROR 31
    ISR_NO_ERROR 32
    ISR_NO_ERROR 33
    ISR_NO_ERROR 34
    ISR_NO_ERROR 35
    ISR_NO_ERROR 36
    ISR_NO_ERROR 37
    ISR_NO_ERROR 38
    ISR_NO_ERROR 39
    ISR_NO_ERROR 40
    ISR_NO_ERROR 41
    ISR_NO_ERROR 42
    ISR_NO_ERROR 43
    ISR_NO_ERROR 44
    ISR_NO_ERROR 45
    ISR_NO_ERROR 46
    ISR_NO_ERROR 47
    ISR_NO_ERROR 48
    ISR_NO_ERROR 49
    ISR_NO_ERROR 50
    ISR_NO_ERROR 51
    ISR_NO_ERROR 128
    ISR_NO_ERROR 255

isr_common:
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

    # RCX is the first Microsoft x64 argument. R15 is non-volatile in that
    # ABI, so it retains the original frame pointer across the C++ call.
    movq %rsp, %r15
    movq %r15, %rcx
    andq $-16, %rsp
    subq $32, %rsp
    call interrupt_dispatch

    # The dispatcher may return a different saved context. Resume the common
    # epilogue on that frame so the final iretq performs the context switch.
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

    addq $16, %rsp  # Discard vector and error code.
    iretq

    .section .rdata,"dr"
    .align 8
    .globl isr_stub_table
isr_stub_table:
    .quad isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .quad isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

    .align 8
    .globl irq_stub_table
irq_stub_table:
    .quad isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    .quad isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
