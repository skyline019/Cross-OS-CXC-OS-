.text
.globl abs
abs:
	mov %edi, %eax
	test %eax, %eax
	jns .Lpos
	neg %eax
.Lpos:
	ret
