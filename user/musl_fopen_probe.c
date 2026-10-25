/* M71: musl-gcc static CRT + fopen/fwrite/fread/fclose → USER_MUSL_FOP_PASS. */
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char path[] = "muslfop.txt";
    static const char payload[] = "MUSL-FOP";
    static const char pass[] = "USER_MUSL_FOP_PASS\n";
    char buffer[16];

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

    FILE* in = fopen(path, "r");
    if (in == 0) {
        return 4;
    }
    if (fread(buffer, 1, sizeof(payload) - 1, in) != sizeof(payload) - 1) {
        return 5;
    }
    if (memcmp(buffer, payload, sizeof(payload) - 1) != 0) {
        return 6;
    }
    if (fclose(in) != 0) {
        return 7;
    }

    if (printf("%s", pass) < 0) {
        return 8;
    }
    if (fflush(stdout) != 0) {
        return 9;
    }
    return 0;
}
