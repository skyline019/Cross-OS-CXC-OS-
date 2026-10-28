        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # Preserve the firmware device-tree pointer before using a1 for stack
        # calculation; QEMU may place the DTB inside our optional RAM window.
        mv t0, a1
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 16384-byte stack per CPU.
        # sp = stack0 + ((hartid + 1) * 16384)
        la sp, stack0
        li a0, 16384
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
        # jump to start(fdt_pa) in start.c
        mv a0, t0
        call start
spin:
        j spin
