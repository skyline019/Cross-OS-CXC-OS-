/* M65/M66: musl-subset write via `syscall` → USER_MUSL_PASS (not mini_libc). */
#include "musl_subset/unistd.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_MUSL_PASS\n";
    (void)argc;
    (void)argv;

    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 1;
    }
    return 0;
}
