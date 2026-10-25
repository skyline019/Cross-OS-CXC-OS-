/* M59: host-gcc + mini_libc writev → USER_LIBC_WRITEV_PASS. */
#include "mini_libc/unistd.h"

void _start(void) {
    static const char a[] = "USER_LIBC_";
    static const char b[] = "WRITEV_PASS\n";
    struct iovec iov[2];
    iov[0].iov_base = (void*)a;
    iov[0].iov_len = sizeof(a) - 1;
    iov[1].iov_base = (void*)b;
    iov[1].iov_len = sizeof(b) - 1;

    const ssize_t want =
        (ssize_t)(iov[0].iov_len + iov[1].iov_len);
    const ssize_t got = writev(1, iov, 2);
    if (got != want) {
        _exit(1);
    }
    _exit(0);
}
