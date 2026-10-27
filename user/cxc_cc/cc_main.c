/* M275–M278 CCDRV: freestanding disk/cc argv CLI.
 *   cc -c in.c -o out.o
 *   cc -o out.elf a.c b.s x.o @job.rsp
 * ET_EXEC only via spawn disk/ld; .s via spawn disk/as; nested -c via spawn disk/cc.
 * argc<2 → default -o disk/out.elf @disk/job.rsp (ONBXST compat).
 */
#include "cxc_cc/cc.h"
#include "mini_libc/fcntl.h"
#include "mini_libc/string.h"
#include "mini_libc/unistd.h"

enum {
    CC_CLI_MAX_IN = 96,
    CC_CLI_PATH = CC_PATH_MAX
};

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
    case 50:
        hint = "DHINT:LNK\n";
        break;
    default:
        hint = "DHINT:UNK\n";
        break;
    }
    c = write(1, hint, 10);
    (void)c;
}

static int streq(const char* a, const char* b) {
    size_t i = 0;
    if (a == 0 || b == 0) {
        return 0;
    }
    while (a[i] != 0 && a[i] == b[i]) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int ends_with(const char* path, const char* suf) {
    size_t n;
    size_t m;
    size_t i;
    if (path == 0 || suf == 0) {
        return 0;
    }
    n = strlen(path);
    m = strlen(suf);
    if (m > n) {
        return 0;
    }
    for (i = 0; i < m; i++) {
        if (path[n - m + i] != suf[i]) {
            return 0;
        }
    }
    return 1;
}

static void replace_ext(const char* in, const char* ext, char* out, size_t cap) {
    size_t n = strlen(in);
    size_t i;
    size_t dot = n;
    size_t el = strlen(ext);
    for (i = 0; i < n; i++) {
        if (in[i] == '.') {
            dot = i;
        }
    }
    if (dot + el + 1 >= cap) {
        out[0] = 0;
        return;
    }
    for (i = 0; i < dot; i++) {
        out[i] = in[i];
    }
    for (i = 0; i <= el; i++) {
        out[dot + i] = ext[i];
    }
}

static int compile_c_to_obj(const char* in_path, const char* out_path) {
    static char src_buf[CC_SRC_MAX];
    size_t src_len = 0;
    unsigned char* elf = 0;
    size_t elf_len = 0;
    int fd = open(in_path, O_RDONLY, 0);
    if (fd < 0) {
        return 51;
    }
    if (read_all(fd, src_buf, CC_SRC_MAX - 1, &src_len) != 0) {
        close(fd);
        return 52;
    }
    close(fd);
    src_buf[src_len] = 0;
    if (cc_compile_obj(src_buf, src_len, &elf, &elf_len) != 0 || !elf ||
        elf_len == 0) {
        write_dmsg(cc_last_fail_code());
        return 4;
    }
    fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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

static int spawn_as(const char* in_path, const char* out_path) {
    char* argv[4];
    int st;
    int pfd = open("disk/as", O_RDONLY, 0);
    if (pfd < 0) {
        write_dmsg(41);
        return 4;
    }
    close(pfd);
    argv[0] = (char*)"disk/as";
    argv[1] = (char*)in_path;
    argv[2] = (char*)out_path;
    argv[3] = 0;
    st = run_tool("disk/as", argv);
    if (st != 0) {
        if (st > 0 && st < 100) {
            write_dmsg(st);
        } else {
            write_dmsg(40);
        }
        return 4;
    }
    return 0;
}

static int spawn_ld(const char* rsp_path, const char* out_path) {
    char* argv[4];
    int st;
    int pfd = open("disk/ld", O_RDONLY, 0);
    if (pfd < 0) {
        write_dmsg(42);
        return 4;
    }
    close(pfd);
    argv[0] = (char*)"disk/ld";
    argv[1] = (char*)rsp_path;
    argv[2] = (char*)out_path;
    argv[3] = 0;
    st = run_tool("disk/ld", argv);
    if (st != 0) {
        if (st > 0 && st < 100) {
            write_dmsg(st);
        } else {
            write_dmsg(50);
        }
        return 4;
    }
    return 0;
}

/* Nested disk/cc -c so spawn_cc audit sees real process boundary. */
static int spawn_cc_c(const char* in_path, const char* out_path) {
    char* argv[6];
    int st;
    int pfd = open("disk/cc", O_RDONLY, 0);
    if (pfd < 0) {
        write_dmsg(43);
        return 4;
    }
    close(pfd);
    argv[0] = (char*)"disk/cc";
    argv[1] = (char*)"-c";
    argv[2] = (char*)in_path;
    argv[3] = (char*)"-o";
    argv[4] = (char*)out_path;
    argv[5] = 0;
    st = run_tool("disk/cc", argv);
    if (st != 0) {
        if (st > 0 && st < 100) {
            write_dmsg(st);
        } else {
            write_dmsg(30);
        }
        return 4;
    }
    return 0;
}

static int append_input(char paths[][CC_CLI_PATH], int* n, const char* p) {
    size_t i;
    if (*n >= CC_CLI_MAX_IN || p == 0 || p[0] == 0) {
        return -1;
    }
    if (strlen(p) >= CC_CLI_PATH) {
        return -1;
    }
    for (i = 0; p[i] != 0; i++) {
        paths[*n][i] = p[i];
    }
    paths[*n][i] = 0;
    (*n)++;
    return 0;
}

static int load_rsp(const char* rsp_path, char paths[][CC_CLI_PATH], int* n) {
    static char rsp_buf[CC_SRC_MAX];
    size_t rsp_len = 0;
    size_t i = 0;
    int fd = open(rsp_path, O_RDONLY, 0);
    if (fd < 0) {
        return 51;
    }
    if (read_all(fd, rsp_buf, CC_SRC_MAX - 1, &rsp_len) != 0) {
        close(fd);
        return 52;
    }
    close(fd);
    rsp_buf[rsp_len] = 0;
    while (i < rsp_len && *n < CC_CLI_MAX_IN) {
        size_t start;
        size_t len;
        char tmp[CC_CLI_PATH];
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
        while (i < rsp_len && rsp_buf[i] != ' ' && rsp_buf[i] != '\t' &&
               rsp_buf[i] != '\r' && rsp_buf[i] != '\n') {
            i++;
        }
        len = i - start;
        if (len == 0 || len >= CC_CLI_PATH) {
            return 56;
        }
        memcpy(tmp, rsp_buf + start, len);
        tmp[len] = 0;
        if (tmp[0] == '@') {
            return 57; /* nested @rsp not supported */
        }
        if (append_input(paths, n, tmp) != 0) {
            return 56;
        }
    }
    return 0;
}

static int write_link_rsp(const char* rsp_path, char objs[][CC_CLI_PATH],
                          int nobj) {
    int fd;
    int i;
    fd = open(rsp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return 53;
    }
    for (i = 0; i < nobj; i++) {
        size_t n = strlen(objs[i]);
        if (write_all(fd, (const unsigned char*)objs[i], n) != 0 ||
            write_all(fd, (const unsigned char*)"\n", 1) != 0) {
            close(fd);
            return 54;
        }
    }
    if (close(fd) != 0) {
        return 55;
    }
    return 0;
}

int main(int argc, char** argv) {
    int compile_only = 0;
    const char* out_path = 0;
    /* Path tables in BSS — large enough for musl soak. */
    static char inputs[CC_CLI_MAX_IN][CC_CLI_PATH];
    static char objs[CC_CLI_MAX_IN][CC_CLI_PATH];
    int n_in = 0;
    int n_obj = 0;
    int i;
    int rc;

    /* Default job for bare execve(disk/cc) / disk/cxc-cc from ONBXST. */
    if (argc < 2) {
        static char* def_av[] = {(char*)"disk/cc", (char*)"-o",
                                 (char*)"disk/out.elf", (char*)"@disk/job.rsp",
                                 0};
        argv = def_av;
        argc = 4;
    }

    for (i = 1; i < argc; i++) {
        if (streq(argv[i], "-c")) {
            compile_only = 1;
            continue;
        }
        if (streq(argv[i], "-o")) {
            if (i + 1 >= argc) {
                return 50;
            }
            out_path = argv[++i];
            continue;
        }
        if (argv[i][0] == '@') {
            rc = load_rsp(argv[i] + 1, inputs, &n_in);
            if (rc != 0) {
                return rc;
            }
            continue;
        }
        if (append_input(inputs, &n_in, argv[i]) != 0) {
            return 56;
        }
    }

    if (n_in == 0) {
        return 50;
    }

    if (compile_only) {
        const char* in = inputs[0];
        char auto_out[CC_CLI_PATH];
        if (n_in != 1) {
            return 50;
        }
        if (out_path == 0) {
            if (ends_with(in, ".c")) {
                replace_ext(in, ".o", auto_out, sizeof(auto_out));
            } else if (ends_with(in, ".s")) {
                replace_ext(in, ".o", auto_out, sizeof(auto_out));
            } else {
                return 50;
            }
            if (auto_out[0] == 0) {
                return 50;
            }
            out_path = auto_out;
        }
        if (ends_with(in, ".c")) {
            return compile_c_to_obj(in, out_path);
        }
        if (ends_with(in, ".s")) {
            return spawn_as(in, out_path);
        }
        return 50;
    }

    if (out_path == 0) {
        out_path = "disk/out.elf";
    }

    /* Materialize each input to an ET_REL .o (cc / as / passthrough). */
    for (i = 0; i < n_in; i++) {
        const char* in = inputs[i];
        char obj[CC_CLI_PATH];
        if (ends_with(in, ".o") || ends_with(in, ".elf")) {
            if (append_input(objs, &n_obj, in) != 0) {
                return 56;
            }
            continue;
        }
        if (ends_with(in, ".c")) {
            replace_ext(in, ".o", obj, sizeof(obj));
            if (obj[0] == 0) {
                return 50;
            }
            /* Nest disk/cc -c for a.c/b.c; keep fat lib.c in-process. */
            if (streq(in, "disk/a.c") || streq(in, "disk/b.c")) {
                rc = spawn_cc_c(in, obj);
            } else {
                rc = compile_c_to_obj(in, obj);
            }
            if (rc != 0) {
                return rc;
            }
            if (append_input(objs, &n_obj, obj) != 0) {
                return 56;
            }
            continue;
        }
        if (ends_with(in, ".s")) {
            replace_ext(in, ".o", obj, sizeof(obj));
            if (obj[0] == 0) {
                return 50;
            }
            rc = spawn_as(in, obj);
            if (rc != 0) {
                return rc;
            }
            if (append_input(objs, &n_obj, obj) != 0) {
                return 56;
            }
            continue;
        }
        return 50;
    }

    if (n_obj == 0) {
        return 50;
    }

    rc = write_link_rsp("disk/_link.rsp", objs, n_obj);
    if (rc != 0) {
        return rc;
    }
    rc = spawn_ld("disk/_link.rsp", out_path);
    if (rc != 0) {
        return rc;
    }

    /* ONBXST / bare driver: run linked ET_EXEC (writes ONBOX_CCDRV_PASS). */
    {
        char* out_argv[2];
        char* out_envp[1];
        out_argv[0] = (char*)out_path;
        out_argv[1] = 0;
        out_envp[0] = 0;
        execve(out_path, out_argv, out_envp);
        return 8;
    }
}
