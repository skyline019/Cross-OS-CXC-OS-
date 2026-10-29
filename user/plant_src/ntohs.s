.text
.globl ntohs
ntohs:
	mov %edi, %eax
	rolw $8, %ax
	movzwl %ax, %eax
	ret
