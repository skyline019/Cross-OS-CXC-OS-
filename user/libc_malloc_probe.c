/* M56: host-gcc + mini_libc malloc → USER_LIBC_MALLOC_PASS. */
#include "mini_libc/stdlib.h"
#include "mini_libc/unistd.h"

void _start(void) {
    static const char pass[] = "USER_LIBC_MALLOC_PASS\n";

    unsigned char* a = (unsigned char*)malloc(24);
    unsigned char* b = (unsigned char*)malloc(40);
    if (a == 0 || b == 0 || a == b) {
        _exit(1);
    }

    a[0] = 0x11;
    a[23] = 0x22;
    b[0] = 0x33;
    b[39] = 0x44;

    if (a[0] != 0x11 || a[23] != 0x22 || b[0] != 0x33 || b[39] != 0x44) {
        _exit(2);
    }

    free(a);
    free(b);

    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        _exit(3);
    }
    _exit(0);
}
