.text
.globl islower
islower:
	mov %edi, %eax
	sub $97, %eax
	cmp $25, %eax
	setbe %al
	movzbl %al, %eax
	ret
