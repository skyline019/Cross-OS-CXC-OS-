.text
.globl __intscan
__intscan:
	push %rbp
	mov %rsp, %rbp
	sub $32, %rsp
	mov %rdi, %rax
	mov %rax, -8(%rbp)
	mov %esi, %eax
	test %eax, %eax
	jnz .Lb
	mov $10, %eax
.Lb:
	mov %eax, -12(%rbp)
	xor %eax, %eax
	mov %eax, -16(%rbp)
	mov %eax, -20(%rbp)
	mov -8(%rbp), %rax
	mov %rax, %rdi
	call __shgetc
	cmp $45, %eax
	jne .Lp
	mov $1, %eax
	mov %eax, -16(%rbp)
	jmp .Ln
.Lp:
	cmp $43, %eax
	jne .Lf0
.Ln:
	mov -8(%rbp), %rax
	mov %rax, %rdi
	call __shgetc
.Lf0:
	mov %eax, -24(%rbp)
.Lloop:
	mov -24(%rbp), %eax
	test %eax, %eax
	js .Ldone
	mov %eax, %edx
	sub $48, %edx
	cmp $9, %edx
	ja .Lback
	mov -20(%rbp), %eax
	imul $10, %eax
	add %edx, %eax
	mov %eax, -20(%rbp)
	mov -8(%rbp), %rax
	mov %rax, %rdi
	call __shgetc
	mov %eax, -24(%rbp)
	jmp .Lloop
.Lback:
	mov -8(%rbp), %rax
	mov %rax, %rdi
	mov (%rdi), %rax
	sub $1, %rax
	mov %rax, (%rdi)
.Ldone:
	mov -16(%rbp), %eax
	test %eax, %eax
	jz .Lget
	mov -20(%rbp), %eax
	neg %eax
	jmp .Lpos
.Lget:
	mov -20(%rbp), %eax
.Lpos:
	mov %rbp, %rsp
	pop %rbp
	ret
