.text
.globl isalnum
isalnum:
	mov %edi, %eax
	sub $48, %eax
	cmp $9, %eax
	jbe .Ly
	mov %edi, %eax
	or $32, %eax
	sub $97, %eax
	cmp $25, %eax
	setbe %al
	movzbl %al, %eax
	ret
.Ly:
	mov $1, %eax
	ret
