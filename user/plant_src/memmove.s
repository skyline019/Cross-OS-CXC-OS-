.text
.globl memmove
memmove:
	push %rdi
	test %rdx, %rdx
	jz .Ld
	cmp %rsi, %rdi
	jae .Lb
	mov %rdx, %rcx
	rep movsb
	jmp .Ld
.Lb:
	add %rdx, %rdi
	add %rdx, %rsi
.L:
	sub $1, %rsi
	sub $1, %rdi
	movzbl (%rsi), %eax
	mov %al, (%rdi)
	sub $1, %rdx
	jnz .L
.Ld:
	pop %rax
	ret
