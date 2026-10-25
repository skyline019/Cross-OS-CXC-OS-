/* Freestanding M47 probe: open/write/lseek/read round-trip → USER_LSEEK_PASS. */

enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_LSEEK = 8,
    SYS_EXIT = 60,
    O_RDONLY = 0,
    O_WRONLY = 1,
    O_CREAT = 64,
    O_TRUNC = 512,
    SEEK_SET = 0,
    SEEK_CUR = 1,
    SEEK_END = 2,
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

static int bytes_equal(const char* left, const char* right, long length) {
    for (long index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

void _start(void) {
    static const char path[] = "seek.bin";
    static const char payload[] = "ABCDEFGH";
    static const char expect[] = "EFGH";
    static const char pass[] = "USER_LSEEK_PASS\n";
    char buffer[8];

    const long open_flags = O_WRONLY | O_CREAT | O_TRUNC;
    long fd = syscall3(SYS_OPEN, (long)path, open_flags, 0644);
    if (fd < 0) {
        syscall1(SYS_EXIT, 1);
    }
    if (syscall3(SYS_WRITE, fd, (long)payload, 8) != 8) {
        syscall1(SYS_EXIT, 2);
    }
    if (syscall1(SYS_CLOSE, fd) != 0) {
        syscall1(SYS_EXIT, 3);
    }

    fd = syscall3(SYS_OPEN, (long)path, O_RDONLY, 0);
    if (fd < 0) {
        syscall1(SYS_EXIT, 4);
    }
    if (syscall3(SYS_LSEEK, fd, 4, SEEK_SET) != 4) {
        syscall1(SYS_EXIT, 5);
    }
    if (syscall3(SYS_READ, fd, (long)buffer, 4) != 4) {
        syscall1(SYS_EXIT, 6);
    }
    if (!bytes_equal(buffer, expect, 4)) {
        syscall1(SYS_EXIT, 7);
    }
    if (syscall3(SYS_LSEEK, fd, 0, SEEK_END) != 8) {
        syscall1(SYS_EXIT, 8);
    }
    if (syscall3(SYS_LSEEK, fd, -3, SEEK_CUR) != 5) {
        syscall1(SYS_EXIT, 9);
    }
    if (syscall1(SYS_CLOSE, fd) != 0) {
        syscall1(SYS_EXIT, 10);
    }

    syscall3(SYS_WRITE, 1, (long)pass, (long)(sizeof(pass) - 1));
    syscall1(SYS_EXIT, 0);
    for (;;) {
    }
}
