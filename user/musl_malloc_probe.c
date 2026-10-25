/* M69: musl-gcc static CRT + malloc/free → USER_MUSL_MAL_PASS. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    static const char pass[] = "USER_MUSL_MAL_PASS\n";
    char* buf = (char*)malloc(64);
    if (buf == 0) {
        return 1;
    }
    memset(buf, 0, 64);
    memcpy(buf, pass, sizeof(pass) - 1);
    if (write(1, buf, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        free(buf);
        return 2;
    }
    free(buf);
    return 0;
}
