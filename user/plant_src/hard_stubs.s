.text
.globl ___errno_location
.globl __stack_chk_fail
.globl __uflow
.globl free
.globl malloc
___errno_location:
	lea errno_cell(%rip), %rax
	ret
__stack_chk_fail:
.Lssp:
	ud2
	jmp .Lssp
__uflow:
	mov $-1, %eax
	ret
malloc:
	mov %rdi, %rax
	add $15, %rax
	and $-16, %rax
	push %rax
	mov heap_off(%rip), %rax
	mov %rax, %rcx
	pop %rax
	add %rcx, %rax
	cmp $512, %rax
	ja .Lmf
	push %rax
	lea heap(%rip), %rax
	add %rcx, %rax
	mov %rax, %rsi
	pop %rax
	mov %rax, heap_off(%rip)
	mov %rsi, %rax
	ret
.Lmf:
	xor %eax, %eax
	ret
free:
	ret
.data
errno_cell:
.quad 1
heap_off:
.quad 16
heap:
.byte 1
.zero 255
.zero 256
