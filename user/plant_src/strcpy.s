.text
.globl strcpy
strcpy:
	push %rdi
.L:
	movzbl (%rsi), %eax
	mov %al, (%rdi)
	add $1, %rsi
	add $1, %rdi
	test %eax, %eax
	jnz .L
	pop %rax
	ret
