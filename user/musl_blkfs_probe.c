/* M78: musl-gcc fopen on durable CXCFS path disk/ → USER_MUSL_BLK_PASS. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    static const char path[] = "disk/muslblk.txt";
    static const char missing[] = "disk/no-such-blk.txt";
    static const char payload[] = "MBLK";
    static const char pass[] = "USER_MUSL_BLK_PASS\n";
    char buffer[8];

    /* Edge: missing durable path → ENOENT. */
    FILE* gone = fopen(missing, "r");
    if (gone != 0) {
        fclose(gone);
        return 1;
    }
    if (errno != ENOENT) {
        return 2;
    }

    FILE* out = fopen(path, "w");
    if (out == 0) {
        return 3;
    }
    if (fwrite(payload, 1, sizeof(payload) - 1, out) != sizeof(payload) - 1) {
        return 4;
    }
    if (fclose(out) != 0) {
        return 5;
    }

    FILE* in = fopen(path, "r");
    if (in == 0) {
        return 6;
    }
    if (fread(buffer, 1, sizeof(payload) - 1, in) != sizeof(payload) - 1) {
        return 7;
    }
    if (memcmp(buffer, payload, sizeof(payload) - 1) != 0) {
        return 8;
    }
    if (fclose(in) != 0) {
        return 9;
    }

    if (printf("%s", pass) < 0) {
        return 10;
    }
    if (fflush(stdout) != 0) {
        return 11;
    }
    return 0;
}
