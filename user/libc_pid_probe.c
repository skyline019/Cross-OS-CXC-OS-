/* M63: host-gcc + crt0 + getpid/getppid → USER_LIBC_PID_PASS. */
#include "mini_libc/unistd.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_LIBC_PID_PASS\n";
    (void)argc;
    (void)argv;

    if (getpid() != 1) {
        return 1;
    }
    if (getppid() != 0) {
        return 2;
    }
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 3;
    }
    return 0;
}
