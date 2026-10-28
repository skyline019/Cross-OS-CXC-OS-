.text
.globl llabs
llabs:
	mov %rdi, %rax
	test %rax, %rax
	jns .Lpos
	neg %rax
.Lpos:
	ret
