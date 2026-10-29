.text
.globl strspn
strspn:
	xor %edx, %edx
	xor %eax, %eax
	test %eax, %eax
	jnz .Lssp
.Lo:
	movzbl (%rdi), %eax
	test %eax, %eax
	jz .Ld
	push %rsi
.Li:
	movzbl (%rsi), %ecx
	test %ecx, %ecx
	jz .Lm
	cmp %ecx, %eax
	je .Lh
	add $1, %rsi
	jmp .Li
.Lh:
	pop %rsi
	add $1, %rdi
	add $1, %edx
	jmp .Lo
.Lm:
	pop %rsi
.Ld:
	mov %edx, %eax
	ret
.Lssp:
	call __stack_chk_fail
