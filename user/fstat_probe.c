/* Freestanding M48 probe: write file → fstat size/mode → USER_FSTAT_PASS. */

enum {
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_FSTAT = 5,
    SYS_EXIT = 60,
    O_WRONLY = 1,
    O_CREAT = 64,
    O_TRUNC = 512,
    S_IFMT = 0170000,
    S_IFREG = 0100000,
};

/* Linux x86-64 kernel `struct stat` layout (syscall ABI). */
struct KernelStat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int __pad0;
    unsigned long st_rdev;
    long st_size;
    long st_blksize;
    long st_blocks;
    unsigned long st_atime;
    unsigned long st_atime_nsec;
    unsigned long st_mtime;
    unsigned long st_mtime_nsec;
    unsigned long st_ctime;
    unsigned long st_ctime_nsec;
    long __unused[3];
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
    static const char path[] = "stat.bin";
    static const char payload[] = "ABCDEFGH";
    static const char pass[] = "USER_FSTAT_PASS\n";
    struct KernelStat st;

    const long open_flags = O_WRONLY | O_CREAT | O_TRUNC;
    long fd = syscall3(SYS_OPEN, (long)path, open_flags, 0644);
    if (fd < 0) {
        syscall1(SYS_EXIT, 1);
    }
    if (syscall3(SYS_WRITE, fd, (long)payload, 8) != 8) {
        syscall1(SYS_EXIT, 2);
    }
    for (unsigned i = 0; i < sizeof(st); ++i) {
        ((char*)&st)[i] = 0;
    }
    if (syscall3(SYS_FSTAT, fd, (long)&st, 0) != 0) {
        syscall1(SYS_EXIT, 3);
    }
    if (st.st_size != 8) {
        syscall1(SYS_EXIT, 4);
    }
    if ((st.st_mode & S_IFMT) != S_IFREG) {
        syscall1(SYS_EXIT, 5);
    }
    if (st.st_nlink != 1 || st.st_blksize != 4096) {
        syscall1(SYS_EXIT, 6);
    }
    if (syscall1(SYS_CLOSE, fd) != 0) {
        syscall1(SYS_EXIT, 7);
    }

    syscall3(SYS_WRITE, 1, (long)pass, (long)(sizeof(pass) - 1));
    syscall1(SYS_EXIT, 0);
    for (;;) {
    }
}
