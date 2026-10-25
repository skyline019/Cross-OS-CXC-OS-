/* M52: host-gcc links against mini_libc write/_exit → USER_LIBC_PASS. */
#include "mini_libc/unistd.h"

void _start(void) {
    static const char pass[] = "USER_LIBC_PASS\n";
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        _exit(1);
    }
    _exit(0);
}
