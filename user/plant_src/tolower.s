.text
.globl tolower
tolower:
	mov %edi, %eax
	cmp $65, %edi
	jb .Ldone
	cmp $90, %edi
	ja .Ldone
	add $32, %eax
.Ldone:
	ret
