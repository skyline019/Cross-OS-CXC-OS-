.text
.globl __strchrnul
.globl strchrnul
__strchrnul:
strchrnul:
	mov %esi, %eax
	and $255, %eax
	mov %eax, %edx
.L:
	movzbl (%rdi), %eax
	cmp %edx, %eax
	je .Lf
	test %eax, %eax
	jz .Lf
	add $1, %rdi
	jmp .L
.Lf:
	mov %rdi, %rax
	ret
