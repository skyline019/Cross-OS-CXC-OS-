/* M57: host-gcc + mini_libc errno → USER_LIBC_ERRNO_PASS. */
#include "mini_libc/errno.h"
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"

void _start(void) {
    static const char missing[] = "no-such-errno.txt";
    static const char pass[] = "USER_LIBC_ERRNO_PASS\n";

    errno = 0;
    const int fd = open(missing, O_RDONLY, 0);
    if (fd != -1) {
        _exit(1);
    }
    if (errno != ENOENT) {
        _exit(2);
    }
    if (*__errno_location() != ENOENT) {
        _exit(3);
    }

    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        _exit(4);
    }
    _exit(0);
}
