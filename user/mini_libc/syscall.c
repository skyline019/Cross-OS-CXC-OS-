/* Freestanding mini-libc: Linux x86-64 numbers via int 0x80. */
#include "mini_libc/errno.h"
#include "mini_libc/unistd.h"
#include "mini_libc/utsname.h"

/* run_tool returns -EINVAL etc.; keep errno macros visible. */

enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_BRK = 12,
    SYS_WRITEV = 20,
    SYS_GETPID = 39,
    SYS_FORK = 57,
    SYS_EXECVE = 59,
    SYS_EXIT = 60,
    SYS_WAIT4 = 61,
    SYS_UNAME = 63,
    SYS_GETPPID = 110,
    SYS_SET_TID_ADDRESS = 218,
};

enum { PAGE_SIZE = 4096 };

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

static long syscall4(long number, long a, long b, long c, long d) {
    register long rax __asm__("rax") = number;
    register long rdi __asm__("rdi") = a;
    register long rsi __asm__("rsi") = b;
    register long rdx __asm__("rdx") = c;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("int $0x80"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                     : "memory", "rcx", "r11");
    return rax;
}

static long check_fd(long result) {
    if (result < 0) {
        errno = (int)(-result);
        return -1;
    }
    return result;
}

ssize_t read(int fd, void* buf, size_t count) {
    return (ssize_t)check_fd(syscall3(SYS_READ, fd, (long)buf, (long)count));
}

ssize_t write(int fd, const void* buf, size_t count) {
    return (ssize_t)check_fd(
        syscall3(SYS_WRITE, fd, (long)buf, (long)count));
}

ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    return (ssize_t)check_fd(
        syscall3(SYS_WRITEV, fd, (long)iov, iovcnt));
}

int open(const char* path, int flags, mode_t mode) {
    return (int)check_fd(syscall3(SYS_OPEN, (long)path, flags, mode));
}

int close(int fd) {
    return (int)check_fd(syscall3(SYS_CLOSE, fd, 0, 0));
}

void* brk(void* addr) {
    return (void*)syscall3(SYS_BRK, (long)addr, 0, 0);
}

void* sbrk(intptr_t increment) {
    void* const cur = brk(0);
    if (increment == 0) {
        return cur;
    }
    if (increment < 0) {
        errno = EINVAL;
        return (void*)(intptr_t)-1;
    }
    const uintptr_t want =
        (uintptr_t)cur + (uintptr_t)increment;
    const uintptr_t aligned =
        (want + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
    void* const got = brk((void*)aligned);
    if ((uintptr_t)got < aligned) {
        errno = ENOMEM;
        return (void*)(intptr_t)-1;
    }
    return cur;
}

void _exit(int status) {
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;) {
    }
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    long r = syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
    if (r < 0) {
        errno = (int)(-r);
        return -1;
    }
    return (int)r;
}

int uname(struct utsname* buf) {
    return (int)check_fd(syscall3(SYS_UNAME, (long)buf, 0, 0));
}

long set_tid_address(int* tidptr) {
    return syscall3(SYS_SET_TID_ADDRESS, (long)tidptr, 0, 0);
}

pid_t getpid(void) {
    return (pid_t)syscall3(SYS_GETPID, 0, 0, 0);
}

pid_t getppid(void) {
    return (pid_t)syscall3(SYS_GETPPID, 0, 0, 0);
}

pid_t fork(void) {
    long r = syscall3(SYS_FORK, 0, 0, 0);
    if (r < 0) {
        errno = (int)(-r);
        return (pid_t)-1;
    }
    return (pid_t)r;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    long r = syscall4(SYS_WAIT4, (long)pid, (long)status, (long)options, 0);
    if (r < 0) {
        errno = (int)(-r);
        return (pid_t)-1;
    }
    return (pid_t)r;
}

int run_tool(const char* path, char* const argv[]) {
    char* empty_env[1];
    pid_t pid;
    int status;
    empty_env[0] = 0;
    if (path == 0 || argv == 0) {
        errno = EINVAL;
        return -EINVAL;
    }
    pid = fork();
    if (pid < 0) {
        return -errno;
    }
    if (pid == 0) {
        execve(path, argv, empty_env);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -errno;
    }
    /* Kernel stores (exit & 0xff) << 8 — same as Linux WEXITSTATUS. */
    return (status >> 8) & 0xff;
}
