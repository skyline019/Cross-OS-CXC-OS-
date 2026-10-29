.text
.globl imaxabs
imaxabs:
	mov %rdi, %rax
	test %rax, %rax
	jns .Lpos
	neg %rax
.Lpos:
	ret
