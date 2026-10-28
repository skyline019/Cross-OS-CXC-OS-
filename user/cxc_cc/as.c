/* M127–M131: freestanding GAS-ish assembler → SysV ET_REL (A2 as subset). */
#include "cxc_cc/cc.h"

enum {
    AS_FAIL = 40,
    AS_TOK_MAX = 64,
    AS_DATA_MAX = 32,
    AS_STR_MAX = 64,
    AS_LEA_PEND = 48,
    AS_LOC_MAX = 48,
    AS_BR_PEND = 96 /* M224: probe walk/ctype branch surface */
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
    unsigned long long v = 0;
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
            unsigned d;
            if (c >= '0' && c <= '9') {
                d = (unsigned)(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = 10u + (unsigned)(c - 'a');
            } else if (c >= 'A' && c <= 'F') {
                d = 10u + (unsigned)(c - 'A');
            } else {
                return -1;
            }
            if (v > (0xffffffffffffffffull >> 4)) {
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
            unsigned d;
            if (s[i] < '0' || s[i] > '9') {
                return -1;
            }
            d = (unsigned)(s[i] - '0');
            if (v > 922337203685477580ull ||
                (v == 922337203685477580ull && d > (neg ? 8u : 7u))) {
                return -1;
            }
            v = v * 10ull + d;
            i++;
        }
    }
    if (neg) {
        if (v > 9223372036854775808ull) {
            return -1;
        }
        *out = -(long)v;
    } else {
        if (v > 9223372036854775807ull) {
            return -1;
        }
        *out = (long)v;
    }
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
    if (*p == ',' || *p == ':' || *p == '(' || *p == ')') {
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

/* Parse "..." into out; advances past closing quote. */
static int read_quoted(const char** pp, const char* end, char* out, size_t cap,
                       size_t* out_n) {
    const char* p = *pp;
    size_t n = 0;
    skip_ws(&p, end);
    if (p >= end || *p != '"') {
        return -1;
    }
    p++;
    while (p < end && *p != '"' && *p != '\n') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char e = *p++;
            if (e == 'n') {
                c = '\n';
            } else if (e == 't') {
                c = '\t';
            } else if (e == '0') {
                c = 0;
            } else if (e == '"' || e == '\\') {
                c = e;
            } else {
                return -1;
            }
        }
        if (n + 1 >= cap) {
            return -1;
        }
        out[n++] = c;
    }
    if (p >= end || *p != '"') {
        return -1;
    }
    p++;
    out[n] = 0;
    *out_n = n;
    *pp = p;
    return 0;
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

static int data_find(char names[][CC_IDENT_MAX + 1], size_t* offs, int n,
                     const char* name) {
    int i;
    for (i = 0; i < n; i++) {
        if (eq(names[i], name)) {
            return i;
        }
    }
    (void)offs;
    return -1;
}

static int data_define(char names[][CC_IDENT_MAX + 1], size_t* offs, int* n,
                       const char* name, size_t off) {
    size_t k = 0;
    if (*n >= AS_DATA_MAX || data_find(names, offs, *n, name) >= 0) {
        return -1;
    }
    while (k < CC_IDENT_MAX && name[k]) {
        names[*n][k] = name[k];
        k++;
    }
    if (name[k] != 0) {
        return -1;
    }
    names[*n][k] = 0;
    offs[*n] = off;
    (*n)++;
    return *n - 1;
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
    static unsigned char ro_buf[CC_DATA_MAX];
    char dnames[AS_DATA_MAX][CC_IDENT_MAX + 1];
    size_t doffs[AS_DATA_MAX];
    int ndata = 0;
    char lea_names[AS_LEA_PEND][CC_IDENT_MAX + 1];
    int lea_ri[AS_LEA_PEND];
    int nlea_pend = 0;
    char lnames[AS_LOC_MAX][CC_IDENT_MAX + 1];
    size_t loffs[AS_LOC_MAX];
    int nloc = 0;
    char br_names[AS_BR_PEND][CC_IDENT_MAX + 1];
    size_t br_at[AS_BR_PEND];
    int nbr = 0;
    const char* p = src;
    const char* end = src + len;
    int in_text = 0;
    int in_rodata = 0; /* true for .rodata/.data/.bss content regions */
    int in_bss = 0;
    int cur_fn = -1;
    int i;

    g_as_fail = 0;
    memset(&em, 0, sizeof(em));
    em.code = code_buf;
    em.data = data_buf;
    em.ro = ro_buf;
    em.code_cap = CC_CODE_MAX;
    em.data_cap = CC_DATA_MAX;
    em.ro_cap = CC_DATA_MAX;
    em.obj_mode = 1;
    cc_reloc_reset();

    while (p < end) {
        char t0[AS_TOK_MAX];
        char t1[AS_TOK_MAX];
        char t2[AS_TOK_MAX];
        char t3[AS_TOK_MAX];
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
                if (in_rodata) {
                    size_t off = cc_data_cur_off(&em);
                    if (data_define(dnames, doffs, &ndata, t0, off) < 0) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                } else if (in_text) {
                    if (t0[0] == '.') {
                        if (data_define(lnames, loffs, &nloc, t0, em.code_len) <
                            0) {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                    } else {
                        cur_fn = ensure_fn(&em, t0, 1);
                        if (cur_fn < 0 || em.failed) {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                    }
                } else {
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
            in_rodata = 0;
            in_bss = 0;
        } else if (eq(t0, ".rodata")) {
            in_text = 0;
            in_rodata = 1;
            in_bss = 0;
            cur_fn = -1;
            cc_data_set_sec(&em, 1);
        } else if (eq(t0, ".data")) {
            in_text = 0;
            in_rodata = 1;
            in_bss = 0;
            cur_fn = -1;
            cc_data_set_sec(&em, 2);
        } else if (eq(t0, ".bss")) {
            in_text = 0;
            in_rodata = 1;
            in_bss = 1;
            cur_fn = -1;
            cc_data_set_sec(&em, 3);
        } else if (eq(t0, ".section")) {
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t1, ".rodata") || eq(t1, "rodata")) {
                in_text = 0;
                in_rodata = 1;
                in_bss = 0;
                cur_fn = -1;
                cc_data_set_sec(&em, 1);
            } else if (eq(t1, ".data") || eq(t1, "data")) {
                in_text = 0;
                in_rodata = 1;
                in_bss = 0;
                cur_fn = -1;
                cc_data_set_sec(&em, 2);
            } else if (eq(t1, ".bss") || eq(t1, "bss")) {
                in_text = 0;
                in_rodata = 1;
                in_bss = 1;
                cur_fn = -1;
                cc_data_set_sec(&em, 3);
            } else if (eq(t1, ".text") || eq(t1, "text")) {
                in_text = 1;
                in_rodata = 0;
                in_bss = 0;
            } else {
                /* gcc -S: .section .note.GNU-stack — ignore line. */
                while (p < end && *p != '\n') {
                    p++;
                }
                continue;
            }
        } else if (eq(t0, ".zero") || eq(t0, ".skip")) {
            long n = 0;
            char z = 0;
            if (!in_rodata) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                parse_imm(t1, &n) != 0 || n < 1 || n > 256) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (in_bss) {
                (void)cc_bss_bump(&em, (size_t)n);
                if (em.failed) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else {
                while (n-- > 0) {
                    if (em.data_sec <= 1) {
                        (void)cc_ro_push(&em, &z, 1);
                    } else {
                        (void)cc_data_push(&em, &z, 1);
                    }
                    if (em.failed) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                }
            }
        } else if (eq(t0, ".globl") || eq(t0, ".global")) {
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || !t1[0]) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* gcc -S often emits .globl before an explicit .text. */
            if (!in_text && !in_rodata) {
                in_text = 1;
            }
            /* M131: duplicate .globl is a hard reject (stable fail). */
            if (cc_fn_find(&em, t1) >= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (ensure_fn(&em, t1, 0) < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, ".asciz") || eq(t0, ".string")) {
            char sbuf[AS_STR_MAX];
            size_t sn = 0;
            if (!in_rodata || in_bss) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_quoted(&p, end, sbuf, sizeof(sbuf), &sn) != 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (em.data_sec <= 1) {
                (void)cc_ro_push(&em, sbuf, sn + 1);
            } else {
                (void)cc_data_push(&em, sbuf, sn + 1);
            }
            if (em.failed) {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, ".ascii")) {
            char sbuf[AS_STR_MAX];
            size_t sn = 0;
            if (!in_rodata || in_bss) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_quoted(&p, end, sbuf, sizeof(sbuf), &sn) != 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (sn == 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (em.data_sec <= 1) {
                (void)cc_ro_push(&em, sbuf, sn);
            } else {
                (void)cc_data_push(&em, sbuf, sn);
            }
            if (em.failed) {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, ".byte") || eq(t0, ".long") || eq(t0, ".quad") ||
                   eq(t0, ".int")) {
            long v = 0;
            unsigned char raw[8];
            int nbytes = eq(t0, ".byte") ? 1 : (eq(t0, ".quad") ? 8 : 4);
            int bi;
            if (!in_rodata || in_bss) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                parse_imm(t1, &v) != 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            for (bi = 0; bi < nbytes; bi++) {
                raw[bi] = (unsigned char)((unsigned long long)v >> (8 * bi));
            }
            if (em.data_sec <= 1) {
                (void)cc_ro_push(&em, raw, (size_t)nbytes);
            } else {
                (void)cc_data_push(&em, raw, (size_t)nbytes);
            }
            if (em.failed) {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, ".type") || eq(t0, ".size") || eq(t0, ".file") ||
                   eq(t0, ".ident") || eq(t0, ".cfi_startproc") ||
                   eq(t0, ".cfi_endproc") || eq(t0, ".cfi_def_cfa") ||
                   eq(t0, ".cfi_offset") || eq(t0, ".p2align") ||
                   eq(t0, ".align")) {
            /* gcc -S noise: skip rest of line. */
            while (p < end && *p != '\n') {
                p++;
            }
            continue;
        } else if (eq(t0, "endbr64") || eq(t0, "endbr32")) {
            /* M199: CET endbr — ignore (nop for our freestanding as). */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            continue;
        } else if (eq(t0, "leave")) {
            /* M189: leave = mov %rbp,%rsp ; pop %rbp */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0xC9);
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
        } else if (eq(t0, "lea") || eq(t0, "leaq")) {
            int di;
            size_t d_off;
            size_t k;
            long disp = 0;
            int rbp_form = 0;
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M157: lea disp(%rbp), %rax | lea name(%rip), %rax (deferred). */
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || !t1[0]) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (t1[0] == '(' || t1[0] == '-' ||
                (t1[0] >= '0' && t1[0] <= '9')) {
                rbp_form = 1;
                if (t1[0] == '(') {
                    disp = 0;
                } else {
                    if (parse_imm(t1, &disp) != 0) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != '(') {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                }
                if (read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%rbp") || eq(t3, "%ebp")) ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ')' ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%rax") || eq(t3, "%eax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (disp < -128 || disp > 127) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                cc_emit_bytes(&em, "\x48\x8D\x45", 3); /* lea disp8(%rbp), %rax */
                cc_emit_byte(&em, (unsigned char)disp);
                (void)rbp_form;
            } else {
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != '(' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%rip") || eq(t3, "rip")) ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ')' ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%rax") || eq(t3, "%eax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                di = data_find(dnames, doffs, ndata, t1);
                d_off = di >= 0 ? doffs[di] : 0;
                cc_emit_lea_data(&em, d_off);
                if (di < 0) {
                    if (nlea_pend >= AS_LEA_PEND) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    k = 0;
                    while (k < CC_IDENT_MAX && t1[k]) {
                        lea_names[nlea_pend][k] = t1[k];
                        k++;
                    }
                    if (t1[k] != 0) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    lea_names[nlea_pend][k] = 0;
                    lea_ri[nlea_pend] = cc_reloc_count() - 1;
                    nlea_pend++;
                }
            }
        } else if (eq(t0, "push") || eq(t0, "pushq")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M148: frame regs rbp/rbx/rcx/rdx + existing rax/rdi/rsi. */
            if (eq(t1, "%rax") || eq(t1, "%eax")) {
                cc_emit_byte(&em, 0x50);
            } else if (eq(t1, "%rcx") || eq(t1, "%ecx")) {
                cc_emit_byte(&em, 0x51);
            } else if (eq(t1, "%rdx") || eq(t1, "%edx")) {
                cc_emit_byte(&em, 0x52);
            } else if (eq(t1, "%rbx") || eq(t1, "%ebx")) {
                cc_emit_byte(&em, 0x53);
            } else if (eq(t1, "%rbp") || eq(t1, "%ebp")) {
                cc_emit_byte(&em, 0x55);
            } else if (eq(t1, "%rsi") || eq(t1, "%esi")) {
                cc_emit_byte(&em, 0x56);
            } else if (eq(t1, "%rdi") || eq(t1, "%edi")) {
                cc_emit_byte(&em, 0x57);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "pop") || eq(t0, "popq")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t1, "%rax") || eq(t1, "%eax")) {
                cc_emit_byte(&em, 0x58);
            } else if (eq(t1, "%rcx") || eq(t1, "%ecx")) {
                cc_emit_byte(&em, 0x59);
            } else if (eq(t1, "%rdx") || eq(t1, "%edx")) {
                cc_emit_byte(&em, 0x5A);
            } else if (eq(t1, "%rbx") || eq(t1, "%ebx")) {
                cc_emit_byte(&em, 0x5B);
            } else if (eq(t1, "%rbp") || eq(t1, "%ebp")) {
                cc_emit_byte(&em, 0x5D);
            } else if (eq(t1, "%rsi") || eq(t1, "%esi")) {
                cc_emit_byte(&em, 0x5E);
            } else if (eq(t1, "%rdi") || eq(t1, "%edi")) {
                cc_emit_byte(&em, 0x5F);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "imul") || eq(t0, "imull")) {
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
                /* imul $imm, %eax  →  imul eax, eax, imm */
                if (parse_imm(t1 + 1, &imm) != 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (imm >= -128 && imm <= 127) {
                    cc_emit_bytes(&em, "\x6B\xC0", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_bytes(&em, "\x69\xC0", 2);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (t1[0] == '%') {
                /* imul %reg, %eax */
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t1, "%eax") || eq(t1, "%rax")) {
                    cc_emit_bytes(&em, "\x0F\xAF\xC0", 3);
                } else if (eq(t1, "%edi") || eq(t1, "%rdi")) {
                    cc_emit_bytes(&em, "\x0F\xAF\xC7", 3);
                } else if (eq(t1, "%esi") || eq(t1, "%rsi")) {
                    cc_emit_bytes(&em, "\x0F\xAF\xC6", 3);
                } else if (eq(t1, "%edx") || eq(t1, "%rdx")) {
                    cc_emit_bytes(&em, "\x0F\xAF\xC2", 3);
                } else if (eq(t1, "%ecx") || eq(t1, "%rcx")) {
                    cc_emit_bytes(&em, "\x0F\xAF\xC1", 3);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "sub") || eq(t0, "subl") || eq(t0, "subq")) {
            long imm = 0;
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M253: sub %fs:0x28, %rdx — stack canary check. */
            if (eq(t1, "%fs")) {
                long fsoff = 0;
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ':' ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                    parse_imm(t2, &fsoff) != 0 ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 || t3[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%rdx") || eq(t3, "%edx")) || fsoff != 0x28) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                cc_emit_bytes(&em, "\x64\x48\x2B\x14\x25\x28\x00\x00\x00", 9);
            } else if (eq(t1, "%edx") || eq(t1, "%rdx")) {
                /* M297: sub %rdx, %rax */
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t1, "%rdx") || eq(t3, "%rax") || eq(t0, "subq")) {
                    cc_emit_bytes(&em, "\x48\x29\xD0", 3);
                } else {
                    cc_emit_bytes(&em, "\x29\xD0", 2);
                }
            } else if (eq(t1, "%ecx") || eq(t1, "%rcx")) {
                /* M300: sub %ecx, %eax — strcmp/memcmp diff */
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t1, "%rcx") || eq(t3, "%rax") || eq(t0, "subq")) {
                    cc_emit_bytes(&em, "\x48\x29\xC8", 3);
                } else {
                    cc_emit_bytes(&em, "\x29\xC8", 2);
                }
            } else if (t1[0] != '$' || parse_imm(t1 + 1, &imm) != 0) {
                g_as_fail = AS_FAIL;
                return -1;
            } else if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                       read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            } else if (eq(t3, "%rsp") || eq(t3, "%esp")) {
                /* M157/M296: frame alloc — subq $imm8|$imm32, %rsp */
                if (imm < 0 || imm > 0x7fffffffL) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (imm <= 127) {
                    cc_emit_bytes(&em, "\x48\x83\xEC", 3);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_bytes(&em, "\x48\x81\xEC", 3);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (eq(t3, "%eax") || eq(t3, "%rax")) {
                if (imm >= -128 && imm <= 127) {
                    cc_emit_bytes(&em, "\x83\xE8", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_byte(&em, 0x2D);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (eq(t3, "%edi") || eq(t3, "%rdi")) {
                /* M209: sub $imm, %edi — ctype/gcc -S */
                if (imm >= -128 && imm <= 127) {
                    cc_emit_bytes(&em, "\x83\xEF", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_bytes(&em, "\x81\xEF", 2);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (eq(t3, "%esi") || eq(t3, "%rsi")) {
                if (imm >= -128 && imm <= 127) {
                    cc_emit_bytes(&em, "\x83\xEE", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_bytes(&em, "\x81\xEE", 2);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (eq(t3, "%edx") || eq(t3, "%rdx")) {
                /* M229: sub $imm, %rdx — pair with add $imm,%rdx */
                if (imm < -128 || imm > 127) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t3, "%rdx") || eq(t0, "subq")) {
                    cc_emit_bytes(&em, "\x48\x83\xEA", 3);
                } else {
                    cc_emit_bytes(&em, "\x83\xEA", 2);
                }
                cc_emit_byte(&em, (unsigned char)imm);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "inc") || eq(t0, "incl") || eq(t0, "incq") ||
                   eq(t0, "dec") || eq(t0, "decl") || eq(t0, "decq")) {
            int is_dec = eq(t0, "dec") || eq(t0, "decl") || eq(t0, "decq");
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M229: inc/dec %rdi|%rsi|%rdx */
            if (eq(t1, "%rdi") || eq(t1, "%edi")) {
                if (eq(t1, "%rdi") || eq(t0, "incq") || eq(t0, "decq")) {
                    cc_emit_bytes(&em, is_dec ? "\x48\xFF\xCF" : "\x48\xFF\xC7",
                                  3);
                } else {
                    cc_emit_bytes(&em, is_dec ? "\xFF\xCF" : "\xFF\xC7", 2);
                }
            } else if (eq(t1, "%rsi") || eq(t1, "%esi")) {
                if (eq(t1, "%rsi") || eq(t0, "incq") || eq(t0, "decq")) {
                    cc_emit_bytes(&em, is_dec ? "\x48\xFF\xCE" : "\x48\xFF\xC6",
                                  3);
                } else {
                    cc_emit_bytes(&em, is_dec ? "\xFF\xCE" : "\xFF\xC6", 2);
                }
            } else if (eq(t1, "%rdx") || eq(t1, "%edx")) {
                if (eq(t1, "%rdx") || eq(t0, "incq") || eq(t0, "decq")) {
                    cc_emit_bytes(&em, is_dec ? "\x48\xFF\xCA" : "\x48\xFF\xC2",
                                  3);
                } else {
                    cc_emit_bytes(&em, is_dec ? "\xFF\xCA" : "\xFF\xC2", 2);
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "rolw") || eq(t0, "rorw")) {
            /* M239: rolw/rorw $imm, %ax — htons-style byte swap */
            long imm = 0;
            int is_ror = eq(t0, "rorw");
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || t1[0] != '$' ||
                parse_imm(t1 + 1, &imm) != 0 || imm < 0 || imm > 15) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                !(eq(t3, "%ax") || eq(t3, "%AX"))) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_bytes(&em, is_ror ? "\x66\xC1\xC8" : "\x66\xC1\xC0", 3);
            cc_emit_byte(&em, (unsigned char)imm);
        } else if (eq(t0, "bswap")) {
            /* M239: bswap %eax|%rax */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t1, "%eax")) {
                cc_emit_bytes(&em, "\x0F\xC8", 2);
            } else if (eq(t1, "%rax")) {
                cc_emit_bytes(&em, "\x48\x0F\xC8", 3);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "bsf") || eq(t0, "bsfl") || eq(t0, "bsfq")) {
            /* M298: bsf %eax|%rax, %eax|%rax — ffs plant leaf */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                (eq(t3, "%eax") || eq(t3, "%rax"))) {
                if (eq(t0, "bsfq") || eq(t1, "%rax") || eq(t3, "%rax")) {
                    cc_emit_bytes(&em, "\x48\x0F\xBC\xC0", 4);
                } else {
                    cc_emit_bytes(&em, "\x0F\xBC\xC0", 3);
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "cdq") || eq(t0, "cltd")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0x99); /* cdq — sign-extend eax→edx:eax */
        } else if (eq(t0, "neg") || eq(t0, "negl") || eq(t0, "negq")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M189/M295: neg %eax|%rax */
            if (eq(t1, "%rax") || eq(t0, "negq")) {
                if (!(eq(t1, "%rax") || eq(t1, "%eax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                cc_emit_bytes(&em, "\x48\xF7\xD8", 3);
            } else if (eq(t1, "%eax")) {
                cc_emit_bytes(&em, "\xF7\xD8", 2);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "idiv") || eq(t0, "idivl") || eq(t0, "div") ||
                   eq(t0, "divl")) {
            int is_udiv = eq(t0, "div") || eq(t0, "divl");
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* idiv/div %ecx|%esi|%edi — dividend in edx:eax */
            if (eq(t1, "%ecx") || eq(t1, "%rcx")) {
                cc_emit_bytes(&em, is_udiv ? "\xF7\xF1" : "\xF7\xF9", 2);
            } else if (eq(t1, "%edx") || eq(t1, "%rdx")) {
                cc_emit_bytes(&em, is_udiv ? "\xF7\xF2" : "\xF7\xFA", 2);
            } else if (eq(t1, "%esi") || eq(t1, "%rsi")) {
                cc_emit_bytes(&em, is_udiv ? "\xF7\xF6" : "\xF7\xFE", 2);
            } else if (eq(t1, "%edi") || eq(t1, "%rdi")) {
                cc_emit_bytes(&em, is_udiv ? "\xF7\xF7" : "\xF7\xFF", 2);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "movzx") || eq(t0, "movzbl") || eq(t0, "movzb") ||
                   eq(t0, "movzwl") || eq(t0, "movzw") || eq(t0, "movsx") ||
                   eq(t0, "movsbl") || eq(t0, "movsb") || eq(t0, "movswl") ||
                   eq(t0, "movsw")) {
            int is_sx = eq(t0, "movsx") || eq(t0, "movsbl") || eq(t0, "movsb") ||
                        eq(t0, "movswl") || eq(t0, "movsw");
            int is_word = eq(t0, "movzwl") || eq(t0, "movzw") ||
                          eq(t0, "movswl") || eq(t0, "movsw");
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M179/M189/M209/M219/M239: movzbl/movzwl %al|%ax|%dl|(mem), %eax */
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (t1[0] == '(' || (t1[0] != '%' && t1[0] != 0)) {
                /* M219: movzbl (%rdi|%rsi)|disp(%rdi), %eax */
                long disp = 0;
                char base[AS_TOK_MAX];
                if (is_sx || is_word) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (t1[0] == '(') {
                    disp = 0;
                    if (read_tok(&p, end, base, sizeof(base)) <= 0 ||
                        read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != ')') {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                } else {
                    if (parse_imm(t1, &disp) != 0 || disp < -128 ||
                        disp > 127) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != '(' ||
                        read_tok(&p, end, base, sizeof(base)) <= 0 ||
                        read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != ')') {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                }
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                /* M300: allow %eax|%ecx|%edx dest for memcmp/atoi plants */
                {
                    unsigned char modrm_rdi = 0x07;
                    unsigned char modrm_rsi = 0x06;
                    if (eq(t3, "%eax") || eq(t3, "%rax")) {
                        modrm_rdi = 0x07;
                        modrm_rsi = 0x06;
                    } else if (eq(t3, "%ecx") || eq(t3, "%rcx")) {
                        modrm_rdi = 0x0F;
                        modrm_rsi = 0x0E;
                    } else if (eq(t3, "%edx") || eq(t3, "%rdx")) {
                        modrm_rdi = 0x17;
                        modrm_rsi = 0x16;
                    } else {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (eq(base, "%rdi") || eq(base, "%edi")) {
                        if (disp == 0) {
                            cc_emit_bytes(&em, "\x0F\xB6", 2);
                            cc_emit_byte(&em, modrm_rdi);
                        } else {
                            cc_emit_bytes(&em, "\x0F\xB6", 2);
                            cc_emit_byte(&em, (unsigned char)(modrm_rdi | 0x40));
                            cc_emit_byte(&em, (unsigned char)disp);
                        }
                    } else if (eq(base, "%rsi") || eq(base, "%esi")) {
                        if (disp == 0) {
                            cc_emit_bytes(&em, "\x0F\xB6", 2);
                            cc_emit_byte(&em, modrm_rsi);
                        } else {
                            cc_emit_bytes(&em, "\x0F\xB6", 2);
                            cc_emit_byte(&em, (unsigned char)(modrm_rsi | 0x40));
                            cc_emit_byte(&em, (unsigned char)disp);
                        }
                    } else {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                }
            } else {
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (is_word) {
                    /* M239: movzwl/movswl %ax, %eax */
                    if (!(eq(t1, "%ax") || eq(t1, "%AX"))) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    cc_emit_bytes(&em, is_sx ? "\x0F\xBF\xC0" : "\x0F\xB7\xC0",
                                  3);
                } else if (eq(t1, "%al") || eq(t1, "%AL")) {
                    cc_emit_bytes(&em, is_sx ? "\x0F\xBE\xC0" : "\x0F\xB6\xC0",
                                  3);
                } else if (eq(t1, "%dl") || eq(t1, "%DL")) {
                    cc_emit_bytes(&em, is_sx ? "\x0F\xBE\xC2" : "\x0F\xB6\xC2",
                                  3);
                } else if (eq(t1, "%cl") || eq(t1, "%CL")) {
                    cc_emit_bytes(&em, is_sx ? "\x0F\xBE\xC1" : "\x0F\xB6\xC1",
                                  3);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            }
        } else if (eq(t0, "cmp") || eq(t0, "cmpl") || eq(t0, "cmpb")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M219: cmp %al, (%rdi) */
            if ((eq(t1, "%al") || eq(t1, "%AL")) && t3[0] == '(') {
                char base[AS_TOK_MAX];
                if (read_tok(&p, end, base, sizeof(base)) <= 0 ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ')') {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(base, "%rdi") || eq(base, "%edi")) {
                    cc_emit_bytes(&em, "\x38\x07", 2);
                } else if (eq(base, "%rsi") || eq(base, "%esi")) {
                    cc_emit_bytes(&em, "\x38\x06", 2);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else if (eq(t1, "%eax") && eq(t3, "%eax")) {
                cc_emit_bytes(&em, "\x39\xC0", 2);
            } else if (eq(t1, "%edi") && eq(t3, "%eax")) {
                cc_emit_bytes(&em, "\x39\xF8", 2);
            } else if ((eq(t1, "%ecx") || eq(t1, "%rcx")) &&
                       (eq(t3, "%eax") || eq(t3, "%rax"))) {
                /* M169: wider cmp %ecx, %eax */
                cc_emit_bytes(&em, "\x39\xC8", 2);
            } else if ((eq(t1, "%edx") || eq(t1, "%rdx")) &&
                       (eq(t3, "%eax") || eq(t3, "%rax"))) {
                cc_emit_bytes(&em, "\x39\xD0", 2);
            } else if ((eq(t1, "%edx") || eq(t1, "%rdx")) &&
                       (eq(t3, "%ecx") || eq(t3, "%rcx"))) {
                /* M297: cmp %rdx, %rcx (parse_cmp) */
                if (eq(t1, "%rdx") || eq(t3, "%rcx")) {
                    cc_emit_bytes(&em, "\x48\x39\xD1", 3);
                } else {
                    cc_emit_bytes(&em, "\x39\xD1", 2);
                }
            } else if ((eq(t1, "%esi") || eq(t1, "%rsi")) &&
                       (eq(t3, "%eax") || eq(t3, "%rax"))) {
                cc_emit_bytes(&em, "\x39\xF0", 2);
            } else if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                       (eq(t3, "%ecx") || eq(t3, "%rcx"))) {
                cc_emit_bytes(&em, "\x39\xC1", 2);
            } else if (t1[0] == '$') {
                long imm = 0;
                if (parse_imm(t1 + 1, &imm) != 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                /* M199: cmp $imm, %eax|%edi|%esi */
                if (eq(t3, "%eax") || eq(t3, "%rax")) {
                    if (imm >= -128 && imm <= 127) {
                        cc_emit_bytes(&em, "\x83\xF8", 2);
                        cc_emit_byte(&em, (unsigned char)imm);
                    } else {
                        cc_emit_byte(&em, 0x3D);
                        cc_emit_u32(&em, (unsigned)imm);
                    }
                } else if (eq(t3, "%edi") || eq(t3, "%rdi")) {
                    if (imm >= -128 && imm <= 127) {
                        cc_emit_bytes(&em, "\x83\xFF", 2);
                        cc_emit_byte(&em, (unsigned char)imm);
                    } else {
                        cc_emit_bytes(&em, "\x81\xFF", 2);
                        cc_emit_u32(&em, (unsigned)imm);
                    }
                } else if (eq(t3, "%esi") || eq(t3, "%rsi")) {
                    if (imm >= -128 && imm <= 127) {
                        cc_emit_bytes(&em, "\x83\xFE", 2);
                        cc_emit_byte(&em, (unsigned char)imm);
                    } else {
                        cc_emit_bytes(&em, "\x81\xFE", 2);
                        cc_emit_u32(&em, (unsigned)imm);
                    }
                } else if (eq(t3, "%edx") || eq(t3, "%rdx")) {
                    /* M229: cmp $imm, %rdx — pair with sub $imm,%rdx */
                    if (imm >= -128 && imm <= 127) {
                        if (eq(t3, "%rdx")) {
                            cc_emit_bytes(&em, "\x48\x83\xFA", 3);
                        } else {
                            cc_emit_bytes(&em, "\x83\xFA", 2);
                        }
                        cc_emit_byte(&em, (unsigned char)imm);
                    } else {
                        if (eq(t3, "%rdx")) {
                            cc_emit_bytes(&em, "\x48\x81\xFA", 3);
                        } else {
                            cc_emit_bytes(&em, "\x81\xFA", 2);
                        }
                        cc_emit_u32(&em, (unsigned)imm);
                    }
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "sete") || eq(t0, "setz") || eq(t0, "setbe") ||
                   eq(t0, "setna") || eq(t0, "setne") || eq(t0, "setnz") ||
                   eq(t0, "seta") || eq(t0, "setnbe") || eq(t0, "setb") ||
                   eq(t0, "setc") || eq(t0, "setnae") || eq(t0, "setl") ||
                   eq(t0, "setnge") || eq(t0, "setle") || eq(t0, "setng") ||
                   eq(t0, "setg") || eq(t0, "setnle") || eq(t0, "setge") ||
                   eq(t0, "setnl")) {
            /* M199/M209/M229/M297: setcc %al|%dl|%cl */
            unsigned char modrm = 0xC0; /* %al */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t1, "%al") || eq(t1, "%AL")) {
                modrm = 0xC0;
            } else if (eq(t1, "%dl") || eq(t1, "%DL")) {
                modrm = 0xC2;
            } else if (eq(t1, "%cl") || eq(t1, "%CL")) {
                modrm = 0xC1; /* M229 */
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t0, "setbe") || eq(t0, "setna")) {
                cc_emit_bytes(&em, "\x0F\x96", 2);
            } else if (eq(t0, "setne") || eq(t0, "setnz")) {
                cc_emit_bytes(&em, "\x0F\x95", 2);
            } else if (eq(t0, "seta") || eq(t0, "setnbe")) {
                cc_emit_bytes(&em, "\x0F\x97", 2); /* M229 */
            } else if (eq(t0, "setb") || eq(t0, "setc") ||
                       eq(t0, "setnae")) {
                cc_emit_bytes(&em, "\x0F\x92", 2); /* M229 */
            } else if (eq(t0, "setl") || eq(t0, "setnge")) {
                cc_emit_bytes(&em, "\x0F\x9C", 2); /* M297 */
            } else if (eq(t0, "setle") || eq(t0, "setng")) {
                cc_emit_bytes(&em, "\x0F\x9E", 2);
            } else if (eq(t0, "setg") || eq(t0, "setnle")) {
                cc_emit_bytes(&em, "\x0F\x9F", 2);
            } else if (eq(t0, "setge") || eq(t0, "setnl")) {
                cc_emit_bytes(&em, "\x0F\x9D", 2);
            } else {
                cc_emit_bytes(&em, "\x0F\x94", 2); /* sete/setz */
            }
            cc_emit_byte(&em, modrm);
        } else if (eq(t0, "shl") || eq(t0, "shll") || eq(t0, "sal") ||
                   eq(t0, "sall") || eq(t0, "shr") || eq(t0, "shrl") ||
                   eq(t0, "sar") || eq(t0, "sarl")) {
            long imm = 0;
            int is_shr = eq(t0, "shr") || eq(t0, "shrl");
            int is_sar = eq(t0, "sar") || eq(t0, "sarl");
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || t1[0] != '$' ||
                parse_imm(t1 + 1, &imm) != 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (imm < 0 || imm > 31) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                !(eq(t3, "%eax") || eq(t3, "%rax"))) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            /* M169: shl/shr/sar $imm, %eax */
            if (is_sar) {
                cc_emit_bytes(&em, "\xC1\xF8", 2);
            } else if (is_shr) {
                cc_emit_bytes(&em, "\xC1\xE8", 2);
            } else {
                cc_emit_bytes(&em, "\xC1\xE0", 2);
            }
            cc_emit_byte(&em, (unsigned char)imm);
        } else if (eq(t0, "jmp") || eq(t0, "je") || eq(t0, "jz") ||
                   eq(t0, "jne") || eq(t0, "jnz") || eq(t0, "jl") ||
                   eq(t0, "jg") || eq(t0, "jle") || eq(t0, "jge") ||
                   eq(t0, "js") || eq(t0, "jns") || eq(t0, "jbe") ||
                   eq(t0, "jna") || eq(t0, "ja") || eq(t0, "jnbe") ||
                   eq(t0, "jb") || eq(t0, "jnae") || eq(t0, "jc") ||
                   eq(t0, "jae") || eq(t0, "jnb") || eq(t0, "jnc")) {
            size_t k;
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 || !t1[0]) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t0, "jmp")) {
                cc_emit_byte(&em, 0xE9);
            } else if (eq(t0, "jne") || eq(t0, "jnz")) {
                cc_emit_bytes(&em, "\x0F\x85", 2);
            } else if (eq(t0, "jl")) {
                cc_emit_bytes(&em, "\x0F\x8C", 2);
            } else if (eq(t0, "jg")) {
                cc_emit_bytes(&em, "\x0F\x8F", 2);
            } else if (eq(t0, "jle")) {
                cc_emit_bytes(&em, "\x0F\x8E", 2);
            } else if (eq(t0, "jge")) {
                cc_emit_bytes(&em, "\x0F\x8D", 2);
            } else if (eq(t0, "js")) {
                cc_emit_bytes(&em, "\x0F\x88", 2);
            } else if (eq(t0, "jns")) {
                cc_emit_bytes(&em, "\x0F\x89", 2);
            } else if (eq(t0, "jbe") || eq(t0, "jna")) {
                cc_emit_bytes(&em, "\x0F\x86", 2); /* M199 */
            } else if (eq(t0, "ja") || eq(t0, "jnbe")) {
                cc_emit_bytes(&em, "\x0F\x87", 2); /* M199 */
            } else if (eq(t0, "jb") || eq(t0, "jnae") || eq(t0, "jc")) {
                cc_emit_bytes(&em, "\x0F\x82", 2); /* M209 */
            } else if (eq(t0, "jae") || eq(t0, "jnb") || eq(t0, "jnc")) {
                cc_emit_bytes(&em, "\x0F\x83", 2); /* M209 */
            } else {
                cc_emit_bytes(&em, "\x0F\x84", 2); /* je/jz */
            }
            if (nbr >= AS_BR_PEND) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            br_at[nbr] = em.code_len;
            cc_emit_i32(&em, 0);
            k = 0;
            while (k < CC_IDENT_MAX && t1[k]) {
                br_names[nbr][k] = t1[k];
                k++;
            }
            if (t1[k] != 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            br_names[nbr][k] = 0;
            nbr++;
        } else if (eq(t0, "test") || eq(t0, "testl") || eq(t0, "testq")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                (eq(t3, "%eax") || eq(t3, "%rax"))) {
                if (eq(t1, "%rax") || eq(t3, "%rax")) {
                    cc_emit_bytes(&em, "\x48\x85\xC0", 3);
                } else {
                    cc_emit_bytes(&em, "\x85\xC0", 2);
                }
            } else if ((eq(t1, "%rsi") || eq(t1, "%esi")) &&
                       (eq(t3, "%rsi") || eq(t3, "%esi"))) {
                /* M298: test %rsi, %rsi (bzero/strnlen) */
                if (eq(t1, "%rsi") || eq(t3, "%rsi")) {
                    cc_emit_bytes(&em, "\x48\x85\xF6", 3);
                } else {
                    cc_emit_bytes(&em, "\x85\xF6", 2);
                }
            } else if ((eq(t1, "%rcx") || eq(t1, "%ecx")) &&
                       (eq(t3, "%rcx") || eq(t3, "%ecx"))) {
                if (eq(t1, "%rcx") || eq(t3, "%rcx")) {
                    cc_emit_bytes(&em, "\x48\x85\xC9", 3);
                } else {
                    cc_emit_bytes(&em, "\x85\xC9", 2);
                }
            } else if ((eq(t1, "%rdx") || eq(t1, "%edx")) &&
                       (eq(t3, "%rdx") || eq(t3, "%edx"))) {
                /* M219: test %rdx, %rdx */
                if (eq(t1, "%rdx") || eq(t3, "%rdx")) {
                    cc_emit_bytes(&em, "\x48\x85\xD2", 3);
                } else {
                    cc_emit_bytes(&em, "\x85\xD2", 2);
                }
            } else if ((eq(t1, "%rdi") || eq(t1, "%edi")) &&
                       (eq(t3, "%rdi") || eq(t3, "%edi"))) {
                if (eq(t1, "%rdi") || eq(t3, "%rdi")) {
                    cc_emit_bytes(&em, "\x48\x85\xFF", 3);
                } else {
                    cc_emit_bytes(&em, "\x85\xFF", 2);
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "and") || eq(t0, "andl") || eq(t0, "or") ||
                   eq(t0, "orl")) {
            long imm = 0;
            int is_or = eq(t0, "or") || eq(t0, "orl");
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
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (imm >= -128 && imm <= 127) {
                    cc_emit_bytes(&em, is_or ? "\x83\xC8" : "\x83\xE0", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    cc_emit_byte(&em, is_or ? 0x0D : 0x25);
                    cc_emit_u32(&em, (unsigned)imm);
                }
            } else if (t1[0] == '%') {
                /* M179: and/or %reg, %eax */
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t1, "%ecx") || eq(t1, "%rcx")) {
                    cc_emit_bytes(&em, is_or ? "\x09\xC8" : "\x21\xC8", 2);
                } else if (eq(t1, "%edx") || eq(t1, "%rdx")) {
                    cc_emit_bytes(&em, is_or ? "\x09\xD0" : "\x21\xD0", 2);
                } else if (eq(t1, "%esi") || eq(t1, "%rsi")) {
                    cc_emit_bytes(&em, is_or ? "\x09\xF0" : "\x21\xF0", 2);
                } else if (eq(t1, "%edi") || eq(t1, "%rdi")) {
                    cc_emit_bytes(&em, is_or ? "\x09\xF8" : "\x21\xF8", 2);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else {
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
                } else if (eq(t3, "%edx") || eq(t3, "%rdx")) {
                    /* M146: 3rd SysV arg for strncmp etc. */
                    cc_emit_byte(&em, 0xBA);
                    cc_emit_u32(&em, (unsigned)imm);
                } else if (eq(t3, "%ecx") || eq(t3, "%rcx")) {
                    /* M157: idiv divisor setup */
                    cc_emit_byte(&em, 0xB9);
                    cc_emit_u32(&em, (unsigned)imm);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            } else if (t1[0] == '%') {
                /* M253: mov %fs:0x28, %rax — stack-guard canary load. */
                if (eq(t1, "%fs")) {
                    long fsoff = 0;
                    if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ':' ||
                        read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        parse_imm(t2, &fsoff) != 0 ||
                        read_tok(&p, end, t3, sizeof(t3)) <= 0 || t3[0] != ',' ||
                        read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (!(eq(t3, "%rax") || eq(t3, "%eax")) || fsoff != 0x28) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    /* 64 48 8b 04 25 28 00 00 00 */
                    cc_emit_bytes(&em, "\x64\x48\x8B\x04\x25\x28\x00\x00\x00", 9);
                } else if (read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                           t2[0] != ',' ||
                           read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                } else if (t3[0] == '%') {
                    if ((eq(t1, "%edi") || eq(t1, "%rdi")) &&
                        (eq(t3, "%eax") || eq(t3, "%rax"))) {
                        /* M295: mov %rdi, %rax for labs/llabs plant_src */
                        if (eq(t1, "%rdi") || eq(t3, "%rax")) {
                            cc_emit_bytes(&em, "\x48\x89\xF8", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xF8", 2);
                        }
                    } else if (eq(t1, "%esi") && eq(t3, "%eax")) {
                        cc_emit_bytes(&em, "\x89\xF0", 2);
                    } else if (eq(t1, "%edi") && eq(t3, "%esi")) {
                        cc_emit_bytes(&em, "\x89\xFE", 2);
                    } else if (eq(t1, "%eax") && eq(t3, "%edi")) {
                        cc_emit_bytes(&em, "\x89\xC7", 2);
                    } else if ((eq(t1, "%rax") || eq(t1, "%eax")) &&
                               (eq(t3, "%rdi") || eq(t3, "%edi"))) {
                        cc_emit_bytes(&em, "\x48\x89\xC7", 3);
                    } else if ((eq(t1, "%rax") || eq(t1, "%eax")) &&
                               (eq(t3, "%rsi") || eq(t3, "%esi"))) {
                        cc_emit_bytes(&em, "\x48\x89\xC6", 3);
                    } else if ((eq(t1, "%rsp") || eq(t1, "%esp")) &&
                               (eq(t3, "%rbp") || eq(t3, "%ebp"))) {
                        /* M157: movq %rsp, %rbp — gcc -S frame setup */
                        cc_emit_bytes(&em, "\x48\x89\xE5", 3);
                    } else if ((eq(t1, "%rbp") || eq(t1, "%ebp")) &&
                               (eq(t3, "%rsp") || eq(t3, "%esp"))) {
                        /* leave-ish: mov %rbp, %rsp */
                        cc_emit_bytes(&em, "\x48\x89\xEC", 3);
                    } else if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                               (eq(t3, "%edx") || eq(t3, "%rdx"))) {
                        /* M297: prefer REX.W when any 64-bit name used */
                        if (eq(t1, "%rax") || eq(t3, "%rdx")) {
                            cc_emit_bytes(&em, "\x48\x89\xC2", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xC2", 2);
                        }
                    } else if ((eq(t1, "%edx") || eq(t1, "%rdx")) &&
                               (eq(t3, "%eax") || eq(t3, "%rax"))) {
                        if (eq(t1, "%rdx") || eq(t3, "%rax")) {
                            cc_emit_bytes(&em, "\x48\x89\xD0", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xD0", 2);
                        }
                    } else if ((eq(t1, "%ecx") || eq(t1, "%rcx")) &&
                               (eq(t3, "%eax") || eq(t3, "%rax"))) {
                        /* M298: mov %ecx, %eax (strlen result) */
                        if (eq(t1, "%rcx") || eq(t3, "%rax")) {
                            cc_emit_bytes(&em, "\x48\x89\xC8", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xC8", 2);
                        }
                    } else if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                               (eq(t3, "%ecx") || eq(t3, "%rcx"))) {
                        if (eq(t1, "%rax") || eq(t3, "%rcx")) {
                            cc_emit_bytes(&em, "\x48\x89\xC1", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xC1", 2);
                        }
                    } else if ((eq(t1, "%esi") || eq(t1, "%rsi")) &&
                               (eq(t3, "%eax") || eq(t3, "%rax"))) {
                        /* M297: mov %rsi, %rax (SysV prologue) */
                        if (eq(t1, "%rsi") || eq(t3, "%rax")) {
                            cc_emit_bytes(&em, "\x48\x89\xF0", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xF0", 2);
                        }
                    } else if ((eq(t1, "%esi") || eq(t1, "%rsi")) &&
                               (eq(t3, "%ecx") || eq(t3, "%rcx"))) {
                        /* M298: mov %rsi, %rcx (bzero count) */
                        if (eq(t1, "%rsi") || eq(t3, "%rcx")) {
                            cc_emit_bytes(&em, "\x48\x89\xF1", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xF1", 2);
                        }
                    } else if ((eq(t1, "%edx") || eq(t1, "%rdx")) &&
                               (eq(t3, "%ecx") || eq(t3, "%rcx"))) {
                        /* M300: mov %rdx, %rcx (memset count) */
                        if (eq(t1, "%rdx") || eq(t3, "%rcx")) {
                            cc_emit_bytes(&em, "\x48\x89\xD1", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xD1", 2);
                        }
                    } else if ((eq(t1, "%esi") || eq(t1, "%rsi")) &&
                               (eq(t3, "%edx") || eq(t3, "%rdx"))) {
                        /* M300: mov %rsi, %rdx (strcpy save src) */
                        if (eq(t1, "%rsi") || eq(t3, "%rdx")) {
                            cc_emit_bytes(&em, "\x48\x89\xF2", 3);
                        } else {
                            cc_emit_bytes(&em, "\x89\xF2", 2);
                        }
                    } else {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                } else {
                    /* store: %eax|%rax|%al, disp(%rbp|%rdi|%rax) or (%reg) */
                    long disp = 0;
                    char base[AS_TOK_MAX];
                    int use64;
                    int use8 = eq(t1, "%al") || eq(t1, "%AL");
                    if (!(eq(t1, "%eax") || eq(t1, "%rax") || use8)) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    use64 = eq(t1, "%rax");
                    if (t3[0] == '(') {
                        disp = 0;
                        if (read_tok(&p, end, base, sizeof(base)) <= 0 ||
                            read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                            t2[0] != ')') {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                    } else {
                        if (parse_imm(t3, &disp) != 0) {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                        if (read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                            t2[0] != '(' ||
                            read_tok(&p, end, base, sizeof(base)) <= 0 ||
                            read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                            t2[0] != ')') {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                    }
                    if (use8) {
                        /* M300: mov %al, (%rdi|%rsi) for strcpy/stpcpy */
                        if (disp != 0) {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                        if (eq(base, "%rdi") || eq(base, "%edi")) {
                            cc_emit_bytes(&em, "\x88\x07", 2);
                        } else if (eq(base, "%rsi") || eq(base, "%esi")) {
                            cc_emit_bytes(&em, "\x88\x06", 2);
                        } else {
                            g_as_fail = AS_FAIL;
                            return -1;
                        }
                    } else if (eq(base, "%rbp") || eq(base, "%ebp")) {
                        /* M297: match cc_emit_store_local (disp32) when needed */
                        if (use64 || disp < -128 || disp > 127) {
                            cc_emit_bytes(&em, "\x48\x89\x85", 3);
                            cc_emit_i32(&em, (int)disp);
                        } else {
                            cc_emit_bytes(&em, "\x89\x45", 2);
                            cc_emit_byte(&em, (unsigned char)disp);
                        }
                    } else if (eq(base, "%rdi") || eq(base, "%edi")) {
                        if (disp == 0) {
                            cc_emit_bytes(&em, "\x89\x07", 2);
                        } else {
                            cc_emit_bytes(&em, "\x89\x47", 2);
                            cc_emit_byte(&em, (unsigned char)disp);
                        }
                    } else if (eq(base, "%rax") || eq(base, "%eax")) {
                        if (disp == 0) {
                            cc_emit_bytes(&em, "\x89\x00", 2);
                        } else {
                            cc_emit_bytes(&em, "\x89\x40", 2);
                            cc_emit_byte(&em, (unsigned char)disp);
                        }
                    } else {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                }
            } else if (is_ident0(t1[0])) {
                /* M148: mov name(%rip), %rax — mirror lea rip-rel. */
                int di;
                size_t d_off;
                size_t k;
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != '(' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%rip") || eq(t3, "rip")) ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ')' ||
                    read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%rax") || eq(t3, "%eax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                di = data_find(dnames, doffs, ndata, t1);
                d_off = di >= 0 ? doffs[di] : 0;
                cc_emit_mov_data(&em, d_off);
                if (di < 0) {
                    if (nlea_pend >= AS_LEA_PEND) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    k = 0;
                    while (k < CC_IDENT_MAX && t1[k]) {
                        lea_names[nlea_pend][k] = t1[k];
                        k++;
                    }
                    if (t1[k] != 0) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    lea_names[nlea_pend][k] = 0;
                    lea_ri[nlea_pend] = cc_reloc_count() - 1;
                    nlea_pend++;
                }
            } else {
                /* load: disp(%reg), %eax  or  (%reg), %eax */
                long disp = 0;
                char base[AS_TOK_MAX];
                if (t1[0] == '(') {
                    disp = 0;
                    if (read_tok(&p, end, base, sizeof(base)) <= 0 ||
                        read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != ')') {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                } else {
                    if (parse_imm(t1, &disp) != 0) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != '(' ||
                        read_tok(&p, end, base, sizeof(base)) <= 0 ||
                        read_tok(&p, end, t2, sizeof(t2)) <= 0 ||
                        t2[0] != ')') {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                }
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0 ||
                    !(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(base, "%rbp") || eq(base, "%ebp")) {
                    /* M297: match cc_emit_load_local */
                    if (eq(t3, "%rax") || disp < -128 || disp > 127) {
                        cc_emit_bytes(&em, "\x48\x8B\x85", 3);
                        cc_emit_i32(&em, (int)disp);
                    } else {
                        cc_emit_bytes(&em, "\x8B\x45", 2);
                        cc_emit_byte(&em, (unsigned char)disp);
                    }
                } else if (eq(base, "%rdi") || eq(base, "%edi")) {
                    if (disp == 0) {
                        cc_emit_bytes(&em, "\x8B\x07", 2);
                    } else {
                        cc_emit_bytes(&em, "\x8B\x47", 2);
                        cc_emit_byte(&em, (unsigned char)disp);
                    }
                } else if (eq(base, "%rax") || eq(base, "%eax")) {
                    if (disp == 0) {
                        cc_emit_bytes(&em, "\x8B\x00", 2);
                    } else {
                        cc_emit_bytes(&em, "\x8B\x40", 2);
                        cc_emit_byte(&em, (unsigned char)disp);
                    }
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
            }
        } else if (eq(t0, "add") || eq(t0, "addl") || eq(t0, "addq")) {
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
                    if (imm >= -128 && imm <= 127) {
                        cc_emit_bytes(&em, "\x83\xC0", 2);
                        cc_emit_byte(&em, (unsigned char)imm);
                    } else {
                        cc_emit_byte(&em, 0x05);
                        cc_emit_u32(&em, (unsigned)imm);
                    }
                } else if (eq(t3, "%rdi") || eq(t3, "%edi")) {
                    /* M219: add $imm, %rdi — pointer step */
                    if (imm < -128 || imm > 127) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (eq(t3, "%rdi") || eq(t0, "addq")) {
                        cc_emit_bytes(&em, "\x48\x83\xC7", 3);
                    } else {
                        cc_emit_bytes(&em, "\x83\xC7", 2);
                    }
                    cc_emit_byte(&em, (unsigned char)imm);
                } else if (eq(t3, "%rsi") || eq(t3, "%esi")) {
                    if (imm < -128 || imm > 127) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (eq(t3, "%rsi") || eq(t0, "addq")) {
                        cc_emit_bytes(&em, "\x48\x83\xC6", 3);
                    } else {
                        cc_emit_bytes(&em, "\x83\xC6", 2);
                    }
                    cc_emit_byte(&em, (unsigned char)imm);
                } else if (eq(t3, "%rdx") || eq(t3, "%edx")) {
                    if (imm < -128 || imm > 127) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (eq(t3, "%rdx") || eq(t0, "addq")) {
                        cc_emit_bytes(&em, "\x48\x83\xC2", 3);
                    } else {
                        cc_emit_bytes(&em, "\x83\xC2", 2);
                    }
                    cc_emit_byte(&em, (unsigned char)imm);
                } else if (eq(t3, "%ecx")) {
                    /* M298: add $imm, %ecx only (edge still rejects %rcx) */
                    if (imm < -128 || imm > 127) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    cc_emit_bytes(&em, "\x83\xC1", 2);
                    cc_emit_byte(&em, (unsigned char)imm);
                } else if (eq(t3, "%rsp") || eq(t3, "%esp")) {
                    /* M299: switch scrutinee cleanup — add $8, %rsp */
                    if (imm < -128 || imm > 127) {
                        g_as_fail = AS_FAIL;
                        return -1;
                    }
                    if (eq(t3, "%rsp") || eq(t0, "addq")) {
                        cc_emit_bytes(&em, "\x48\x83\xC4", 3);
                    } else {
                        cc_emit_bytes(&em, "\x83\xC4", 2);
                    }
                    cc_emit_byte(&em, (unsigned char)imm);
                } else {
                    g_as_fail = AS_FAIL;
                    return -1;
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
            } else if (eq(t1, "%edx") || eq(t1, "%rdx")) {
                /* M297: add %rdx, %rax */
                if (read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                    read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (!(eq(t3, "%eax") || eq(t3, "%rax"))) {
                    g_as_fail = AS_FAIL;
                    return -1;
                }
                if (eq(t1, "%rdx") || eq(t3, "%rax") || eq(t0, "addq")) {
                    cc_emit_bytes(&em, "\x48\x01\xD0", 3);
                } else {
                    cc_emit_bytes(&em, "\x01\xD0", 2);
                }
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "xor") || eq(t0, "xorl") || eq(t0, "xorq")) {
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
            /* M157: wider xor zeroing for gcc -S / idiv prep */
            if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                (eq(t2, "%eax") || eq(t2, "%rax"))) {
                cc_emit_bytes(&em, "\x31\xC0", 2);
            } else if ((eq(t1, "%edx") || eq(t1, "%rdx")) &&
                       (eq(t2, "%edx") || eq(t2, "%rdx"))) {
                cc_emit_bytes(&em, "\x31\xD2", 2);
            } else if ((eq(t1, "%ecx") || eq(t1, "%rcx")) &&
                       (eq(t2, "%ecx") || eq(t2, "%rcx"))) {
                cc_emit_bytes(&em, "\x31\xC9", 2);
            } else if ((eq(t1, "%esi") || eq(t1, "%rsi")) &&
                       (eq(t2, "%esi") || eq(t2, "%rsi"))) {
                cc_emit_bytes(&em, "\x31\xF6", 2);
            } else if ((eq(t1, "%edi") || eq(t1, "%rdi")) &&
                       (eq(t2, "%edi") || eq(t2, "%rdi"))) {
                cc_emit_bytes(&em, "\x31\xFF", 2);
            } else if ((eq(t1, "%r8") || eq(t1, "%r8d")) &&
                       (eq(t2, "%r8") || eq(t2, "%r8d"))) {
                /* M253: xor %r8d, %r8d */
                cc_emit_bytes(&em, "\x45\x31\xC0", 3);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "cld")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0xFC);
        } else if (eq(t0, "std")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0xFD);
        } else if (eq(t0, "rep")) {
            /* M253: rep movsb / rep stosb (memmove -S surface). */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (eq(t1, "movsb")) {
                cc_emit_bytes(&em, "\xF3\xA4", 2);
            } else if (eq(t1, "stosb")) {
                cc_emit_bytes(&em, "\xF3\xAA", 2);
            } else if (eq(t1, "stosq")) {
                cc_emit_bytes(&em, "\xF3\x48\xAB", 3);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else if (eq(t0, "movsb")) {
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            cc_emit_byte(&em, 0xA4);
        } else if (eq(t0, "movslq") || eq(t0, "movsxd")) {
            /* M253: movslq %eax, %rdx (intscan surface). */
            if (!in_text || cur_fn < 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if (read_tok(&p, end, t1, sizeof(t1)) <= 0 ||
                read_tok(&p, end, t2, sizeof(t2)) <= 0 || t2[0] != ',' ||
                read_tok(&p, end, t3, sizeof(t3)) <= 0) {
                g_as_fail = AS_FAIL;
                return -1;
            }
            if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                (eq(t3, "%rdx") || eq(t3, "%edx"))) {
                cc_emit_bytes(&em, "\x48\x63\xD0", 3);
            } else if ((eq(t1, "%eax") || eq(t1, "%rax")) &&
                       (eq(t3, "%rsi") || eq(t3, "%esi"))) {
                cc_emit_bytes(&em, "\x48\x63\xF0", 3);
            } else {
                g_as_fail = AS_FAIL;
                return -1;
            }
        } else {
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
    for (i = 0; i < nlea_pend; i++) {
        int di = data_find(dnames, doffs, ndata, lea_names[i]);
        if (di < 0 || cc_reloc_set_data(lea_ri[i], doffs[di]) != 0) {
            g_as_fail = AS_FAIL;
            return -1;
        }
    }
    for (i = 0; i < nbr; i++) {
        int li = data_find(lnames, loffs, nloc, br_names[i]);
        int rel;
        if (li < 0) {
            g_as_fail = AS_FAIL;
            return -1;
        }
        rel = (int)(loffs[li] - (br_at[i] + 4));
        em.code[br_at[i]] = (unsigned char)(rel);
        em.code[br_at[i] + 1] = (unsigned char)(rel >> 8);
        em.code[br_at[i] + 2] = (unsigned char)(rel >> 16);
        em.code[br_at[i] + 3] = (unsigned char)(rel >> 24);
    }
    if (cc_data_seal(&em) != 0 || em.failed) {
        g_as_fail = AS_FAIL;
        return -1;
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
