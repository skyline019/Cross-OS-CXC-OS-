.text
.globl ispunct
ispunct:
	mov %edi, %eax
	cmp $33, %eax
	jb .Ln
	cmp $47, %eax
	jbe .Ly
	cmp $58, %eax
	jb .Ln
	cmp $64, %eax
	jbe .Ly
	cmp $91, %eax
	jb .Ln
	cmp $96, %eax
	jbe .Ly
	cmp $123, %eax
	jb .Ln
	cmp $126, %eax
	jbe .Ly
.Ln:
	xor %eax, %eax
	ret
.Ly:
	mov $1, %eax
	ret
