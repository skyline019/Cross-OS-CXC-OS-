.text
.globl isblank
isblank:
	cmp $32, %edi
	je .Ly
	cmp $9, %edi
	je .Ly
	xor %eax, %eax
	ret
.Ly:
	mov $1, %eax
	ret
