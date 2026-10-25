/* M55: host-gcc + mini_libc brk/sbrk → USER_LIBC_SBRK_PASS. */
#include "mini_libc/unistd.h"

void _start(void) {
    static const char pass[] = "USER_LIBC_SBRK_PASS\n";
    const unsigned char marker = 0xA5;

    void* const before = sbrk(0);
    if (before == (void*)(intptr_t)-1 || before == 0) {
        _exit(1);
    }

    void* const old = sbrk(4096);
    if (old == (void*)(intptr_t)-1 || old != before) {
        _exit(2);
    }

    volatile unsigned char* p = (volatile unsigned char*)old;
    *p = marker;
    if (*p != marker) {
        _exit(3);
    }

    void* const after = sbrk(0);
    if (after == (void*)(intptr_t)-1 ||
        (uintptr_t)after < (uintptr_t)before + 4096) {
        _exit(4);
    }

    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        _exit(5);
    }
    _exit(0);
}
