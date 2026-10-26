/* On-box CXC mini-cc driver: disk/in.c → disk/out.elf → execve (A2/M96). */
#include "cxc_cc/cc.h"
#include "mini_libc/fcntl.h"

extern int execve(const char* path, char* const argv[], char* const envp[]);

static int read_all(int fd, char* buf, size_t cap, size_t* out_n) {
    size_t n = 0;
    while (n < cap) {
        ssize_t r = read(fd, buf + n, cap - n);
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
        off += chunk;
    }
    return 0;
}

int main(int argc, char** argv) {
    char* src;
    size_t src_len = 0;
    unsigned char* elf = 0;
    size_t elf_len = 0;
    int fd;
    char* out_argv[2];
    char* out_envp[1];
    (void)argc;
    (void)argv;

    static char src_buf[CC_SRC_MAX];
    src = src_buf;

    fd = open("disk/in.c", O_RDONLY, 0);
    if (fd < 0) {
        _exit(2);
    }
    if (read_all(fd, src, CC_SRC_MAX - 1, &src_len) != 0) {
        _exit(3);
    }
    close(fd);
    src[src_len] = 0;

    if (cc_compile(src, src_len, &elf, &elf_len) != 0 || !elf || elf_len == 0) {
        _exit(4);
    }

    fd = open("disk/out.elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        _exit(5);
    }
    if (write_all(fd, elf, elf_len) != 0) {
        _exit(6);
    }
    if (close(fd) != 0) {
        _exit(7);
    }

    out_argv[0] = (char*)"disk/out.elf";
    out_argv[1] = 0;
    out_envp[0] = 0;
    execve("disk/out.elf", out_argv, out_envp);
    _exit(8);
}
