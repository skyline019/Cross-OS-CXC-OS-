.text
.globl htonl
htonl:
	mov %edi, %eax
	bswap %eax
	ret
