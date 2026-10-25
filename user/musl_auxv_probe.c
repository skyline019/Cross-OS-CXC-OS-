/* M67: musl-subset CRT reads seeded argc/argv/auxv → USER_MUSL_AUXV_PASS. */
#include "musl_subset/unistd.h"

int main(int argc, char** argv) {
    static const char pass[] = "USER_MUSL_AUXV_PASS\n";

    if (argc != 1) {
        return 1;
    }
    if (argv == 0 || argv[0] == 0) {
        return 2;
    }
    if (argv[0][0] != 'm' || argv[0][1] != 'u' || argv[0][2] != 's' ||
        argv[0][3] != 'l' || argv[0][4] != '\0') {
        return 3;
    }
    if (argv[1] != 0) {
        return 4;
    }
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 5;
    }
    return 0;
}
