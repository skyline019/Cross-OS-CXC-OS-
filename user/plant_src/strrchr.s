.text
.globl strrchr
strrchr:
	xor %ecx, %ecx
	mov %esi, %eax
	and $255, %eax
	mov %eax, %edx
.L:
	movzbl (%rdi), %eax
	cmp %edx, %eax
	jne .Ln
	mov %rdi, %rax
	mov %rax, %rcx
.Ln:
	test %eax, %eax
	jz .Ld
	add $1, %rdi
	jmp .L
.Ld:
	mov %rcx, %rax
	ret
