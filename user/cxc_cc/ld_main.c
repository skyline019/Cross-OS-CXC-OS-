/* M267: CLI driver for freestanding ld.c — ld link.rsp out.elf */
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
    static char rsp_buf[CC_SRC_MAX];
    static char path_store[CC_LINK_MAX][CC_PATH_MAX];
    static const char* paths[CC_LINK_MAX];
    size_t rsp_len = 0;
    unsigned char* elf = 0;
    size_t elf_len = 0;
    int npaths = 0;
    size_t i = 0;
    int fd;
    if (argc < 3) {
        return 50;
    }
    fd = open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        return 51;
    }
    if (read_all(fd, rsp_buf, CC_SRC_MAX - 1, &rsp_len) != 0) {
        close(fd);
        return 52;
    }
    close(fd);
    rsp_buf[rsp_len] = 0;
    while (i < rsp_len && npaths < CC_LINK_MAX) {
        size_t start;
        size_t len;
        while (i < rsp_len &&
               (rsp_buf[i] == ' ' || rsp_buf[i] == '\t' || rsp_buf[i] == '\r' ||
                rsp_buf[i] == '\n')) {
            i++;
        }
        if (i >= rsp_len) {
            break;
        }
        if (rsp_buf[i] == '#' || rsp_buf[i] == ';') {
            while (i < rsp_len && rsp_buf[i] != '\n') {
                i++;
            }
            continue;
        }
        start = i;
        while (i < rsp_len && rsp_buf[i] != '\n' && rsp_buf[i] != '\r') {
            i++;
        }
        len = i - start;
        while (len > 0 && (rsp_buf[start + len - 1] == ' ' ||
                           rsp_buf[start + len - 1] == '\t')) {
            len--;
        }
        if (len == 0 || len >= sizeof(path_store[0])) {
            return 50;
        }
        {
            size_t k;
            for (k = 0; k < len; k++) {
                path_store[npaths][k] = rsp_buf[start + k];
            }
            path_store[npaths][len] = 0;
        }
        paths[npaths] = path_store[npaths];
        npaths++;
    }
    if (npaths < 1) {
        return 50;
    }
    if (ld_link(paths, npaths, &elf, &elf_len) != 0 || !elf || elf_len == 0) {
        return 60 + (ld_last_fail_code() % 40);
    }
    fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
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
