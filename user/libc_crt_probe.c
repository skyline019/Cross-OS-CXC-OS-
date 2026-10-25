/* M60: host-gcc + mini_libc crt0 → main → USER_LIBC_CRT_PASS. */
#include "mini_libc/unistd.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_LIBC_CRT_PASS\n";

    if (argc != 1 || argv == 0 || argv[0] == 0 || argv[0][0] != 'c') {
        return 1;
    }
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 2;
    }
    return 0;
}
