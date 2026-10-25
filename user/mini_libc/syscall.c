/* Freestanding mini-libc: Linux x86-64 numbers via int 0x80. */
#include "mini_libc/unistd.h"

enum {
    SYS_WRITE = 1,
    SYS_EXIT = 60,
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

ssize_t write(int fd, const void* buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

void _exit(int status) {
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;) {
    }
}
