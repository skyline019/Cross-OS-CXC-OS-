/* M73: musl-gcc fopen + unlink + recreate → USER_MUSL_UNL_PASS. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    static const char path[] = "mslunl.txt";
    static const char payload[] = "UNL";
    static const char pass[] = "USER_MUSL_UNL_PASS\n";
    char buffer[8];

    FILE* out = fopen(path, "w");
    if (out == 0) {
        return 1;
    }
    if (fwrite(payload, 1, sizeof(payload) - 1, out) != sizeof(payload) - 1) {
        return 2;
    }
    if (fclose(out) != 0) {
        return 3;
    }

    if (unlink(path) != 0) {
        return 4;
    }

    FILE* gone = fopen(path, "r");
    if (gone != 0) {
        fclose(gone);
        return 5;
    }
    if (errno != ENOENT) {
        return 6;
    }

    out = fopen(path, "w");
    if (out == 0) {
        return 7;
    }
    if (fwrite(payload, 1, sizeof(payload) - 1, out) != sizeof(payload) - 1) {
        return 8;
    }
    if (fclose(out) != 0) {
        return 9;
    }

    FILE* in = fopen(path, "r");
    if (in == 0) {
        return 10;
    }
    if (fread(buffer, 1, sizeof(payload) - 1, in) != sizeof(payload) - 1) {
        return 11;
    }
    if (memcmp(buffer, payload, sizeof(payload) - 1) != 0) {
        return 12;
    }
    if (fclose(in) != 0) {
        return 13;
    }

    if (printf("%s", pass) < 0) {
        return 14;
    }
    if (fflush(stdout) != 0) {
        return 15;
    }
    return 0;
}
