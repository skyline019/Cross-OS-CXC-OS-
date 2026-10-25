/* M61: host-gcc + crt0 + uname → USER_LIBC_UNAME_PASS. */
#include "mini_libc/string.h"
#include "mini_libc/unistd.h"
#include "mini_libc/utsname.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_LIBC_UNAME_PASS\n";
    struct utsname u;
    (void)argc;
    (void)argv;

    if (uname(&u) != 0) {
        return 1;
    }
    if (strcmp(u.sysname, "CXC/OS") != 0) {
        return 2;
    }
    if (strcmp(u.machine, "x86_64") != 0) {
        return 3;
    }
    if (u.release[0] == '\0' || u.nodename[0] == '\0') {
        return 4;
    }
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 5;
    }
    return 0;
}
