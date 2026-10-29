.text
.globl __memcpy_fwd
.globl memcpy
__memcpy_fwd:
memcpy:
	push %rdi
	mov %rdx, %rcx
	test %rcx, %rcx
	jz .Ld
	rep movsb
.Ld:
	pop %rax
	ret
