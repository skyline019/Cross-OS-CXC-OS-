.text
.globl isalpha
isalpha:
	mov %edi, %eax
	or $32, %eax
	sub $97, %eax
	cmp $25, %eax
	setbe %al
	movzbl %al, %eax
	ret
