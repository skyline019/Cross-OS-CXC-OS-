/* On-box CXC mini-cc driver: disk/in.c → disk/out.elf → execve (A2). */
#include "cxc_cc/cc.h"
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"

extern int execve(const char* path, char* const argv[], char* const envp[]);

static int read_all(int fd, char* buf, size_t cap, size_t* out_n) {
    /* Kernel max_io is 4096; request at most that per read. */
    enum { CHUNK = 4096 };
    size_t n = 0;
    while (n < cap) {
        size_t want = cap - n;
        ssize_t r;
        if (want > CHUNK) {
            want = CHUNK;
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
        off += chunk;
    }
    return 0;
}

/* Debug兜底: DMSG:NN + DHINT:TAG on serial before exit 4. */
static void write_dmsg(int code) {
    char msg[8];
    const char* hint;
    int c;
    if (code < 0) {
        code = 0;
    }
    if (code > 99) {
        code = 99;
    }
    msg[0] = 'D';
    msg[1] = 'M';
    msg[2] = 'S';
    msg[3] = 'G';
    msg[4] = ':';
    msg[5] = (char)('0' + (code / 10));
    msg[6] = (char)('0' + (code % 10));
    msg[7] = '\n';
    c = write(1, msg, 8);
    (void)c;
    switch (code) {
    case 10:
        hint = "DHINT:PRE\n";
        break;
    case 11:
        hint = "DHINT:CAL\n";
        break;
    case 12:
        hint = "DHINT:STM\n";
        break;
    case 20:
        hint = "DHINT:ELF\n";
        break;
    case 21:
        hint = "DHINT:UND\n";
        break;
    case 22:
        hint = "DHINT:REL\n";
        break;
    case 23:
        hint = "DHINT:DUP\n";
        break;
    case 30:
        hint = "DHINT:PAR\n";
        break;
    default:
        hint = "DHINT:UNK\n";
        break;
    }
    c = write(1, hint, 10);
    (void)c;
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
        write_dmsg(cc_last_fail_code());
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
