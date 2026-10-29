.text
.globl strcat
strcat:
	push %rdi
.F:
	movzbl (%rdi), %eax
	test %eax, %eax
	jz .C
	add $1, %rdi
	jmp .F
.C:
	movzbl (%rsi), %eax
	mov %al, (%rdi)
	add $1, %rsi
	add $1, %rdi
	test %eax, %eax
	jnz .C
	pop %rax
	ret
