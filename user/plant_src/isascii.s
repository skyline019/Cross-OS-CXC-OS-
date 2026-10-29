.text
.globl isascii
isascii:
	mov %edi, %eax
	and $128, %eax
	sete %al
	movzbl %al, %eax
	ret
