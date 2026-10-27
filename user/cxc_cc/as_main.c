/* M266: CLI driver for freestanding as.c — as in.s out.o */
#include "cxc_cc/cc.h"
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"

static int read_all(int fd, char* buf, size_t cap, size_t* out_n) {
    size_t n = 0;
    while (n < cap) {
        size_t want = cap - n;
        ssize_t r;
        if (want > 4096) {
            want = 4096;
        }
        r = read(fd, buf + n, want);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            break;
        }
        n += (size_t)r;
    }
    *out_n = n;
    return 0;
}

static int write_all(int fd, const unsigned char* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        size_t chunk = n - off;
        ssize_t w;
        if (chunk > 4096) {
            chunk = 4096;
        }
        w = write(fd, buf + off, chunk);
        if (w < 0 || (size_t)w != chunk) {
            return -1;
        }
        off += (size_t)chunk;
    }
    return 0;
}

int main(int argc, char** argv) {
    static char src_buf[CC_SRC_MAX];
    size_t src_len = 0;
    unsigned char* elf = 0;
    size_t elf_len = 0;
    int fd;
    if (argc < 3) {
        return 50;
    }
    fd = open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        return 51;
    }
    if (read_all(fd, src_buf, CC_SRC_MAX - 1, &src_len) != 0) {
        close(fd);
        return 52;
    }
    close(fd);
    src_buf[src_len] = 0;
    if (as_compile(src_buf, src_len, &elf, &elf_len) != 0 || !elf ||
        elf_len == 0) {
        return 60 + (as_last_fail_code() % 40);
    }
    fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return 53;
    }
    if (write_all(fd, elf, elf_len) != 0) {
        close(fd);
        return 54;
    }
    if (close(fd) != 0) {
        return 55;
    }
    return 0;
}
