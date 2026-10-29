.text
.globl atoi
atoi:
	xor %eax, %eax
.L:
	movzbl (%rdi), %edx
	sub $48, %rdx
	cmp $9, %rdx
	ja .D
	imul $10, %eax
	add %edx, %eax
	add $1, %rdi
	jmp .L
.D:
	ret
