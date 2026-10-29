.text
.globl iscntrl
iscntrl:
	cmp $127, %edi
	je .Ly
	mov %edi, %eax
	cmp $32, %eax
	setb %al
	movzbl %al, %eax
	ret
.Ly:
	mov $1, %eax
	ret
