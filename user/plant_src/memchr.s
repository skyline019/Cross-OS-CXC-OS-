.text
.globl memchr
memchr:
	test %rdx, %rdx
	jz .Ln
	mov %esi, %eax
	and $255, %eax
	mov %eax, %ecx
.L:
	movzbl (%rdi), %eax
	cmp %ecx, %eax
	je .Lf
	add $1, %rdi
	sub $1, %rdx
	jnz .L
.Ln:
	xor %eax, %eax
	ret
.Lf:
	mov %rdi, %rax
	ret
