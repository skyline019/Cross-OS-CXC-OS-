.text
.globl strchr
strchr:
	mov %esi, %eax
	and $255, %eax
	mov %eax, %edx
.L:
	movzbl (%rdi), %eax
	cmp %edx, %eax
	je .Lf
	test %eax, %eax
	jz .Ln
	add $1, %rdi
	jmp .L
.Lf:
	mov %rdi, %rax
	ret
.Ln:
	xor %eax, %eax
	ret
