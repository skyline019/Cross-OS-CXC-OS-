/* M58: host-gcc + mini_libc string → USER_LIBC_STRING_PASS. */
#include "mini_libc/string.h"
#include "mini_libc/unistd.h"

void _start(void) {
    static const char pass[] = "USER_LIBC_STRING_PASS\n";
    char buf[16];
    char src[] = "cxc";

    if (strlen(src) != 3) {
        _exit(1);
    }
    if (strcmp(src, "cxc") != 0) {
        _exit(2);
    }
    if (strcmp(src, "cya") >= 0) {
        _exit(3);
    }

    memset(buf, 0xA5, sizeof(buf));
    if ((unsigned char)buf[0] != 0xA5 || (unsigned char)buf[15] != 0xA5) {
        _exit(4);
    }
    memcpy(buf, src, 4);
    if (memcmp(buf, src, 4) != 0) {
        _exit(5);
    }
    if (strcmp(buf, "cxc") != 0) {
        _exit(6);
    }

    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        _exit(7);
    }
    _exit(0);
}
