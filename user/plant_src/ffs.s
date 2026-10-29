.text
.globl ffs
ffs:
	mov %edi, %eax
	test %eax, %eax
	jz .Lz
	bsf %eax, %eax
	add $1, %eax
	ret
.Lz:
	xor %eax, %eax
	ret
