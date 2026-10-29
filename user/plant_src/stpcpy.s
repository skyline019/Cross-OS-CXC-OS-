.text
.globl __stpcpy
.globl stpcpy
__stpcpy:
stpcpy:
.L:
	movzbl (%rsi), %eax
	mov %al, (%rdi)
	add $1, %rsi
	add $1, %rdi
	test %eax, %eax
	jnz .L
	sub $1, %rdi
	mov %rdi, %rax
	ret
