/* M60: host-gcc + mini_libc crt0 → main → USER_LIBC_CRT_PASS.
 * CRT reads argc/argv from the kernel-seeded stack (default arg0 "musl"). */
#include "mini_libc/unistd.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_LIBC_CRT_PASS\n";

    if (argc != 1 || argv == 0 || argv[0] == 0) {
        return 1;
    }
    if (argv[0][0] != 'm' || argv[0][1] != 'u' || argv[0][2] != 's' ||
        argv[0][3] != 'l' || argv[0][4] != '\0') {
        return 1;
    }
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 2;
    }
    return 0;
}
