.text
.globl __memrchr
.globl memrchr
__memrchr:
memrchr:
	test %rdx, %rdx
	jz .Ln
	add %rdx, %rdi
	mov %esi, %eax
	and $255, %eax
	mov %eax, %ecx
.L:
	sub $1, %rdi
	movzbl (%rdi), %eax
	cmp %ecx, %eax
	je .Lf
	sub $1, %rdx
	jnz .L
.Ln:
	xor %eax, %eax
	ret
.Lf:
	mov %rdi, %rax
	ret
