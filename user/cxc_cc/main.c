/* On-box CXC mini-cc: optional disk/lib.s|lib.c→lib.o, then in.c→out.elf. */
#include "cxc_cc/cc.h"
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"

extern int execve(const char* path, char* const argv[], char* const envp[]);

static int read_all(int fd, char* buf, size_t cap, size_t* out_n) {
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
        off += (size_t)w;
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
    case 40:
        hint = "DHINT:ASM\n";
        break;
    default:
        hint = "DHINT:UNK\n";
        break;
    }
    c = write(1, hint, 10);
    (void)c;
}

static int compile_write(const char* in_path, const char* out_path, int obj) {
    static char src_buf[CC_SRC_MAX];
    size_t src_len = 0;
    unsigned char* elf = 0;
    size_t elf_len = 0;
    int fd = open(in_path, O_RDONLY, 0);
    if (fd < 0) {
        return -2;
    }
    if (read_all(fd, src_buf, CC_SRC_MAX - 1, &src_len) != 0) {
        close(fd);
        return -3;
    }
    close(fd);
    src_buf[src_len] = 0;
    if (obj) {
        if (cc_compile_obj(src_buf, src_len, &elf, &elf_len) != 0 || !elf ||
            elf_len == 0) {
            write_dmsg(cc_last_fail_code());
            return -4;
        }
    } else {
        if (cc_compile(src_buf, src_len, &elf, &elf_len) != 0 || !elf ||
            elf_len == 0) {
            write_dmsg(cc_last_fail_code());
            return -4;
        }
    }
    fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, obj ? 0644 : 0755);
    if (fd < 0) {
        return -5;
    }
    if (write_all(fd, elf, elf_len) != 0) {
        close(fd);
        return -6;
    }
    if (close(fd) != 0) {
        return -7;
    }
    return 0;
}

/* M127: assemble GAS subset → SysV ET_REL. */
static int assemble_write(const char* in_path, const char* out_path) {
    static char src_buf[CC_SRC_MAX];
    size_t src_len = 0;
    unsigned char* elf = 0;
    size_t elf_len = 0;
    int fd = open(in_path, O_RDONLY, 0);
    if (fd < 0) {
        return -2;
    }
    if (read_all(fd, src_buf, CC_SRC_MAX - 1, &src_len) != 0) {
        close(fd);
        return -3;
    }
    close(fd);
    src_buf[src_len] = 0;
    if (as_compile(src_buf, src_len, &elf, &elf_len) != 0 || !elf ||
        elf_len == 0) {
        write_dmsg(as_last_fail_code());
        return -4;
    }
    fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -5;
    }
    if (write_all(fd, elf, elf_len) != 0) {
        close(fd);
        return -6;
    }
    if (close(fd) != 0) {
        return -7;
    }
    return 0;
}

int main(int argc, char** argv) {
    char* out_argv[2];
    char* out_envp[1];
    int rc;
    (void)argc;
    (void)argv;

    /* M127: prefer disk/lib.s → as; else M113 disk/lib.c → cc_compile_obj. */
    {
        int sfd = open("disk/lib.s", O_RDONLY, 0);
        if (sfd >= 0) {
            close(sfd);
            rc = assemble_write("disk/lib.s", "disk/lib.o");
            if (rc == -4) {
                _exit(4);
            }
            if (rc != 0) {
                _exit(9);
            }
        } else {
            int lfd = open("disk/lib.c", O_RDONLY, 0);
            if (lfd >= 0) {
                close(lfd);
                rc = compile_write("disk/lib.c", "disk/lib.o", 1);
                if (rc == -4) {
                    _exit(4);
                }
                if (rc != 0) {
                    _exit(9);
                }
            }
        }
    }

    rc = compile_write("disk/in.c", "disk/out.elf", 0);
    if (rc == -2) {
        _exit(2);
    }
    if (rc == -3) {
        _exit(3);
    }
    if (rc == -4) {
        _exit(4);
    }
    if (rc == -5) {
        _exit(5);
    }
    if (rc == -6) {
        _exit(6);
    }
    if (rc == -7) {
        _exit(7);
    }
    if (rc != 0) {
        _exit(9);
    }

    out_argv[0] = (char*)"disk/out.elf";
    out_argv[1] = 0;
    out_envp[0] = 0;
    execve("disk/out.elf", out_argv, out_envp);
    _exit(8);
}
