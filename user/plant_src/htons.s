.text
.globl htons
htons:
	mov %edi, %eax
	rolw $8, %ax
	movzwl %ax, %eax
	ret
