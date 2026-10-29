.text
.globl strcmp
strcmp:
.L:
	movzbl (%rdi), %eax
	movzbl (%rsi), %ecx
	cmp %ecx, %eax
	jne .Ld
	test %eax, %eax
	jz .Lz
	add $1, %rdi
	add $1, %rsi
	jmp .L
.Ld:
	sub %ecx, %eax
	ret
.Lz:
	xor %eax, %eax
	ret
