.text
.globl isgraph
isgraph:
	mov %edi, %eax
	sub $33, %eax
	cmp $93, %eax
	setbe %al
	movzbl %al, %eax
	ret
