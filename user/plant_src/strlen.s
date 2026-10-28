.text
.globl strlen
strlen:
	xor %ecx, %ecx
.L:
	movzbl (%rdi), %eax
	test %eax, %eax
	jz .Ld
	add $1, %rdi
	add $1, %ecx
	jmp .L
.Ld:
	mov %ecx, %eax
	ret
