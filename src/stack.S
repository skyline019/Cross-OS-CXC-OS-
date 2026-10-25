    .text
    .globl switch_stack_and_call
    .def switch_stack_and_call
    .scl 2
    .type 32
    .endef
switch_stack_and_call:
    # Microsoft x64 arguments:
    #   RCX = new stack top, RDX = entry function, R8 = entry argument.
    # The entry function receives its argument in RCX and never returns.
    movq %rcx, %rsp
    andq $-16, %rsp
    xorl %ebp, %ebp
    cld
    movq %r8, %rcx
    subq $32, %rsp
    call *%rdx
    ud2
