.text
.globl isspace
isspace:
	cmp $32, %edi
	je .Ly
	cmp $9, %edi
	je .Ly
	cmp $10, %edi
	je .Ly
	cmp $11, %edi
	je .Ly
	cmp $12, %edi
	je .Ly
	cmp $13, %edi
	je .Ly
	xor %eax, %eax
	ret
.Ly:
	mov $1, %eax
	ret
