.text
.globl toupper
toupper:
	mov %edi, %eax
	cmp $97, %edi
	jb .Ldone
	cmp $122, %edi
	ja .Ldone
	sub $32, %eax
.Ldone:
	ret
