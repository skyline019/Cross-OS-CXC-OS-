.text
.globl strncmp
strncmp:
	test %rdx, %rdx
	jz .Lz
.L:
	movzbl (%rdi), %eax
	movzbl (%rsi), %ecx
	cmp %ecx, %eax
	jne .Ld
	test %eax, %eax
	jz .Lz
	add $1, %rdi
	add $1, %rsi
	sub $1, %rdx
	jnz .L
.Lz:
	xor %eax, %eax
	ret
.Ld:
	sub %ecx, %eax
	ret
