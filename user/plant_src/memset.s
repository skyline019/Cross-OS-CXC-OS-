.text
.globl memset
memset:
	push %rdi
	mov %esi, %eax
	and $255, %eax
	mov %rdx, %rcx
	test %rcx, %rcx
	jz .Ld
	rep stosb
.Ld:
	pop %rax
	ret
