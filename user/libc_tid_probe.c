/* M62: host-gcc + crt0 + set_tid_address → USER_LIBC_TID_PASS. */
#include "mini_libc/unistd.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_LIBC_TID_PASS\n";
    int clear_slot = 0;
    (void)argc;
    (void)argv;

    const long tid = set_tid_address(&clear_slot);
    if (tid != 1) {
        return 1;
    }
    if (set_tid_address(0) != 1) {
        return 2;
    }
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 3;
    }
    return 0;
}
