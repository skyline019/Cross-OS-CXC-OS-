.text
.globl isupper
isupper:
	mov %edi, %eax
	sub $65, %eax
	cmp $25, %eax
	setbe %al
	movzbl %al, %eax
	ret
