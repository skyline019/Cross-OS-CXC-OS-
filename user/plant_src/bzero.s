.text
.globl bzero
bzero:
	test %rsi, %rsi
	jz .Ld
	mov %rsi, %rcx
	xor %eax, %eax
	rep stosb
.Ld:
	ret
