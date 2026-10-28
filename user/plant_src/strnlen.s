.text
.globl strnlen
strnlen:
	xor %ecx, %ecx
	test %rsi, %rsi
	jz .Ld
.L:
	movzbl (%rdi), %eax
	test %eax, %eax
	jz .Ld
	add $1, %rdi
	add $1, %ecx
	sub $1, %rsi
	jnz .L
.Ld:
	mov %ecx, %eax
	ret
