.text
.globl strdup
strdup:
	push %rdi
	call strlen
	add $1, %rax
	push %rax
	mov %rax, %rdi
	call malloc
	pop %rdx
	pop %rsi
	test %rax, %rax
	jz .Lf
	mov %rax, %rdi
	push %rax
	call memcpy
	pop %rax
	ret
.Lf:
	ret
