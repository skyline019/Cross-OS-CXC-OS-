.text
.globl toascii
toascii:
	mov %edi, %eax
	and $127, %eax
	ret
