.text
.globl ntohl
ntohl:
	mov %edi, %eax
	bswap %eax
	ret
