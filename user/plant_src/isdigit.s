.text
.globl isdigit
isdigit:
	mov %edi, %eax
	sub $48, %eax
	cmp $9, %eax
	setbe %al
	movzbl %al, %eax
	ret
