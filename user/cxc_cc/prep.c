/* Expand #include / #link / #lib; M291: comments, #define, #if/#ifdef. */
#include "cxc_cc/cc.h"

#ifdef CC_HOST
#include <stdio.h>
#else
#include "mini_libc/fcntl.h"
#include "mini_libc/unistd.h"
#endif

enum {
    CC_INCLUDE_DEPTH = 4,
    CC_MACRO_MAX = 32,
    CC_MACRO_NAME = 24,
    CC_MACRO_BODY = 64,
    CC_IF_MAX = 8
};

static char g_links[CC_LINK_MAX][128];
static int g_nlinks;
static char g_libs[CC_LINK_MAX][128];
static int g_nlibs;

typedef struct CcMacro {
    char name[CC_MACRO_NAME];
    char body[CC_MACRO_BODY];
} CcMacro;

static CcMacro g_macros[CC_MACRO_MAX];
static int g_nmacros;
static const char* g_prep_src;
static size_t g_prep_src_len;
static size_t g_prep_cursor;
static int g_prep_fail_line;
static int g_prep_fail_col;

static void prep_loc_at(size_t at, int* line, int* col) {
    int L = 1;
    int C = 1;
    size_t i;
    if (!g_prep_src) {
        *line = 1;
        *col = 1;
        return;
    }
    for (i = 0; i < at && i < g_prep_src_len; i++) {
        if (g_prep_src[i] == '\n') {
            L++;
            C = 1;
        } else {
            C++;
        }
    }
    *line = L;
    *col = C;
}

static void prep_mark_fail(size_t at) {
    prep_loc_at(at, &g_prep_fail_line, &g_prep_fail_col);
}

int cc_prep_last_fail_line(void) {
    return g_prep_fail_line;
}

int cc_prep_last_fail_col(void) {
    return g_prep_fail_col;
}

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

static void macros_reset(void) {
    g_nmacros = 0;
}

static int macro_find(const char* name) {
    int i;
    for (i = 0; i < g_nmacros; i++) {
        if (strcmp(g_macros[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int macro_define(const char* name, const char* body) {
    size_t ni = 0;
    size_t bi = 0;
    int idx;
    if (!name || !name[0] || g_nmacros >= CC_MACRO_MAX) {
        return -1;
    }
    idx = macro_find(name);
    if (idx < 0) {
        idx = g_nmacros++;
    }
    while (name[ni] != 0 && ni + 1 < CC_MACRO_NAME) {
        g_macros[idx].name[ni] = name[ni];
        ni++;
    }
    g_macros[idx].name[ni] = 0;
    while (body && body[bi] != 0 && bi + 1 < CC_MACRO_BODY) {
        g_macros[idx].body[bi] = body[bi];
        bi++;
    }
    g_macros[idx].body[bi] = 0;
    return 0;
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

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int match_directive(const char* in, size_t in_len, size_t i,
                           const char* kw) {
    size_t k = 0;
    size_t j = i;
    while (kw[k] != 0) {
        if (j >= in_len || in[j] != kw[k]) {
            return 0;
        }
        j++;
        k++;
    }
    if (j < in_len && is_ident_char(in[j])) {
        return 0;
    }
    return 1;
}

static size_t skip_line(const char* in, size_t in_len, size_t j) {
    while (j < in_len && in[j] != '\n') {
        j++;
    }
    if (j < in_len && in[j] == '\n') {
        j++;
    }
    return j;
}

static size_t skip_ws(const char* in, size_t in_len, size_t j) {
    while (j < in_len && (in[j] == ' ' || in[j] == '\t')) {
        j++;
    }
    return j;
}

static int read_ident(const char* in, size_t in_len, size_t* j, char* buf,
                      size_t cap) {
    size_t n = 0;
    size_t i = *j;
    if (i >= in_len || !is_ident_start(in[i])) {
        return -1;
    }
    while (i < in_len && is_ident_char(in[i])) {
        if (n + 1 < cap) {
            buf[n++] = in[i];
        }
        i++;
    }
    buf[n] = 0;
    *j = i;
    return n > 0 ? 0 : -1;
}

static int parse_quoted_path(const char* in, size_t in_len, size_t* j,
                             size_t* path_at, size_t* path_len) {
    size_t i = skip_ws(in, in_len, *j);
    if (i >= in_len || in[i] != '"') {
        return -1;
    }
    i++;
    *path_at = i;
    while (i < in_len && in[i] != '"' && in[i] != '\n') {
        i++;
    }
    if (i >= in_len || in[i] != '"') {
        return -1;
    }
    *path_len = i - *path_at;
    i++;
    *j = i;
    return 0;
}

static int expand(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_n, int depth) {
    size_t i = 0;
    int if_stack[CC_IF_MAX];
    int if_n = 0;
    int skipping = 0;

    while (i < in_len) {
        g_prep_cursor = i;
        /* Line comments */
        if (!skipping && in[i] == '/' && i + 1 < in_len && in[i + 1] == '/') {
            i = skip_line(in, in_len, i);
            if (append_bytes(out, out_cap, out_n, "\n", 1) != 0) {
                return -1;
            }
            continue;
        }
        /* Block comments */
        if (!skipping && in[i] == '/' && i + 1 < in_len && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in_len && !(in[i] == '*' && in[i + 1] == '/')) {
                i++;
            }
            if (i + 1 >= in_len) {
                return -1;
            }
            i += 2;
            if (append_bytes(out, out_cap, out_n, " ", 1) != 0) {
                return -1;
            }
            continue;
        }

        if (is_line_start(in, i) && i < in_len && in[i] == '#') {
            size_t j = i + 1;
            j = skip_ws(in, in_len, j);

            if (match_directive(in, in_len, j, "include")) {
                size_t path_at = 0;
                size_t path_len = 0;
                j += 7;
                if (parse_quoted_path(in, in_len, &j, &path_at, &path_len) !=
                    0) {
                    return -1;
                }
                j = skip_line(in, in_len, j);
                if (!skipping) {
                    if (handle_include(in + path_at, path_len, out, out_cap,
                                       out_n, depth) != 0) {
                        return -1;
                    }
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "link")) {
                size_t path_at = 0;
                size_t path_len = 0;
                j += 4;
                if (parse_quoted_path(in, in_len, &j, &path_at, &path_len) !=
                    0) {
                    return -1;
                }
                j = skip_line(in, in_len, j);
                if (!skipping && add_link(in + path_at, path_len) != 0) {
                    return -1;
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "lib")) {
                size_t path_at = 0;
                size_t path_len = 0;
                j += 3;
                if (parse_quoted_path(in, in_len, &j, &path_at, &path_len) !=
                    0) {
                    return -1;
                }
                j = skip_line(in, in_len, j);
                if (!skipping && add_lib(in + path_at, path_len) != 0) {
                    return -1;
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "define")) {
                char name[CC_MACRO_NAME];
                char body[CC_MACRO_BODY];
                size_t bn = 0;
                j += 6;
                j = skip_ws(in, in_len, j);
                if (read_ident(in, in_len, &j, name, sizeof(name)) != 0) {
                    return -1;
                }
                /* Object macro only (no '(') — function macros: single-level
                 * body copy without arg substitution for now. */
                if (j < in_len && in[j] == '(') {
                    /* Skip params; body until newline; store as empty body fail
                     * → treat as object body after ')'. */
                    while (j < in_len && in[j] != ')' && in[j] != '\n') {
                        j++;
                    }
                    if (j >= in_len || in[j] != ')') {
                        return -1;
                    }
                    j++;
                }
                j = skip_ws(in, in_len, j);
                while (j < in_len && in[j] != '\n') {
                    if (bn + 1 < sizeof(body)) {
                        body[bn++] = in[j];
                    }
                    j++;
                }
                while (bn > 0 &&
                       (body[bn - 1] == ' ' || body[bn - 1] == '\t')) {
                    bn--;
                }
                body[bn] = 0;
                if (j < in_len && in[j] == '\n') {
                    j++;
                }
                if (!skipping && macro_define(name, body) != 0) {
                    return -1;
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "ifdef") ||
                match_directive(in, in_len, j, "ifndef")) {
                int ifndef = match_directive(in, in_len, j, "ifndef");
                char name[CC_MACRO_NAME];
                int take;
                j += ifndef ? 6 : 5;
                j = skip_ws(in, in_len, j);
                if (read_ident(in, in_len, &j, name, sizeof(name)) != 0) {
                    return -1;
                }
                j = skip_line(in, in_len, j);
                take = ifndef ? (macro_find(name) < 0) : (macro_find(name) >= 0);
                if (if_n >= CC_IF_MAX) {
                    return -1;
                }
                if_stack[if_n++] = (!skipping) && take;
                {
                    int s = 0;
                    int k;
                    for (k = 0; k < if_n; k++) {
                        if (!if_stack[k]) {
                            s = 1;
                            break;
                        }
                    }
                    skipping = s;
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "if")) {
                int take = 0;
                j += 2;
                j = skip_ws(in, in_len, j);
                if (j < in_len && in[j] == '1') {
                    take = 1;
                    j++;
                } else if (j < in_len && in[j] == '0') {
                    take = 0;
                    j++;
                } else {
                    return -1; /* only #if 0 / #if 1 */
                }
                j = skip_line(in, in_len, j);
                if (if_n >= CC_IF_MAX) {
                    return -1;
                }
                if_stack[if_n++] = (!skipping) && take;
                {
                    int s = 0;
                    int k;
                    for (k = 0; k < if_n; k++) {
                        if (!if_stack[k]) {
                            s = 1;
                            break;
                        }
                    }
                    skipping = s;
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "else")) {
                j = skip_line(in, in_len, j + 4);
                if (if_n <= 0) {
                    return -1;
                }
                /* Flip only if parent is active: stack[n-2] all true. */
                {
                    int parent_ok = 1;
                    int k;
                    for (k = 0; k < if_n - 1; k++) {
                        if (!if_stack[k]) {
                            parent_ok = 0;
                            break;
                        }
                    }
                    if (parent_ok) {
                        if_stack[if_n - 1] = !if_stack[if_n - 1];
                    }
                }
                {
                    int s = 0;
                    int k;
                    for (k = 0; k < if_n; k++) {
                        if (!if_stack[k]) {
                            s = 1;
                            break;
                        }
                    }
                    skipping = s;
                }
                i = j;
                continue;
            }
            if (match_directive(in, in_len, j, "endif")) {
                j = skip_line(in, in_len, j + 5);
                if (if_n <= 0) {
                    return -1;
                }
                if_n--;
                {
                    int s = 0;
                    int k;
                    for (k = 0; k < if_n; k++) {
                        if (!if_stack[k]) {
                            s = 1;
                            break;
                        }
                    }
                    skipping = s;
                }
                i = j;
                continue;
            }
            /* Unknown # directive: leave for lexer hard-fail when not skipping */
            if (skipping) {
                i = skip_line(in, in_len, i);
                continue;
            }
        }

        if (skipping) {
            if (in[i] == '\n') {
                if (append_bytes(out, out_cap, out_n, "\n", 1) != 0) {
                    return -1;
                }
            }
            i++;
            continue;
        }

        /* Macro expand: identifier → body */
        if (is_ident_start(in[i])) {
            char name[CC_MACRO_NAME];
            size_t j = i;
            int mi;
            if (read_ident(in, in_len, &j, name, sizeof(name)) != 0) {
                return -1;
            }
            mi = macro_find(name);
            if (mi >= 0) {
                size_t bl = 0;
                while (g_macros[mi].body[bl] != 0) {
                    bl++;
                }
                if (append_bytes(out, out_cap, out_n, g_macros[mi].body, bl) !=
                    0) {
                    return -1;
                }
                i = j;
                continue;
            }
            if (append_bytes(out, out_cap, out_n, in + i, j - i) != 0) {
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
    if (if_n != 0) {
        g_prep_cursor = i < in_len ? i : (in_len > 0 ? in_len - 1 : 0);
        return -1;
    }
    return 0;
}

int cc_preprocess(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_len) {
    *out_len = 0;
    g_prep_src = in;
    g_prep_src_len = in_len;
    g_prep_cursor = 0;
    g_prep_fail_line = 1;
    g_prep_fail_col = 1;
    if (!in || !out || out_cap < 2) {
        prep_mark_fail(0);
        return -1;
    }
    macros_reset();
    if (expand(in, in_len, out, out_cap - 1, out_len, 0) != 0) {
        prep_mark_fail(g_prep_cursor);
        return -1;
    }
    out[*out_len] = 0;
    return 0;
}

int cc_read_source(const char* path, char* buf, size_t cap, size_t* out_n) {
    return read_file(path, buf, cap, out_n);
}
