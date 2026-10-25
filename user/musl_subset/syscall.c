/* musl-subset syscall shim: Linux x86-64 numbers, int 0x80 transport.
 * Upstream musl uses the syscall instruction; see docs/musl-subset-link-notes.md. */
#include "unistd.h"

enum {
    SYS_WRITE = 1,
    SYS_EXIT = 60,
};

static long __syscall3(long number, long a, long b, long c) {
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

ssize_t write(int fd, const void* buf, size_t count) {
    const long result =
        __syscall3(SYS_WRITE, fd, (long)buf, (long)count);
    if (result < 0) {
        return -1;
    }
    return (ssize_t)result;
}

void _exit(int status) {
    __syscall3(SYS_EXIT, status, 0, 0);
    for (;;) {
    }
}
