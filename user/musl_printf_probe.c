/* M70: musl-gcc static CRT + printf → USER_MUSL_PRT_PASS. */
#include <stdio.h>

int main(void) {
    if (printf("USER_MUSL_PRT_PASS\n") < 0) {
        return 1;
    }
    if (fflush(stdout) != 0) {
        return 2;
    }
    return 0;
}
