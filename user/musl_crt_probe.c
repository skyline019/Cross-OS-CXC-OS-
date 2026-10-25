/* M68: host musl-gcc static CRT → USER_MUSL_CRT_PASS. */
#include <unistd.h>

int main(void) {
    static const char pass[] = "USER_MUSL_CRT_PASS\n";
    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        return 1;
    }
    return 0;
}
