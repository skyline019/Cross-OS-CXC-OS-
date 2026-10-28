.text
.globl strpbrk
strpbrk:
	push %rdi
	call strcspn
	pop %rdi
	add %rax, %rdi
	movzbl (%rdi), %eax
	test %eax, %eax
	jz .Ln
	mov %rdi, %rax
	ret
.Ln:
	xor %eax, %eax
	ret
