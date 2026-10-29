.text
.globl __shlim
.globl __shgetc
__shlim:
	ret
__shgetc:
	mov (%rdi), %rax
	movzbl (%rax), %ecx
	test %ecx, %ecx
	jz .Leof
	add $1, %rax
	mov %rax, (%rdi)
	mov %ecx, %eax
	ret
.Leof:
	call __uflow
	ret
