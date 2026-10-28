.text
.globl isprint
isprint:
	mov %edi, %eax
	sub $32, %eax
	cmp $94, %eax
	setbe %al
	movzbl %al, %eax
	ret
