.text
.globl strtol
strtol:
	push %rbp
	mov %rsp, %rbp
	sub $32, %rsp
	mov %rdi, %rax
	mov %rax, -8(%rbp)
	mov %rsi, %rax
	mov %rax, -16(%rbp)
	mov %rdx, %rax
	mov %rax, -24(%rbp)
	lea -8(%rbp), %rax
	mov %rax, %rdi
	xor %esi, %esi
	call __shlim
	lea -8(%rbp), %rax
	mov %rax, %rdi
	mov -24(%rbp), %rax
	mov %rax, %rsi
	mov $1, %edx
	mov $-1, %ecx
	call __intscan
	push %rax
	mov -16(%rbp), %rax
	test %rax, %rax
	jz .Lout
	mov %rax, %rdi
	mov -8(%rbp), %rax
	mov %rax, (%rdi)
.Lout:
	pop %rax
	mov %rbp, %rsp
	pop %rbp
	ret
