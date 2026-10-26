/* Expand #include "path" (disk/-relative) before parse. */
#include "cxc_cc/cc.h"

#ifdef CC_HOST
#include <stdio.h>
#else
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"
#endif

enum { CC_INCLUDE_DEPTH = 4 };

static char g_links[CC_LINK_MAX][128];
static int g_nlinks;
static char g_libs[CC_LINK_MAX][128];
static int g_nlibs;

void cc_link_reset(void) {
    g_nlinks = 0;
}

void cc_lib_reset(void) {
    g_nlibs = 0;
}

int cc_lib_count(void) {
    return g_nlibs;
}

const char* cc_lib_at(int i) {
    if (i < 0 || i >= g_nlibs) {
        return 0;
    }
    return g_libs[i];
}

int cc_link_count(void) {
    return g_nlinks;
}

const char* cc_link_at(int i) {
    if (i < 0 || i >= g_nlinks) {
        return 0;
    }
    return g_links[i];
}

static int make_disk_path(const char* path, size_t path_len, char* full,
                          size_t full_cap) {
    size_t i;
    if (path_len == 0 || path_len >= 96 || full_cap < 8) {
        return -1;
    }
    if (path_len >= 5 && path[0] == 'd' && path[1] == 'i' && path[2] == 's' &&
        path[3] == 'k' && path[4] == '/') {
        if (path_len + 1 > full_cap) {
            return -1;
        }
        for (i = 0; i < path_len; i++) {
            full[i] = path[i];
        }
        full[path_len] = 0;
        return 0;
    }
    if (5 + path_len + 1 > full_cap) {
        return -1;
    }
    full[0] = 'd';
    full[1] = 'i';
    full[2] = 's';
    full[3] = 'k';
    full[4] = '/';
    for (i = 0; i < path_len; i++) {
        full[5 + i] = path[i];
    }
    full[5 + path_len] = 0;
    return 0;
}

static int add_link(const char* path, size_t path_len) {
    char full[128];
    int i;
    if (g_nlinks >= CC_LINK_MAX) {
        return -1;
    }
    if (make_disk_path(path, path_len, full, sizeof(full)) != 0) {
        return -1;
    }
    for (i = 0; i < g_nlinks; i++) {
        if (strcmp(g_links[i], full) == 0) {
            return 0;
        }
    }
    i = 0;
    while (full[i] != 0) {
        g_links[g_nlinks][i] = full[i];
        i++;
    }
    g_links[g_nlinks][i] = 0;
    g_nlinks++;
    return 0;
}

static int add_lib(const char* path, size_t path_len) {
    char full[128];
    int i;
    if (g_nlibs >= CC_LINK_MAX) {
        return -1;
    }
    if (make_disk_path(path, path_len, full, sizeof(full)) != 0) {
        return -1;
    }
    for (i = 0; i < g_nlibs; i++) {
        if (strcmp(g_libs[i], full) == 0) {
            return 0;
        }
    }
    i = 0;
    while (full[i] != 0) {
        g_libs[g_nlibs][i] = full[i];
        i++;
    }
    g_libs[g_nlibs][i] = 0;
    g_nlibs++;
    return 0;
}

static int append_bytes(char* out, size_t cap, size_t* n, const char* p,
                        size_t len) {
    size_t i;
    if (*n + len >= cap) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        out[(*n)++] = p[i];
    }
    return 0;
}

static int read_file(const char* path, char* buf, size_t cap, size_t* out_n) {
#ifdef CC_HOST
    FILE* f = fopen(path, "rb");
    size_t n;
    if (!f) {
        return -1;
    }
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    *out_n = n;
    return 0;
#else
    int fd = open(path, O_RDONLY, 0);
    size_t n = 0;
    if (fd < 0) {
        return -1;
    }
    while (n < cap - 1) {
        size_t want = cap - 1 - n;
        ssize_t r;
        if (want > 4096) {
            want = 4096;
        }
        r = read(fd, buf + n, want);
        if (r < 0) {
            close(fd);
            return -1;
        }
        if (r == 0) {
            break;
        }
        n += (size_t)r;
    }
    close(fd);
    buf[n] = 0;
    *out_n = n;
    return 0;
#endif
}

static int expand(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_n, int depth);

static int handle_include(const char* path, size_t path_len, char* out,
                          size_t out_cap, size_t* out_n, int depth) {
    char full[128];
    static char file_buf[CC_SRC_MAX];
    size_t file_len = 0;
    if (depth >= CC_INCLUDE_DEPTH || path_len == 0 || path_len >= 96) {
        return -1;
    }
    if (make_disk_path(path, path_len, full, sizeof(full)) != 0) {
        return -1;
    }
    if (read_file(full, file_buf, sizeof(file_buf), &file_len) != 0) {
        return -1;
    }
    return expand(file_buf, file_len, out, out_cap, out_n, depth + 1);
}

static int is_line_start(const char* in, size_t i) {
    return i == 0 || in[i - 1] == '\n';
}

static int expand(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_n, int depth) {
    size_t i = 0;
    while (i < in_len) {
        if (is_line_start(in, i) && i + 10 <= in_len && in[i] == '#' &&
            in[i + 1] == 'i' && in[i + 2] == 'n' && in[i + 3] == 'c' &&
            in[i + 4] == 'l' && in[i + 5] == 'u' && in[i + 6] == 'd' &&
            in[i + 7] == 'e') {
            size_t j = i + 8;
            size_t path_at;
            size_t path_len;
            while (j < in_len && (in[j] == ' ' || in[j] == '\t')) {
                j++;
            }
            if (j >= in_len || in[j] != '"') {
                return -1;
            }
            j++;
            path_at = j;
            while (j < in_len && in[j] != '"' && in[j] != '\n') {
                j++;
            }
            if (j >= in_len || in[j] != '"') {
                return -1;
            }
            path_len = j - path_at;
            j++;
            while (j < in_len && in[j] != '\n') {
                j++;
            }
            if (j < in_len && in[j] == '\n') {
                j++;
            }
            if (handle_include(in + path_at, path_len, out, out_cap, out_n,
                               depth) != 0) {
                return -1;
            }
            i = j;
            continue;
        }
        if (is_line_start(in, i) && i + 5 <= in_len && in[i] == '#' &&
            in[i + 1] == 'l' && in[i + 2] == 'i' && in[i + 3] == 'n' &&
            in[i + 4] == 'k') {
            size_t j = i + 5;
            size_t path_at;
            size_t path_len;
            while (j < in_len && (in[j] == ' ' || in[j] == '\t')) {
                j++;
            }
            if (j >= in_len || in[j] != '"') {
                return -1;
            }
            j++;
            path_at = j;
            while (j < in_len && in[j] != '"' && in[j] != '\n') {
                j++;
            }
            if (j >= in_len || in[j] != '"') {
                return -1;
            }
            path_len = j - path_at;
            j++;
            while (j < in_len && in[j] != '\n') {
                j++;
            }
            if (j < in_len && in[j] == '\n') {
                j++;
            }
            if (add_link(in + path_at, path_len) != 0) {
                return -1;
            }
            i = j;
            continue;
        }
        if (is_line_start(in, i) && i + 4 <= in_len && in[i] == '#' &&
            in[i + 1] == 'l' && in[i + 2] == 'i' && in[i + 3] == 'b') {
            size_t j = i + 4;
            size_t path_at;
            size_t path_len;
            while (j < in_len && (in[j] == ' ' || in[j] == '\t')) {
                j++;
            }
            if (j >= in_len || in[j] != '"') {
                return -1;
            }
            j++;
            path_at = j;
            while (j < in_len && in[j] != '"' && in[j] != '\n') {
                j++;
            }
            if (j >= in_len || in[j] != '"') {
                return -1;
            }
            path_len = j - path_at;
            j++;
            while (j < in_len && in[j] != '\n') {
                j++;
            }
            if (j < in_len && in[j] == '\n') {
                j++;
            }
            if (add_lib(in + path_at, path_len) != 0) {
                return -1;
            }
            i = j;
            continue;
        }
        if (append_bytes(out, out_cap, out_n, in + i, 1) != 0) {
            return -1;
        }
        i++;
    }
    return 0;
}

int cc_preprocess(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_len) {
    *out_len = 0;
    if (!in || !out || out_cap < 2) {
        return -1;
    }
    if (expand(in, in_len, out, out_cap - 1, out_len, 0) != 0) {
        return -1;
    }
    out[*out_len] = 0;
    return 0;
}

int cc_read_source(const char* path, char* buf, size_t cap, size_t* out_n) {
    return read_file(path, buf, cap, out_n);
}
