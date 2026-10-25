/* Freestanding M50 probe: mkdir + nested files → getdents64 → USER_GETDENTS_PASS. */

enum {
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_EXIT = 60,
    SYS_MKDIR = 83,
    SYS_GETDENTS64 = 217,
    O_RDONLY = 0,
    O_WRONLY = 1,
    O_CREAT = 64,
    O_TRUNC = 512,
    DT_DIR = 4,
    DT_REG = 8,
};

struct LinuxDirent64 {
    unsigned long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
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

static int name_eq(const char* left, const char* right) {
    for (;;) {
        if (*left != *right) {
            return 0;
        }
        if (*left == 0) {
            return 1;
        }
        ++left;
        ++right;
    }
}

void _start(void) {
    static const char dir_path[] = "list_dir";
    static const char file_a[] = "list_dir/a.txt";
    static const char file_b[] = "list_dir/b.txt";
    static const char payload[] = "x";
    static const char pass[] = "USER_GETDENTS_PASS\n";
    char buffer[512];
    int saw_dot = 0;
    int saw_dotdot = 0;
    int saw_a = 0;
    int saw_b = 0;

    if (syscall3(SYS_MKDIR, (long)dir_path, 0755, 0) != 0) {
        syscall1(SYS_EXIT, 1);
    }

    const long creat = O_WRONLY | O_CREAT | O_TRUNC;
    long fd = syscall3(SYS_OPEN, (long)file_a, creat, 0644);
    if (fd < 0 || syscall3(SYS_WRITE, fd, (long)payload, 1) != 1 ||
        syscall1(SYS_CLOSE, fd) != 0) {
        syscall1(SYS_EXIT, 2);
    }
    fd = syscall3(SYS_OPEN, (long)file_b, creat, 0644);
    if (fd < 0 || syscall3(SYS_WRITE, fd, (long)payload, 1) != 1 ||
        syscall1(SYS_CLOSE, fd) != 0) {
        syscall1(SYS_EXIT, 3);
    }

    fd = syscall3(SYS_OPEN, (long)dir_path, O_RDONLY, 0);
    if (fd < 0) {
        syscall1(SYS_EXIT, 4);
    }

    for (;;) {
        const long got = syscall3(SYS_GETDENTS64, fd, (long)buffer, 512);
        if (got < 0) {
            syscall1(SYS_EXIT, 5);
        }
        if (got == 0) {
            break;
        }
        long pos = 0;
        while (pos < got) {
            struct LinuxDirent64* ent = (struct LinuxDirent64*)(buffer + pos);
            if (ent->d_reclen == 0 || pos + ent->d_reclen > got) {
                syscall1(SYS_EXIT, 6);
            }
            if (name_eq(ent->d_name, ".")) {
                if (ent->d_type != DT_DIR) {
                    syscall1(SYS_EXIT, 7);
                }
                saw_dot = 1;
            } else if (name_eq(ent->d_name, "..")) {
                if (ent->d_type != DT_DIR) {
                    syscall1(SYS_EXIT, 8);
                }
                saw_dotdot = 1;
            } else if (name_eq(ent->d_name, "a.txt")) {
                if (ent->d_type != DT_REG) {
                    syscall1(SYS_EXIT, 9);
                }
                saw_a = 1;
            } else if (name_eq(ent->d_name, "b.txt")) {
                if (ent->d_type != DT_REG) {
                    syscall1(SYS_EXIT, 10);
                }
                saw_b = 1;
            }
            pos += ent->d_reclen;
        }
    }

    if (!saw_dot || !saw_dotdot || !saw_a || !saw_b) {
        syscall1(SYS_EXIT, 11);
    }
    if (syscall1(SYS_CLOSE, fd) != 0) {
        syscall1(SYS_EXIT, 12);
    }

    syscall3(SYS_WRITE, 1, (long)pass, (long)(sizeof(pass) - 1));
    syscall1(SYS_EXIT, 0);
    for (;;) {
    }
}
