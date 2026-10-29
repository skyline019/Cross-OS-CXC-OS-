.text
.globl labs
labs:
	mov %rdi, %rax
	test %rax, %rax
	jns .Lpos
	neg %rax
.Lpos:
	ret
