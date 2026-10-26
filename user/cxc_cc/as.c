/* M127: freestanding GAS-ish assembler → SysV ET_REL (A2 as subset). */
#include "cxc_cc/cc.h"

enum {
    AS_FAIL = 40,
    AS_TOK_MAX = 64
};

static int g_as_fail;

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_ident0(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '.';
}

static int is_ident(char c) {
    return is_ident0(c) || (c >= '0' && c <= '9');
}

static int parse_imm(const char* s, long* out) {
    long v = 0;
    int neg = 0;
    size_t i = 0;
    if (s[0] == '-') {
        neg = 1;
        i = 1;
    }
    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        if (!s[i]) {
            return -1;
        }
        while (s[i]) {
            char c = s[i];
            int d;
            if (c >= '0' && c <= '9') {
                d = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                d = 10 + c - 'a';
            } else if (c >= 'A' && c <= 'F') {
                d = 10 + c - 'A';
            } else {
                return -1;
            }
            v = (v << 4) + d;
            i++;
        }
    } else {
        if (!s[i]) {
            return -1;
        }
        while (s[i]) {
            if (s[i] < '0' || s[i] > '9') {
                return -1;
            }
            v = v * 10 + (s[i] - '0');
            i++;
        }
    }
    *out = neg ? -v : v;
    return 0;
}

static void skip_ws(const char** pp, const char* end) {
    const char* p = *pp;
    while (p < end && is_space(*p)) {
        p++;
    }
    *pp = p;
}

static int read_tok(const char** pp, const char* end, char* out, size_t cap) {
    const char* p = *pp;
    size_t n = 0;
    skip_ws(&p, end);
    if (p >= end || *p == '\n' || *p == '#' || *p == ';') {
        out[0] = 0;
        *pp = p;
        return 0;
    }
    if (*p == ',' || *p == ':') {
        if (cap < 2) {
            return -1;
        }
        out[0] = *p;
        out[1] = 0;
        *pp = p + 1;
        return 1;
    }
    if (*p == '%') {
        out[n++] = *p++;
        while (p < end && n + 1 < cap && is_ident(*p)) {
            out[n++] = *p++;
        }
        out[n] = 0;
        *pp = p;
        return n > 1 ? 1 : -1;
    }
    if (*p == '$') {
        out[n++] = *p++;
        while (p < end && n + 1 < cap &&
               (is_ident(*p) || *p == '-' || *p == '+')) {
            out[n++] = *p++;
        }
        out[n] = 0;
        *pp = p;
        return n > 1 ? 1 : -1;
    }
    if (is_ident0(*p) || (*p >= '0' && *p <= '9') || *p == '-') {
        while (p < end && n + 1 < cap &&
               (is_ident(*p) || *p == '-' ||
                (n > 0 && (*p == 'x' || *p == 'X')))) {
            out[n++] = *p++;
        }
        out[n] = 0;
        *pp = p;
        return 1;
    }
    return -1;
}

static int eq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int ensure_fn(CcEmit* em, const char* name, int define_here) {
    int fi = cc_fn_find(em, name);
    size_t n = 0;
    CcFn* fn;
    if (fi >= 0) {
        if (define_here) {
            if (em->fns[fi].defined) {
                return -1;
            }
            em->fns[fi].entry = em->code_len;
            em->fns[fi].defined = 1;
            em->fns[fi].sysv = 1;
        }
        return fi;
    }
    if (em->nfns >= CC_FN_MAX) {
        return -1;
    }
    fn = &em->fns[em->nfns];
    while (n < CC_IDENT_MAX && name[n]) {
        fn->name[n] = name[n];
        n++;
    }
    if (name[n] != 0) {
        return -1;
    }
    fn->name[n] = 0;
    fn->arity = 2;
    fn->sysv = 1;
    fn->defined = 0;
    fn->entry = 0;
    if (define_here) {
        fn->entry = em->code_len;
        fn->defined = 1;
    }
    em->nfns++;
    return em->nfns - 1;
}

static int emit_call(CcEmit* em, const char* name) {
    int fi = ensure_fn(em, name, 0);
    size_t at;
    if (fi < 0) {
        return -1;
    }
    at = cc_emit_call_rel32(em);
    if (em->failed || em->ncalls >= CC_CALL_MAX) {
        return -1;
    }
    em->calls[em->ncalls].at = at;
    em->calls[em->ncalls].fn = fi;
    em->ncalls++;
    return 0;
}

int as_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len) {
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];
    const char* p = src;
    const char* end = src + len;
    int in_text = 0;
    int cur_fn = -1;
    int i;

    g_as_fail = 0;
    memset(&em, 0, sizeof(em));
    em.code = code_buf;
    em.data = data_buf;
    em.code_cap = CC_CODE_MAX;
    em.data_cap = CC_DATA_MAX;
    em.obj_mode = 1;
    cc_reloc_reset();

    while (p < end) {
        char t0[AS_TOK_MAX];
        char t1[AS_TOK_MAX];
        char t2[AS_TOK_MAX];
        char t3[AS_TOK_MAX];
        const char* line = p;
        skip_ws(&p, end);
        if (p >= end) {
            break;
        }
        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p == '#' || *p == ';') {
            while (p < end && *p != '\n') {
                p++;
            }
            continue;
        }
        if (read_tok(&p, end, t0, sizeof(t0)) < 0) {
            g_as_fail = AS_FAIL;
            return -1;
        }
        if (t0[0] == 0) {
            while (p < end && *p != '\n') {
                p++;
            }
            continue;
        }
        /* label: */
        {
            const char* save = p;
            char colon[4];
            if (read_tok(&p, end, colon, sizeof(colon)) > 0 && colon[0] == ':' &&
                colon[1] == 0) {
                if (!in_text) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                cur_fn = ensure_fn(&em, t0, 1);
                if (cur_fn < 0 || em.failed) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                while (p < end && *p != '\n') {
                    if (!is_space(*p)) {
                        break;
                    }
                    p++;
                }
                if (p < end && *p == '\n') {
                    p++;
                }
                continue;
            }
            p = save;
        }
        if (eq(t0, ".text")) {
            in_text = 1;
        } else if (eq(t0, ".globl") || eq(t0, ".global")) {
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || !t1[0]) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (ensure_fn(&em, t1, 0) < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "ret")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0xC3);
        } else if (eq(t0, "nop")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0x90);
        } else if (eq(t0, "call")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || !t1[0]) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (emit_call(&em, t1) != 0 || em.failed) {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "mov") || eq(t0, "movl") || eq(t0, "movq")) {
            long imm = 0;
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (t1[0] == '$') {
                if (parse_imm(t1 + 1, &imm) != 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t3, "%eax") || eq(t3, "%rax")) {
                    cc_emit_byte(&em, 0xB8);
                    cc_emit_u32(&em, (unsigned)imm);
                } else if (eq(t3, "%esi") || eq(t3, "%rsi")) {
                    cc_emit_byte(&em, 0xBE);
                    cc_emit_u32(&em, (unsigned)imm);
                } else if (eq(t3, "%edi") || eq(t3, "%rdi")) {
                    cc_emit_byte(&em, 0xBF);
                    cc_emit_u32(&em, (unsigned)imm);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else if (t1[0] == '%') {
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 || t3[0] != '%') {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t1, "%edi") && eq(t3, "%eax")) {
                    cc_emit_bytes(&em, "\x89\xF8", 2);
                } else if (eq(t1, "%esi") && eq(t3, "%eax")) {
                    cc_emit_bytes(&em, "\x89\xF0", 2);
                } else if (eq(t1, "%edi") && eq(t3, "%esi")) {
                    cc_emit_bytes(&em, "\x89\xFE", 2);
                } else if (eq(t1, "%eax") && eq(t3, "%edi")) {
                    cc_emit_bytes(&em, "\x89\xC7", 2);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "add") || eq(t0, "addl")) {
            long imm = 0;
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (t1[0] == '$') {
                if (parse_imm(t1 + 1, &imm) != 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (imm >= -128 && imm <= 127) {
                    cc_emit_bytes(&em, "\x83\xC0", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_byte(&em, 0x05);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (eq(t1, "%esi")) {
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                cc_emit_bytes(&em, "\x01\xF0", 2);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "xor") || eq(t0, "xorl")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t2, sizeof(t2)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t1, "%eax") && eq(t2, "%eax")) {
                cc_emit_bytes(&em, "\x31\xC0", 2);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else {
            (void)line;
            g_as_fail = AS_FAIL;
            return -1;
        }
        skip_ws(&p, end);
        if (p < end && *p != '\n' && *p != '#' && *p != ';') {
            g_as_fail = AS_FAIL;
            return -1;
        }
        while (p < end && *p != '\n') {
            p++;
        }
        if (p < end && *p == '\n') {
            p++;
        }
        if (em.failed) {
            g_as_fail = AS_FAIL;
            return -1;
        }
    }

    if (em.nfns < 1 || em.code_len == 0) {
        g_as_fail = AS_FAIL;
        return -1;
    }
    for (i = 0; i < em.nfns; i++) {
        if (!em.fns[i].defined) {
            /* UND ok only if called; cc_build_rel_elf handles */
            continue;
        }
    }
    if (cc_build_rel_elf(&em, out_elf, out_len) != 0) {
        g_as_fail = AS_FAIL;
        return -1;
    }
    return 0;
}

int as_last_fail_code(void) {
    return g_as_fail;
}
