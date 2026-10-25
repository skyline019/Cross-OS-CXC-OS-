/* M53: host-gcc + mini_libc open/read/write/close → USER_LIBC_FS_PASS. */
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"

static int bytes_equal(const char* left, const char* right, long length) {
    for (long index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

void _start(void) {
    static const char path[] = "libcfs.txt";
    static const char payload[] = "LIBC-FS";
    static const char pass[] = "USER_LIBC_FS_PASS\n";
    char buffer[8];

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        _exit(1);
    }
    if (write(fd, payload, sizeof(payload) - 1) !=
        (ssize_t)(sizeof(payload) - 1)) {
        _exit(2);
    }
    if (close(fd) != 0) {
        _exit(3);
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        _exit(4);
    }
    if (read(fd, buffer, sizeof(payload) - 1) !=
        (ssize_t)(sizeof(payload) - 1)) {
        _exit(5);
    }
    if (!bytes_equal(buffer, payload, (long)(sizeof(payload) - 1))) {
        _exit(6);
    }
    if (close(fd) != 0) {
        _exit(7);
    }

    if (write(1, pass, sizeof(pass) - 1) != (ssize_t)(sizeof(pass) - 1)) {
        _exit(8);
    }
    _exit(0);
}
