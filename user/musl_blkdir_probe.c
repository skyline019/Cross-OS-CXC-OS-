/* M79: musl mkdir/opendir on durable disk/ tree → USER_MUSL_DIR_PASS. */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(void) {
    static const char dir_path[] = "disk/bld";
    static const char file_path[] = "disk/bld/a.txt";
    static const char payload[] = "DIR";
    static const char pass[] = "USER_MUSL_DIR_PASS\n";
    int saw_a = 0;

    if (mkdir(dir_path, 0755) != 0) {
        return 1;
    }

    FILE* out = fopen(file_path, "w");
    if (out == 0) {
        return 2;
    }
    if (fwrite(payload, 1, sizeof(payload) - 1, out) != sizeof(payload) - 1) {
        return 3;
    }
    if (fclose(out) != 0) {
        return 4;
    }

    DIR* dir = opendir(dir_path);
    if (dir == 0) {
        return 5;
    }
    for (;;) {
        errno = 0;
        struct dirent* ent = readdir(dir);
        if (ent == 0) {
            if (errno != 0) {
                closedir(dir);
                return 6;
            }
            break;
        }
        if (strcmp(ent->d_name, "a.txt") == 0) {
            saw_a = 1;
        }
    }
    if (closedir(dir) != 0) {
        return 7;
    }
    if (saw_a == 0) {
        return 8;
    }

    /* Edge: nested mkdir under missing parent must fail. */
    if (mkdir("disk/missing/child", 0755) == 0) {
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
