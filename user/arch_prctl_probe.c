/* Freestanding M51 probe: ARCH_SET_FS / ARCH_GET_FS + %fs:0 → USER_ARCH_PRCTL_PASS. */

enum {
    SYS_WRITE = 1,
    SYS_EXIT = 60,
    SYS_ARCH_PRCTL = 158,
    ARCH_SET_FS = 0x1002,
    ARCH_GET_FS = 0x1003,
};

static long syscall3(long number, long a, long b, long c) {
    register long rax __asm__("rax") = number;
    register long rdi __asm__("rdi") = a;
    register long rsi __asm__("rsi") = b;
    register long rdx __asm__("rdx") = c;
    __asm__ volatile("int $0x80"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx)
                     : "memory", "rcx", "r11");
    return rax;
}

static long syscall1(long number, long a) {
    return syscall3(number, a, 0, 0);
}

void _start(void) {
    static const char pass[] = "USER_ARCH_PRCTL_PASS\n";
    long tls = 0;
    long got = 0;
    long via_fs = 0;

    tls = 0xC0FFEE42L;
    if (syscall3(SYS_ARCH_PRCTL, ARCH_SET_FS, (long)&tls, 0) != 0) {
        syscall1(SYS_EXIT, 1);
    }
    if (syscall3(SYS_ARCH_PRCTL, ARCH_GET_FS, (long)&got, 0) != 0) {
        syscall1(SYS_EXIT, 2);
    }
    if (got != (long)&tls) {
        syscall1(SYS_EXIT, 3);
    }

    __asm__ volatile("mov %%fs:0, %0" : "=r"(via_fs) : : "memory");
    if (via_fs != 0xC0FFEE42L) {
        syscall1(SYS_EXIT, 4);
    }

    syscall3(SYS_WRITE, 1, (long)pass, (long)(sizeof(pass) - 1));
    syscall1(SYS_EXIT, 0);
    for (;;) {
    }
}
