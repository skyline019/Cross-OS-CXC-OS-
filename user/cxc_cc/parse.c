#include "cxc_cc/cc.h"

typedef struct CcParser {
    CcLexer* lx;
    CcEmit* em;
} CcParser;

static void snap_fail_loc(CcParser* p) {
    /* M296: first-fail line/col only (matches fail_code latch). */
    if (p->em->fail_line == 0 && p->lx) {
        p->em->fail_line = p->lx->line;
        p->em->fail_col = p->lx->col;
    }
}

static void fail(CcParser* p) {
    p->em->failed = 1;
    if (p->em->fail_code == 0) {
        p->em->fail_code = 30; /* parse fail (debug) */
        snap_fail_loc(p);
    }
}

static void fail_unknown(CcParser* p) {
    /* M287/D1: unsupported lexeme (e.g. '#' from #define) */
    p->em->failed = 1;
    if (p->em->fail_code == 0) {
        p->em->fail_code = 60;
        snap_fail_loc(p);
    }
}

static int accept(CcParser* p, int tok) {
    if (p->lx->tok == TOK_UNKNOWN) {
        fail_unknown(p);
        return 0;
    }
    if (p->lx->tok == tok) {
        cc_lex_next(p->lx);
        if (p->lx->tok == TOK_UNKNOWN) {
            fail_unknown(p);
        }
        return 1;
    }
    return 0;
}

static void expect(CcParser* p, int tok) {
    if (!accept(p, tok)) {
        if (!p->em->failed) {
            fail(p);
        }
    }
}

/* M132: optional const/unsigned + int|char|typedef. is_char/is_const out. */
static int accept_type_ex(CcParser* p, int* is_char, int* is_const) {
    if (is_char) {
        *is_char = 0;
    }
    if (is_const) {
        *is_const = 0;
    }
    if (accept(p, TOK_KW_CONST)) {
        if (is_const) {
            *is_const = 1;
        }
    }
    {
        int saw_unsigned = accept(p, TOK_KW_UNSIGNED);
        if (accept(p, TOK_KW_CHAR)) {
            if (is_char) {
                *is_char = 1;
            }
            return 1;
        }
        if (accept(p, TOK_KW_INT) || accept(p, TOK_KW_LONG)) {
            return 1;
        }
        /* M159: bare `unsigned` ≡ unsigned int (word slot). */
        if (saw_unsigned) {
            return 1;
        }
    }
    if (p->lx->tok == TOK_IDENT &&
        cc_typedef_find(p->em, p->lx->text) >= 0) {
        cc_lex_next(p->lx);
        return 1;
    }
    return 0;
}

static int is_type_tok(CcParser* p) {
    if (p->lx->tok == TOK_KW_INT || p->lx->tok == TOK_KW_LONG ||
        p->lx->tok == TOK_KW_CHAR || p->lx->tok == TOK_KW_CONST ||
        p->lx->tok == TOK_KW_UNSIGNED || p->lx->tok == TOK_KW_VOID) {
        return 1;
    }
    return p->lx->tok == TOK_IDENT &&
           cc_typedef_find(p->em, p->lx->text) >= 0;
}

static void emit_add_rax_imm(CcEmit* em, int bytes) {
    if (bytes == 0) {
        return;
    }
    cc_emit_bytes(em, "\x48\x05", 2); /* add rax, imm32 */
    cc_emit_u32(em, (unsigned)bytes);
}

static void emit_trunc_char(CcEmit* em) {
    /* and eax, 0xff — explicit narrow truncate (word slot). */
    cc_emit_bytes(em, "\x25\xFF\x00\x00\x00", 5);
}

static int store_local_checked(CcParser* p, int idx) {
    if (idx < 0 || idx >= p->em->nlocals) {
        fail(p);
        return -1;
    }
    if (p->em->locals[idx].is_const) {
        fail(p);
        return -1;
    }
    if (p->em->locals[idx].is_char) {
        emit_trunc_char(p->em);
    }
    cc_emit_store_local(p->em, p->em->locals[idx].slot);
    return 0;
}

static int store_global_checked(CcParser* p, int gi) {
    if (gi < 0 || gi >= p->em->nglobals) {
        fail(p);
        return -1;
    }
    if (p->em->globals[gi].is_const) {
        fail(p);
        return -1;
    }
    cc_emit_store_global(p->em, p->em->globals[gi].data_off);
    return 0;
}

static void parse_expr(CcParser* p);
static void parse_unary(CcParser* p);

static size_t emit_je32(CcEmit* em) {
    size_t at;
    int id = 0;
    if (em->asm_mode) {
        id = cc_asm_new_label(em);
        cc_asm_je_label(em, id);
    }
    cc_emit_bytes(em, "\x0F\x84", 2);
    at = em->code_len;
    cc_emit_i32(em, 0);
    if (em->asm_mode) {
        if (em->nasm_fix >= 64) {
            em->failed = 1;
        } else {
            em->asm_fix_at[em->nasm_fix] = at;
            em->asm_fix_id[em->nasm_fix] = id;
            em->nasm_fix++;
        }
    }
    return at;
}

static size_t emit_jne32(CcEmit* em) {
    size_t at;
    int id = 0;
    if (em->asm_mode) {
        id = cc_asm_new_label(em);
        cc_asm_jne_label(em, id);
    }
    cc_emit_bytes(em, "\x0F\x85", 2);
    at = em->code_len;
    cc_emit_i32(em, 0);
    if (em->asm_mode) {
        if (em->nasm_fix >= 64) {
            em->failed = 1;
        } else {
            em->asm_fix_at[em->nasm_fix] = at;
            em->asm_fix_id[em->nasm_fix] = id;
            em->nasm_fix++;
        }
    }
    return at;
}

static size_t emit_jmp32(CcEmit* em) {
    size_t at;
    int id = 0;
    if (em->asm_mode) {
        id = cc_asm_new_label(em);
        cc_asm_jmp_label(em, id);
    }
    cc_emit_byte(em, 0xE9);
    at = em->code_len;
    cc_emit_i32(em, 0);
    if (em->asm_mode) {
        if (em->nasm_fix >= 64) {
            em->failed = 1;
        } else {
            em->asm_fix_at[em->nasm_fix] = at;
            em->asm_fix_id[em->nasm_fix] = id;
            em->nasm_fix++;
        }
    }
    return at;
}

static void patch_rel32(CcEmit* em, size_t at, size_t target) {
    int rel = (int)(target - (at + 4));
    int i;
    em->code[at] = (unsigned char)(rel);
    em->code[at + 1] = (unsigned char)(rel >> 8);
    em->code[at + 2] = (unsigned char)(rel >> 16);
    em->code[at + 3] = (unsigned char)(rel >> 24);
    /* M297: forward asm labels — define .LccN at current code end when
     * target==code_len. Backward edges use cc_asm_jmp_label(known id). */
    if (!em->asm_mode) {
        return;
    }
    for (i = 0; i < em->nasm_fix; i++) {
        if (em->asm_fix_at[i] == at) {
            int id = em->asm_fix_id[i];
            em->asm_fix_at[i] = em->asm_fix_at[em->nasm_fix - 1];
            em->asm_fix_id[i] = em->asm_fix_id[em->nasm_fix - 1];
            em->nasm_fix--;
            if (target == em->code_len) {
                cc_asm_def_label(em, id);
            }
            /* backward: label text already used correct id via jmp_label */
            return;
        }
    }
}

static void parse_call(CcParser* p, const char* name) {
    int fi = cc_fn_find(p->em, name);
    int argc = 0;
    size_t call_at;
    expect(p, TOK_LPAREN);
    if (fi < 0) {
        fail(p);
        return;
    }
    if (p->lx->tok != TOK_RPAREN) {
        for (;;) {
            parse_expr(p);
            cc_emit_push_rax(p->em);
            argc++;
            if (argc > CC_FN_ARGS) {
                fail(p);
                return;
            }
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
    }
    expect(p, TOK_RPAREN);
    if (argc != p->em->fns[fi].arity) {
        fail(p);
        return;
    }
    if (p->em->fns[fi].sysv) {
        /* Args were pushed L→R; pop into SysV regs (last arg on top). */
        if (argc >= 4) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tpop %rcx\n");
            }
            cc_emit_byte(p->em, 0x59); /* pop rcx */
        }
        if (argc >= 3) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tpop %rdx\n");
            }
            cc_emit_byte(p->em, 0x5A); /* pop rdx */
        }
        if (argc >= 2) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tpop %rsi\n");
            }
            cc_emit_byte(p->em, 0x5E); /* pop rsi */
        }
        if (argc >= 1) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tpop %rdi\n");
            }
            cc_emit_byte(p->em, 0x5F); /* pop rdi */
        }
        if (argc > 4) {
            fail(p);
            return;
        }
    }
    if (p->em->asm_mode) {
        cc_emit_call_name(p->em, p->em->fns[fi].name);
    }
    call_at = cc_emit_call_rel32(p->em);
    /* obj_mode: leave e8 00..00 + record for .rela.text (gcc-like). */
    if (p->em->obj_mode || !p->em->fns[fi].defined) {
        if (p->em->ncalls >= CC_CALL_MAX) {
            fail(p);
            return;
        }
        p->em->calls[p->em->ncalls].at = call_at;
        p->em->calls[p->em->ncalls].fn = fi;
        p->em->ncalls++;
    } else {
        patch_rel32(p->em, call_at, p->em->fns[fi].entry);
    }
    if (argc > 0 && !p->em->fns[fi].sysv) {
        /* add rsp, 8*argc */
        cc_emit_bytes(p->em, "\x48\x81\xC4", 3);
        cc_emit_u32(p->em, (unsigned)(argc * 8));
    }
}

static void parse_primary(CcParser* p) {
    if (p->lx->tok == TOK_NUMBER) {
        unsigned long long u = (unsigned long long)p->lx->number;
        if (u > 0xffffffffull || (p->lx->number < 0)) {
            cc_emit_mov_rax_imm64(p->em, u);
        } else {
            cc_emit_mov_eax_imm(p->em, (unsigned)u);
        }
        cc_lex_next(p->lx);
        return;
    }
    if (p->lx->tok == TOK_STR) {
        size_t data_at;
        char buf[CC_STR_MAX + 1];
        size_t n = p->lx->str_len;
        if (n > CC_STR_MAX) {
            n = CC_STR_MAX;
        }
        memcpy(buf, p->lx->str, n);
        buf[n] = 0;
        data_at = cc_data_push(p->em, buf, n + 1);
        cc_lex_next(p->lx);
        cc_emit_lea_data(p->em, data_at);
        return;
    }
    if (p->lx->tok == TOK_IDENT) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        int gi;
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        if (p->lx->tok == TOK_LPAREN) {
            parse_call(p, name);
            return;
        }
        idx = cc_local_find(p->em, name);
        if (idx < 0) {
            int ei;
            gi = cc_global_find(p->em, name);
            if (gi >= 0) {
                cc_emit_load_global(p->em, p->em->globals[gi].data_off);
                return;
            }
            ei = cc_enum_find(p->em, name);
            if (ei < 0) {
                fail(p);
                return;
            }
            cc_emit_mov_eax_imm(p->em, (unsigned)p->em->enums[ei].value);
            return;
        }
        if (accept(p, TOK_LBRACK)) {
            if (p->em->locals[idx].stype >= 0) {
                fail(p);
                return;
            }
            if (p->em->locals[idx].count > 1) {
                int ew = p->em->locals[idx].elem_w > 0
                             ? p->em->locals[idx].elem_w
                             : 8;
                parse_expr(p);
                expect(p, TOK_RBRACK);
                cc_emit_index_load_n(p->em, p->em->locals[idx].slot,
                                    p->em->locals[idx].count, ew);
                return;
            }
            if (p->em->locals[idx].count == 1) {
                /* M288: *(p + i) with pointee elem_w (default 8). */
                int ew = p->em->locals[idx].elem_w > 0
                             ? p->em->locals[idx].elem_w
                             : 8;
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                cc_emit_push_rax(p->em);
                parse_expr(p);
                expect(p, TOK_RBRACK);
                cc_emit_bytes(p->em, "\x48\x89\xC1", 3); /* mov rcx, index */
                cc_emit_byte(p->em, 0x58);               /* pop rax = ptr */
                cc_emit_ptr_index_load(p->em, ew);
                return;
            }
            fail(p);
            return;
        }
        if (accept(p, TOK_DOT)) {
            int off;
            if (p->lx->tok != TOK_IDENT || p->em->locals[idx].stype < 0 ||
                p->em->locals[idx].is_ptr) {
                fail(p);
                return;
            }
            {
                int width;
                char fname[CC_IDENT_MAX + 1];
                off = cc_struct_field(p->em, p->em->locals[idx].stype,
                                     p->lx->text);
                if (off < 0) {
                    fail(p);
                    return;
                }
                memcpy(fname, p->lx->text, CC_IDENT_MAX + 1);
                width = cc_struct_field_size(p->em, p->em->locals[idx].stype,
                                             fname);
                cc_lex_next(p->lx);
                /* M248: compact byte offset from struct base slot. */
                cc_emit_load_local_bytes(p->em, p->em->locals[idx].slot, off,
                                         width > 0 ? width : 8);
                return;
            }
        }
        if (accept(p, TOK_ARROW)) {
            int off;
            int width;
            char fname[CC_IDENT_MAX + 1];
            if (p->lx->tok != TOK_IDENT || p->em->locals[idx].stype < 0 ||
                !p->em->locals[idx].is_ptr) {
                fail(p);
                return;
            }
            off = cc_struct_field(p->em, p->em->locals[idx].stype, p->lx->text);
            if (off < 0) {
                fail(p);
                return;
            }
            memcpy(fname, p->lx->text, CC_IDENT_MAX + 1);
            width = cc_struct_field_size(p->em, p->em->locals[idx].stype, fname);
            cc_lex_next(p->lx);
            cc_emit_load_local(p->em, p->em->locals[idx].slot);
            /* Stack locals: higher field → lower address; off is bytes. */
            emit_add_rax_imm(p->em, -off);
            cc_emit_load_indirect_width(p->em, width > 0 ? width : 8);
            return;
        }
        if (p->em->locals[idx].stype >= 0 && !p->em->locals[idx].is_ptr) {
            /* M290: struct rvalue → first word (covers return v; field0). */
            cc_emit_load_local(p->em, p->em->locals[idx].slot);
            return;
        }
        if (p->em->locals[idx].count != 1) {
            fail(p);
            return;
        }
        cc_emit_load_local(p->em, p->em->locals[idx].slot);
        return;
    }
    if (accept(p, TOK_LPAREN)) {
        /* M289: (T)x cast — T is int/long/char/unsigned[/void*]. */
        if (is_type_tok(p) || p->lx->tok == TOK_KW_VOID) {
            int is_char = 0;
            if (accept(p, TOK_KW_VOID)) {
                (void)accept(p, TOK_STAR);
            } else if (!accept_type_ex(p, &is_char, 0)) {
                fail(p);
                return;
            } else {
                while (accept(p, TOK_STAR)) {
                }
            }
            expect(p, TOK_RPAREN);
            parse_unary(p);
            if (is_char) {
                emit_trunc_char(p->em);
            }
            return;
        }
        parse_expr(p);
        expect(p, TOK_RPAREN);
        return;
    }
    fail(p);
}

static void parse_addr_of(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    int idx;
    int gi;
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    idx = cc_local_find(p->em, name);
    if (idx < 0) {
        gi = cc_global_find(p->em, name);
        if (gi < 0) {
            fail(p);
            return;
        }
        cc_emit_lea_data(p->em, p->em->globals[gi].data_off);
        return;
    }
    if (accept(p, TOK_DOT)) {
        int off;
        if (p->lx->tok != TOK_IDENT || p->em->locals[idx].stype < 0) {
            fail(p);
            return;
        }
        off = cc_struct_field(p->em, p->em->locals[idx].stype, p->lx->text);
        if (off < 0) {
            fail(p);
            return;
        }
        cc_lex_next(p->lx);
        cc_emit_lea_local_bytes(p->em, p->em->locals[idx].slot, off);
        return;
    }
    if (accept(p, TOK_LBRACK)) {
        int ew;
        if (p->em->locals[idx].count < 1) {
            fail(p);
            return;
        }
        ew = p->em->locals[idx].elem_w > 0 ? p->em->locals[idx].elem_w : 8;
        parse_expr(p);
        expect(p, TOK_RBRACK);
        if (p->em->locals[idx].count == 1) {
            /* &p[i] → p + i*elem_w (index already in rax) */
            cc_emit_push_rax(p->em);
            cc_emit_load_local(p->em, p->em->locals[idx].slot);
            cc_emit_bytes(p->em, "\x48\x89\xC1", 3); /* mov rcx, ptr */
            cc_emit_byte(p->em, 0x58);               /* pop rax = index */
            cc_emit_ptr_add_scaled(p->em, ew);
            return;
        }
        cc_emit_index_lea_n(p->em, p->em->locals[idx].slot,
                            p->em->locals[idx].count, ew);
        return;
    }
    /* M140: &struct_value → address of first field / object. */
    if (p->em->locals[idx].stype >= 0 && !p->em->locals[idx].is_ptr) {
        cc_emit_lea_local(p->em, p->em->locals[idx].slot);
        return;
    }
    if (p->em->locals[idx].count != 1) {
        fail(p);
        return;
    }
    cc_emit_lea_local(p->em, p->em->locals[idx].slot);
}

static void parse_unary(CcParser* p) {
    if (accept(p, TOK_MINUS)) {
        parse_unary(p);
        cc_emit_bytes(p->em, "\x48\xF7\xD8", 3); /* neg rax */
        return;
    }
    if (accept(p, TOK_TILDE)) {
        /* M289: ~x → not rax */
        parse_unary(p);
        cc_emit_bytes(p->em, "\x48\xF7\xD0", 3); /* not rax */
        return;
    }
    if (accept(p, TOK_BANG)) {
        parse_unary(p);
        cc_emit_bytes(p->em, "\x85\xC0", 2);     /* test eax, eax */
        cc_emit_bytes(p->em, "\x0F\x94\xC0", 3); /* sete al */
        cc_emit_bytes(p->em, "\x0F\xB6\xC0", 3); /* movzx eax, al */
        return;
    }
    if (accept(p, TOK_STAR)) {
        parse_unary(p);
        cc_emit_load_indirect(p->em);
        return;
    }
    if (accept(p, TOK_AMP)) {
        parse_addr_of(p);
        return;
    }
    if (accept(p, TOK_KW_SIZEOF)) {
        /* M150/M159: sizeof(char)==1, sizeof(T*|void*)==8, int/long==8. */
        unsigned sz = 8;
        int is_char = 0;
        expect(p, TOK_LPAREN);
        if (accept(p, TOK_KW_VOID)) {
            if (!accept(p, TOK_STAR)) {
                fail(p); /* sizeof(void) illegal */
                return;
            }
            while (accept(p, TOK_STAR)) {
            }
            sz = 8;
        } else if (accept_type_ex(p, &is_char, 0)) {
            if (accept(p, TOK_STAR)) {
                sz = 8;
                while (accept(p, TOK_STAR)) {
                    /* multi-level pointer still word-sized */
                }
            } else if (is_char) {
                sz = 1;
            } else {
                sz = 8;
            }
        } else if (p->lx->tok == TOK_IDENT) {
            int st = cc_struct_find(p->em, p->lx->text);
            int li;
            if (st >= 0) {
                sz = (unsigned)p->em->structs[st].ssize;
                cc_lex_next(p->lx);
            } else if ((li = cc_local_find(p->em, p->lx->text)) >= 0) {
                /* sizeof(local): char→1 else word; arrays = count*8. */
                if (p->em->locals[li].is_char && !p->em->locals[li].is_ptr) {
                    sz = 1;
                } else if (p->em->locals[li].count > 1) {
                    sz = (unsigned)(p->em->locals[li].count * 8);
                } else {
                    sz = 8;
                }
                cc_lex_next(p->lx);
            } else {
                fail(p); /* unknown ident in sizeof */
                return;
            }
        } else {
            fail(p); /* illegal sizeof operand */
            return;
        }
        expect(p, TOK_RPAREN);
        cc_emit_mov_eax_imm(p->em, sz);
        return;
    }
    parse_primary(p);
}

static void parse_mul(CcParser* p) {
    parse_unary(p);
    while (p->lx->tok == TOK_STAR || p->lx->tok == TOK_SLASH ||
           p->lx->tok == TOK_PERCENT) {
        int op = p->lx->tok;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_unary(p);
        if (op == TOK_STAR) {
            cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
            cc_emit_pop_rcx(p->em);
            cc_emit_bytes(p->em, "\x48\x89\xC8", 3);
            cc_emit_bytes(p->em, "\x48\x0F\xAF\xC2", 4);
        } else {
            /* / or % : dividend in stack, divisor in rax */
            cc_emit_bytes(p->em, "\x48\x89\xC1", 3);
            cc_emit_byte(p->em, 0x58);
            cc_emit_bytes(p->em, "\x48\x99", 2);
            cc_emit_bytes(p->em, "\x48\xF7\xF9", 3);
            if (op == TOK_PERCENT) {
                cc_emit_bytes(p->em, "\x48\x89\xD0", 3); /* mov rax, rdx */
            }
        }
    }
}

static void parse_add(CcParser* p) {
    /* M288: track if LHS is a pointer local (ident) for scaling. */
    int lhs_ptr = 0;
    int lhs_ew = 8;
    if (p->lx->tok == TOK_IDENT) {
        int li = cc_local_find(p->em, p->lx->text);
        if (li >= 0 && p->em->locals[li].count == 1 &&
            p->em->locals[li].is_ptr) {
            lhs_ptr = 1;
            lhs_ew =
                p->em->locals[li].elem_w > 0 ? p->em->locals[li].elem_w : 8;
        }
    }
    parse_mul(p);
    while (p->lx->tok == TOK_PLUS || p->lx->tok == TOK_MINUS) {
        int op = p->lx->tok;
        int scale = lhs_ptr;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_mul(p);
        if (scale && op == TOK_PLUS) {
            /* ptr + int: rax=int, stack=ptr → ptr + int*ew */
            cc_emit_bytes(p->em, "\x48\x89\xC2", 3); /* mov rdx, rax (int) */
            cc_emit_pop_rcx(p->em);                 /* rcx = ptr */
            cc_emit_bytes(p->em, "\x48\x89\xD0", 3); /* mov rax, rdx */
            cc_emit_ptr_add_scaled(p->em, lhs_ew);
            lhs_ptr = 1; /* result still pointer */
        } else if (scale && op == TOK_MINUS) {
            /* ptr - int */
            cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
            cc_emit_pop_rcx(p->em);
            cc_emit_bytes(p->em, "\x48\x89\xD0", 3);
            if (lhs_ew == 2) {
                cc_emit_bytes(p->em, "\x48\xC1\xE0\x01", 4);
            } else if (lhs_ew == 4) {
                cc_emit_bytes(p->em, "\x48\xC1\xE0\x02", 4);
            } else if (lhs_ew != 1) {
                cc_emit_bytes(p->em, "\x48\xC1\xE0\x03", 4);
            }
            cc_emit_bytes(p->em, "\x48\x29\xC1", 3); /* sub rcx, rax */
            cc_emit_bytes(p->em, "\x48\x89\xC8", 3); /* mov rax, rcx */
            lhs_ptr = 1;
        } else {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tmov %rax, %rdx\n");
            }
            cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
            cc_emit_pop_rcx(p->em);
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tmov %rcx, %rax\n");
            }
            cc_emit_bytes(p->em, "\x48\x89\xC8", 3);
            if (op == TOK_PLUS) {
                if (p->em->asm_mode) {
                    cc_emit_asm_puts(p->em, "\tadd %rdx, %rax\n");
                }
                cc_emit_bytes(p->em, "\x48\x01\xD0", 3);
            } else {
                if (p->em->asm_mode) {
                    cc_emit_asm_puts(p->em, "\tsub %rdx, %rax\n");
                }
                cc_emit_bytes(p->em, "\x48\x29\xD0", 3);
            }
            lhs_ptr = 0;
        }
    }
}

static void parse_shift(CcParser* p) {
    parse_add(p);
    while (p->lx->tok == TOK_LSHIFT || p->lx->tok == TOK_RSHIFT) {
        int op = p->lx->tok;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_add(p);
        cc_emit_bytes(p->em, "\x48\x89\xC1", 3); /* mov rcx, rax (count) */
        cc_emit_byte(p->em, 0x58);               /* pop rax (value) */
        if (op == TOK_LSHIFT) {
            cc_emit_bytes(p->em, "\x48\xD3\xE0", 3); /* shl rax, cl */
        } else {
            cc_emit_bytes(p->em, "\x48\xD3\xE8", 3); /* shr rax, cl */
        }
    }
}

static void parse_cmp(CcParser* p) {
    parse_shift(p);
    while (p->lx->tok == TOK_EQ || p->lx->tok == TOK_NE || p->lx->tok == TOK_LT ||
           p->lx->tok == TOK_LE || p->lx->tok == TOK_GT || p->lx->tok == TOK_GE) {
        int op = p->lx->tok;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_shift(p);
        if (p->em->asm_mode) {
            cc_emit_asm_puts(p->em, "\tmov %rax, %rdx\n");
        }
        cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
        cc_emit_pop_rcx(p->em);
        if (p->em->asm_mode) {
            cc_emit_asm_puts(p->em, "\tcmp %rdx, %rcx\n");
        }
        cc_emit_bytes(p->em, "\x48\x39\xD1", 3);
        if (op == TOK_EQ) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tsete %al\n");
            }
            cc_emit_bytes(p->em, "\x0F\x94\xC0", 3);
        } else if (op == TOK_NE) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tsetne %al\n");
            }
            cc_emit_bytes(p->em, "\x0F\x95\xC0", 3);
        } else if (op == TOK_LT) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tsetl %al\n");
            }
            cc_emit_bytes(p->em, "\x0F\x9C\xC0", 3);
        } else if (op == TOK_LE) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tsetle %al\n");
            }
            cc_emit_bytes(p->em, "\x0F\x9E\xC0", 3);
        } else if (op == TOK_GT) {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tsetg %al\n");
            }
            cc_emit_bytes(p->em, "\x0F\x9F\xC0", 3);
        } else {
            if (p->em->asm_mode) {
                cc_emit_asm_puts(p->em, "\tsetge %al\n");
            }
            cc_emit_bytes(p->em, "\x0F\x9D\xC0", 3);
        }
        if (p->em->asm_mode) {
            cc_emit_asm_puts(p->em, "\tmovzbl %al, %eax\n");
        }
        cc_emit_bytes(p->em, "\x0F\xB6\xC0", 3);
    }
}

static void parse_xor(CcParser* p) {
    /* M289: bitwise ^ (between relational and &&). */
    parse_cmp(p);
    while (p->lx->tok == TOK_CARET) {
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_cmp(p);
        cc_emit_bytes(p->em, "\x48\x89\xC2", 3); /* mov rdx, rax */
        cc_emit_pop_rcx(p->em);
        cc_emit_bytes(p->em, "\x48\x89\xC8", 3); /* mov rax, rcx */
        cc_emit_bytes(p->em, "\x48\x31\xD0", 3); /* xor rax, rdx */
    }
}

static void parse_and(CcParser* p) {
    parse_xor(p);
    while (p->lx->tok == TOK_AMPAMP) {
        size_t je_at;
        cc_lex_next(p->lx);
        cc_emit_asm_test_rax(p->em);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_xor(p);
        patch_rel32(p->em, je_at, p->em->code_len);
    }
}

static void parse_or(CcParser* p) {
    parse_and(p);
    while (p->lx->tok == TOK_BARBAR) {
        size_t jne_at;
        cc_lex_next(p->lx);
        cc_emit_asm_test_rax(p->em);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        jne_at = emit_jne32(p->em);
        parse_and(p);
        patch_rel32(p->em, jne_at, p->em->code_len);
    }
}

static void parse_ternary(CcParser* p) {
    size_t je_at;
    size_t jmp_end;
    parse_or(p);
    if (!accept(p, TOK_QUESTION)) {
        return;
    }
    cc_emit_asm_test_rax(p->em);
    cc_emit_bytes(p->em, "\x85\xC0", 2);
    je_at = emit_je32(p->em);
    parse_ternary(p);
    jmp_end = emit_jmp32(p->em);
    patch_rel32(p->em, je_at, p->em->code_len);
    expect(p, TOK_COLON);
    parse_ternary(p);
    patch_rel32(p->em, jmp_end, p->em->code_len);
}

static void parse_expr(CcParser* p) {
    parse_ternary(p);
}

static void parse_stmt(CcParser* p);

static void parse_block(CcParser* p) {
    expect(p, TOK_LBRACE);
    while (!p->em->failed && p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
        parse_stmt(p);
    }
    expect(p, TOK_RBRACE);
}

static int loop_push(CcEmit* em, size_t cont_at, int is_switch) {
    CcLoop* lp;
    if (em->nloops >= CC_LOOP_MAX) {
        em->failed = 1;
        return -1;
    }
    lp = &em->loops[em->nloops++];
    lp->cont_at = cont_at;
    lp->nbrk = 0;
    lp->ncont = 0;
    lp->is_switch = is_switch;
    lp->asm_cont_id = 0;
    return 0;
}

static void loop_patch_continues(CcEmit* em, size_t target) {
    CcLoop* lp;
    int i;
    if (em->nloops <= 0) {
        return;
    }
    lp = &em->loops[em->nloops - 1];
    for (i = 0; i < lp->ncont; i++) {
        patch_rel32(em, lp->cont_patch[i], target);
    }
    lp->ncont = 0;
}

static void loop_pop(CcEmit* em, size_t exit_at) {
    CcLoop* lp;
    int i;
    if (em->nloops <= 0) {
        em->failed = 1;
        return;
    }
    lp = &em->loops[em->nloops - 1];
    for (i = 0; i < lp->nbrk; i++) {
        patch_rel32(em, lp->brk_at[i], exit_at);
    }
    em->nloops--;
}

static void parse_stmt(CcParser* p) {
    if (p->em->failed) {
        return;
    }
    if (p->lx->tok == TOK_LBRACE) {
        parse_block(p);
        return;
    }
    if (accept(p, TOK_STAR)) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        int slot;
        if (p->lx->tok != TOK_IDENT) {
            fail(p);
            return;
        }
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        expect(p, TOK_ASSIGN);
        idx = cc_local_find(p->em, name);
        if (idx < 0) {
            fail(p);
            return;
        }
        /* M140: reject deref-assign of struct values (non-pointers). */
        if (p->em->locals[idx].stype >= 0 && !p->em->locals[idx].is_ptr) {
            fail(p);
            return;
        }
        slot = p->em->locals[idx].slot;
        if (p->em->locals[idx].count != 1) {
            fail(p);
            return;
        }
        cc_emit_load_local(p->em, slot);
        cc_emit_push_rax(p->em);
        parse_expr(p);
        expect(p, TOK_SEMI);
        cc_emit_pop_rcx(p->em);
        cc_emit_store_indirect(p->em);
        return;
    }
    if (accept(p, TOK_KW_IF)) {
        size_t je_at;
        size_t jmp_end = 0;
        int has_else;
        expect(p, TOK_LPAREN);
        parse_expr(p);
        expect(p, TOK_RPAREN);
        cc_emit_asm_test_rax(p->em);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_stmt(p);
        has_else = accept(p, TOK_KW_ELSE);
        if (has_else) {
            jmp_end = emit_jmp32(p->em);
            patch_rel32(p->em, je_at, p->em->code_len);
            parse_stmt(p);
            patch_rel32(p->em, jmp_end, p->em->code_len);
        } else {
            patch_rel32(p->em, je_at, p->em->code_len);
        }
        return;
    }
    if (accept(p, TOK_KW_WHILE)) {
        size_t loop;
        size_t je_at;
        size_t back;
        int cont_lab = 0;
        expect(p, TOK_LPAREN);
        if (p->em->asm_mode) {
            cont_lab = cc_asm_new_label(p->em);
            cc_asm_def_label(p->em, cont_lab);
        }
        loop = p->em->code_len;
        if (loop_push(p->em, loop, 0) != 0) {
            return;
        }
        if (p->em->asm_mode) {
            p->em->loops[p->em->nloops - 1].asm_cont_id = cont_lab;
        }
        parse_expr(p);
        expect(p, TOK_RPAREN);
        cc_emit_asm_test_rax(p->em);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_stmt(p);
        loop_patch_continues(p->em, loop);
        if (p->em->asm_mode) {
            cc_asm_jmp_label(p->em, cont_lab);
            /* Binary back-edge without a second asm forward label. */
            cc_emit_byte(p->em, 0xE9);
            back = p->em->code_len;
            cc_emit_i32(p->em, 0);
            patch_rel32(p->em, back, loop);
        } else {
            back = emit_jmp32(p->em);
            patch_rel32(p->em, back, loop);
        }
        patch_rel32(p->em, je_at, p->em->code_len);
        loop_pop(p->em, p->em->code_len);
        return;
    }
    if (accept(p, TOK_KW_DO)) {
        size_t body;
        size_t jnz_at;
        int body_lab = 0;
        if (p->em->asm_mode) {
            body_lab = cc_asm_new_label(p->em);
            cc_asm_def_label(p->em, body_lab);
        }
        body = p->em->code_len;
        if (loop_push(p->em, body, 0) != 0) {
            return;
        }
        parse_stmt(p);
        expect(p, TOK_KW_WHILE);
        expect(p, TOK_LPAREN);
        loop_patch_continues(p->em, p->em->code_len);
        parse_expr(p);
        expect(p, TOK_RPAREN);
        expect(p, TOK_SEMI);
        cc_emit_asm_test_rax(p->em);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        if (p->em->asm_mode) {
            cc_asm_jne_label(p->em, body_lab);
            cc_emit_bytes(p->em, "\x0F\x85", 2);
            jnz_at = p->em->code_len;
            cc_emit_i32(p->em, 0);
            patch_rel32(p->em, jnz_at, body);
        } else {
            jnz_at = emit_jne32(p->em);
            patch_rel32(p->em, jnz_at, body);
        }
        loop_pop(p->em, p->em->code_len);
        return;
    }
    if (accept(p, TOK_KW_FOR)) {
        size_t cond_at;
        size_t je_at;
        size_t step_at;
        size_t back;
        char name[CC_IDENT_MAX + 1];
        int slot;
        CcLexer step_lx;
        CcLexer after_paren;
        int depth;
        int cond_lab = 0;
        expect(p, TOK_LPAREN);
        if (!accept(p, TOK_SEMI)) {
            if (p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
            cc_lex_next(p->lx);
            expect(p, TOK_ASSIGN);
            parse_expr(p);
            expect(p, TOK_SEMI);
            slot = cc_local_slot(p->em, name, 1);
            if (slot < 0) {
                fail(p);
                return;
            }
            cc_emit_store_local(p->em, slot);
        }
        if (loop_push(p->em, 0, 0) != 0) {
            return;
        }
        if (p->em->asm_mode) {
            cond_lab = cc_asm_new_label(p->em);
            cc_asm_def_label(p->em, cond_lab);
        }
        cond_at = p->em->code_len;
        if (accept(p, TOK_SEMI)) {
            cc_emit_mov_eax_imm(p->em, 1);
        } else {
            parse_expr(p);
            expect(p, TOK_SEMI);
        }
        cc_emit_asm_test_rax(p->em);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        /* Save step clause, skip to ')' then parse body, then emit step. */
        step_lx = *p->lx;
        depth = 1;
        while (!p->em->failed && p->lx->tok != TOK_EOF && depth > 0) {
            if (p->lx->tok == TOK_LPAREN) {
                depth++;
            } else if (p->lx->tok == TOK_RPAREN) {
                depth--;
                if (depth == 0) {
                    cc_lex_next(p->lx);
                    break;
                }
            }
            cc_lex_next(p->lx);
        }
        if (depth != 0) {
            fail(p);
            return;
        }
        parse_stmt(p);
        after_paren = *p->lx;
        *p->lx = step_lx;
        step_at = p->em->code_len;
        loop_patch_continues(p->em, step_at);
        if (p->lx->tok != TOK_RPAREN) {
            if (p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
            cc_lex_next(p->lx);
            expect(p, TOK_ASSIGN);
            parse_expr(p);
            slot = cc_local_slot(p->em, name, 1);
            if (slot < 0) {
                fail(p);
                return;
            }
            cc_emit_store_local(p->em, slot);
        }
        *p->lx = after_paren;
        if (p->em->asm_mode) {
            cc_asm_jmp_label(p->em, cond_lab);
            cc_emit_byte(p->em, 0xE9);
            back = p->em->code_len;
            cc_emit_i32(p->em, 0);
            patch_rel32(p->em, back, cond_at);
        } else {
            back = emit_jmp32(p->em);
            patch_rel32(p->em, back, cond_at);
        }
        patch_rel32(p->em, je_at, p->em->code_len);
        loop_pop(p->em, p->em->code_len);
        return;
    }
    if (accept(p, TOK_KW_BREAK)) {
        CcLoop* lp;
        size_t at;
        expect(p, TOK_SEMI);
        if (p->em->nloops <= 0) {
            fail(p);
            return;
        }
        lp = &p->em->loops[p->em->nloops - 1];
        if (lp->nbrk >= CC_BREAK_MAX) {
            fail(p);
            return;
        }
        at = emit_jmp32(p->em);
        lp->brk_at[lp->nbrk++] = at;
        return;
    }
    if (accept(p, TOK_KW_CONTINUE)) {
        CcLoop* lp = 0;
        size_t at;
        int i;
        expect(p, TOK_SEMI);
        for (i = p->em->nloops - 1; i >= 0; i--) {
            if (!p->em->loops[i].is_switch) {
                lp = &p->em->loops[i];
                break;
            }
        }
        if (!lp || lp->ncont >= CC_BREAK_MAX) {
            fail(p);
            return;
        }
        /* M299: while continue uses known loop-top label (not a fresh forward id). */
        if (p->em->asm_mode && lp->asm_cont_id > 0) {
            cc_asm_jmp_label(p->em, lp->asm_cont_id);
            cc_emit_byte(p->em, 0xE9);
            at = p->em->code_len;
            cc_emit_i32(p->em, 0);
        } else {
            at = emit_jmp32(p->em);
        }
        lp->cont_patch[lp->ncont++] = at;
        return;
    }
    if (accept(p, TOK_KW_SWITCH)) {
        size_t je_skip;
        size_t jmp_end;
        int saw_default = 0;
        expect(p, TOK_LPAREN);
        parse_expr(p);
        expect(p, TOK_RPAREN);
        cc_emit_push_rax(p->em); /* scrutinee */
        if (loop_push(p->em, 0, 1) != 0) {
            return;
        }
        expect(p, TOK_LBRACE);
        while (!p->em->failed && p->lx->tok != TOK_RBRACE &&
               p->lx->tok != TOK_EOF) {
            if (accept(p, TOK_KW_CASE)) {
                long imm;
                if (p->lx->tok != TOK_NUMBER) {
                    fail(p);
                    return;
                }
                imm = p->lx->number;
                cc_lex_next(p->lx);
                expect(p, TOK_COLON);
                /* mov rax,[rsp]; cmp rax,imm; jne skip */
                if (p->em->asm_mode) {
                    /* pop/push keeps scrutinee; as already accepts these. */
                    cc_emit_asm_puts(p->em, "\tpop %rax\n\tpush %rax\n");
                    cc_emit_asm_cmp_rax_imm(p->em, imm);
                }
                cc_emit_bytes(p->em, "\x48\x8B\x04\x24", 4);
                cc_emit_bytes(p->em, "\x48\x3D", 2);
                cc_emit_u32(p->em, (unsigned)imm);
                je_skip = emit_jne32(p->em);
                while (!p->em->failed && p->lx->tok != TOK_KW_CASE &&
                       p->lx->tok != TOK_KW_DEFAULT &&
                       p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
                    parse_stmt(p);
                }
                /* no fallthrough */
                jmp_end = emit_jmp32(p->em);
                {
                    CcLoop* lp = &p->em->loops[p->em->nloops - 1];
                    if (lp->nbrk < CC_BREAK_MAX) {
                        lp->brk_at[lp->nbrk++] = jmp_end;
                    } else {
                        fail(p);
                        return;
                    }
                }
                patch_rel32(p->em, je_skip, p->em->code_len);
                continue;
            }
            if (accept(p, TOK_KW_DEFAULT)) {
                if (saw_default) {
                    fail(p);
                    return;
                }
                saw_default = 1;
                expect(p, TOK_COLON);
                while (!p->em->failed && p->lx->tok != TOK_KW_CASE &&
                       p->lx->tok != TOK_KW_DEFAULT &&
                       p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
                    parse_stmt(p);
                }
                continue;
            }
            fail(p);
            return;
        }
        expect(p, TOK_RBRACE);
        loop_pop(p->em, p->em->code_len);
        if (p->em->asm_mode) {
            cc_emit_asm_puts(p->em, "\tadd $8, %rsp\n");
        }
        cc_emit_bytes(p->em, "\x48\x83\xC4\x08", 4); /* add rsp, 8 */
        return;
    }
    if (accept(p, TOK_KW_WRITE)) {
        char lit[CC_LIT_MAX + 1];
        size_t n = 0;
        if (p->lx->tok == TOK_STR) {
            if (p->lx->str_len > CC_LIT_MAX) {
                fail(p); /* M291: no silent truncate */
                return;
            }
            while (n < p->lx->str_len) {
                lit[n] = p->lx->str[n];
                n++;
            }
            lit[n] = 0;
            cc_lex_next(p->lx);
            expect(p, TOK_SEMI);
            cc_emit_write_lit2(p->em, lit);
            return;
        }
        if (p->lx->tok != TOK_IDENT) {
            fail(p);
            return;
        }
        while (n < CC_LIT_MAX && p->lx->text[n] != '\0') {
            lit[n] = p->lx->text[n];
            n++;
        }
        lit[n] = 0;
        cc_lex_next(p->lx);
        expect(p, TOK_SEMI);
        cc_emit_write_lit2(p->em, lit);
        return;
    }
    if (accept(p, TOK_KW_PRINT)) {
        parse_expr(p);
        expect(p, TOK_SEMI);
        cc_emit_print_rax(p->em);
        return;
    }
    if (accept(p, TOK_KW_INPUT)) {
        char name[CC_IDENT_MAX + 1];
        int slot;
        if (p->lx->tok != TOK_IDENT) {
            fail(p);
            return;
        }
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        expect(p, TOK_SEMI);
        slot = cc_local_slot(p->em, name, 1);
        if (slot < 0) {
            fail(p);
            return;
        }
        cc_emit_input_slot(p->em, slot);
        return;
    }
    if (accept(p, TOK_KW_RETURN)) {
        /* M289: bare `return;` → rax=0 then leave. */
        if (p->lx->tok != TOK_SEMI) {
            parse_expr(p);
        } else {
            cc_emit_mov_eax_imm(p->em, 0);
        }
        expect(p, TOK_SEMI);
        if (p->em->in_fn) {
            cc_emit_leave_ret(p->em);
        } else {
            cc_emit_bytes(p->em, "\x89\xC3", 2); /* mov ebx, eax */
            cc_emit_write_pass(p->em);
            cc_emit_exit_rax(p->em);
        }
        return;
    }
    if (is_type_tok(p)) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        int slot;
        int is_char = 0;
        int is_const = 0;
        int is_ptr = 0;
        /* M159/M171: void-star / T-star / multi-level pointer locals. */
        if (accept(p, TOK_KW_VOID)) {
            if (!accept(p, TOK_STAR)) {
                fail(p);
                return;
            }
            is_ptr = 1;
            while (accept(p, TOK_STAR)) {
            }
        } else if (!accept_type_ex(p, &is_char, &is_const)) {
            fail(p);
            return;
        } else if (accept(p, TOK_STAR)) {
            is_ptr = 1;
            while (accept(p, TOK_STAR)) {
            }
        }
        if (p->lx->tok != TOK_IDENT) {
            fail(p);
            return;
        }
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        if (accept(p, TOK_SEMI)) {
            if (is_const) {
                fail(p); /* const must be initialized */
                return;
            }
            if (cc_local_define(p->em, name, 1) < 0) {
                fail(p);
                return;
            }
            idx = cc_local_find(p->em, name);
            if (idx >= 0) {
                p->em->locals[idx].is_char = is_ptr ? 0 : is_char;
                p->em->locals[idx].is_ptr = is_ptr;
                if (is_ptr) {
                    p->em->locals[idx].elem_w = is_char ? 1 : 8;
                } else if (is_char) {
                    p->em->locals[idx].elem_w = 1;
                }
            }
            return;
        }
        if (accept(p, TOK_LBRACK)) {
            long dim;
            long dim2 = 1;
            if (is_const || is_ptr) {
                fail(p);
                return;
            }
            if (p->lx->tok != TOK_NUMBER) {
                fail(p);
                return;
            }
            dim = p->lx->number;
            cc_lex_next(p->lx);
            expect(p, TOK_RBRACK);
            /* M290: optional second dim → flattened count. */
            if (accept(p, TOK_LBRACK)) {
                if (is_char || p->lx->tok != TOK_NUMBER) {
                    fail(p);
                    return;
                }
                dim2 = p->lx->number;
                cc_lex_next(p->lx);
                expect(p, TOK_RBRACK);
            }
            expect(p, TOK_SEMI);
            if (dim < 1 || dim2 < 1 || dim * dim2 > CC_LOCAL_MAX) {
                fail(p);
                return;
            }
            if (cc_local_define(p->em, name, (int)(dim * dim2)) < 0) {
                fail(p);
                return;
            }
            if (is_char) {
                idx = cc_local_find(p->em, name);
                if (idx >= 0) {
                    p->em->locals[idx].is_char = 1;
                    p->em->locals[idx].elem_w = 1;
                }
            }
            return;
        }
        if (accept(p, TOK_ASSIGN)) {
            if (cc_local_define(p->em, name, 1) < 0) {
                fail(p);
                return;
            }
            idx = cc_local_find(p->em, name);
            if (idx < 0) {
                fail(p);
                return;
            }
            p->em->locals[idx].is_char = is_ptr ? 0 : is_char;
            p->em->locals[idx].is_const = is_const;
            p->em->locals[idx].is_ptr = is_ptr;
            if (is_ptr) {
                p->em->locals[idx].elem_w = is_char ? 1 : 8;
            } else if (is_char) {
                p->em->locals[idx].elem_w = 1;
            }
            slot = p->em->locals[idx].slot;
            parse_expr(p);
            expect(p, TOK_SEMI);
            if (is_char && !is_ptr) {
                emit_trunc_char(p->em);
            }
            cc_emit_store_local(p->em, slot);
            return;
        }
        fail(p);
        return;
    }
    if (p->lx->tok == TOK_IDENT) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        int slot;
        int st;
        int gi;
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        /* Expression statement: call(...); (libc-style side effects). */
        if (p->lx->tok == TOK_LPAREN) {
            parse_call(p, name);
            expect(p, TOK_SEMI);
            return;
        }
        st = cc_struct_find(p->em, name);
        if (st >= 0) {
            char inst[CC_IDENT_MAX + 1];
            int is_ptr = 0;
            int li;
            if (accept(p, TOK_STAR)) {
                is_ptr = 1;
            }
            if (p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            memcpy(inst, p->lx->text, CC_IDENT_MAX + 1);
            cc_lex_next(p->lx);
            {
                long dim = 1;
                if (accept(p, TOK_LBRACK)) {
                    if (is_ptr || p->lx->tok != TOK_NUMBER) {
                        fail(p);
                        return;
                    }
                    dim = p->lx->number;
                    cc_lex_next(p->lx);
                    expect(p, TOK_RBRACK);
                    if (dim < 1 || dim > CC_LOCAL_MAX) {
                        fail(p);
                        return;
                    }
                }
                expect(p, TOK_SEMI);
                if (is_ptr) {
                    if (cc_local_define_st(p->em, inst, 1, st) < 0) {
                        fail(p);
                        return;
                    }
                    li = cc_local_find(p->em, inst);
                    if (li < 0) {
                        fail(p);
                        return;
                    }
                    p->em->locals[li].is_ptr = 1;
                } else {
                    int words = (p->em->structs[st].ssize + 7) / 8;
                    long total;
                    if (words < 1) {
                        words = 1;
                    }
                    total = (long)words * dim;
                    if (total < 1 || total > CC_LOCAL_MAX) {
                        fail(p);
                        return;
                    }
                    if (cc_local_define_st(p->em, inst, (int)total, st) < 0) {
                        fail(p);
                    }
                }
            }
            return;
        }
        if (accept(p, TOK_SEMI)) {
            if (cc_local_define(p->em, name, 1) < 0) {
                fail(p);
            }
            return;
        }
        if (p->lx->tok == TOK_PLUSPLUS || p->lx->tok == TOK_MINUSMINUS) {
            int inc = (p->lx->tok == TOK_PLUSPLUS);
            cc_lex_next(p->lx);
            expect(p, TOK_SEMI);
            idx = cc_local_find(p->em, name);
            if (idx >= 0 && p->em->locals[idx].count == 1) {
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                if (inc) {
                    cc_emit_bytes(p->em, "\x48\xFF\xC0", 3); /* inc rax */
                } else {
                    cc_emit_bytes(p->em, "\x48\xFF\xC8", 3); /* dec rax */
                }
                (void)store_local_checked(p, idx);
                return;
            }
            gi = cc_global_find(p->em, name);
            if (gi < 0) {
                fail(p);
                return;
            }
            cc_emit_load_global(p->em, p->em->globals[gi].data_off);
            if (inc) {
                cc_emit_bytes(p->em, "\x48\xFF\xC0", 3);
            } else {
                cc_emit_bytes(p->em, "\x48\xFF\xC8", 3);
            }
            (void)store_global_checked(p, gi);
            return;
        }
        if (p->lx->tok == TOK_PLUSEQ || p->lx->tok == TOK_MINUSEQ ||
            p->lx->tok == TOK_PERCENTEQ) {
            int op = p->lx->tok;
            cc_lex_next(p->lx);
            idx = cc_local_find(p->em, name);
            if (idx >= 0 && p->em->locals[idx].count == 1) {
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                cc_emit_push_rax(p->em);
                parse_expr(p);
                expect(p, TOK_SEMI);
                if (op == TOK_PERCENTEQ) {
                    /* stack=lhs, rax=rhs → lhs % rhs */
                    cc_emit_bytes(p->em, "\x48\x89\xC1", 3); /* mov rcx, rax */
                    cc_emit_byte(p->em, 0x58);               /* pop rax */
                    cc_emit_bytes(p->em, "\x48\x99", 2);     /* cqo */
                    cc_emit_bytes(p->em, "\x48\xF7\xF9", 3); /* idiv rcx */
                    cc_emit_bytes(p->em, "\x48\x89\xD0", 3); /* mov rax, rdx */
                } else {
                    cc_emit_pop_rcx(p->em);
                    if (op == TOK_PLUSEQ) {
                        cc_emit_bytes(p->em, "\x48\x01\xC8", 3);
                    } else {
                        cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
                        cc_emit_bytes(p->em, "\x48\x89\xC8", 3);
                        cc_emit_bytes(p->em, "\x48\x29\xD0", 3);
                    }
                }
                (void)store_local_checked(p, idx);
                return;
            }
            gi = cc_global_find(p->em, name);
            if (gi < 0) {
                fail(p);
                return;
            }
            cc_emit_load_global(p->em, p->em->globals[gi].data_off);
            cc_emit_push_rax(p->em);
            parse_expr(p);
            expect(p, TOK_SEMI);
            if (op == TOK_PERCENTEQ) {
                cc_emit_bytes(p->em, "\x48\x89\xC1", 3);
                cc_emit_byte(p->em, 0x58);
                cc_emit_bytes(p->em, "\x48\x99", 2);
                cc_emit_bytes(p->em, "\x48\xF7\xF9", 3);
                cc_emit_bytes(p->em, "\x48\x89\xD0", 3);
            } else {
                cc_emit_pop_rcx(p->em);
                if (op == TOK_PLUSEQ) {
                    cc_emit_bytes(p->em, "\x48\x01\xC8", 3);
                } else {
                    cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
                    cc_emit_bytes(p->em, "\x48\x89\xC8", 3);
                    cc_emit_bytes(p->em, "\x48\x29\xD0", 3);
                }
            }
            (void)store_global_checked(p, gi);
            return;
        }
        if (accept(p, TOK_DOT)) {
            int off;
            idx = cc_local_find(p->em, name);
            if (idx < 0 || p->em->locals[idx].stype < 0 ||
                p->em->locals[idx].is_ptr || p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            {
                int width;
                char fname[CC_IDENT_MAX + 1];
                off = cc_struct_field(p->em, p->em->locals[idx].stype,
                                     p->lx->text);
                if (off < 0) {
                    fail(p);
                    return;
                }
                memcpy(fname, p->lx->text, CC_IDENT_MAX + 1);
                width = cc_struct_field_size(p->em, p->em->locals[idx].stype,
                                             fname);
                cc_lex_next(p->lx);
                expect(p, TOK_ASSIGN);
                parse_expr(p);
                expect(p, TOK_SEMI);
                cc_emit_store_local_bytes(p->em, p->em->locals[idx].slot, off,
                                          width > 0 ? width : 8);
                return;
            }
        }
        if (accept(p, TOK_ARROW)) {
            int off;
            int width;
            char fname[CC_IDENT_MAX + 1];
            idx = cc_local_find(p->em, name);
            if (idx < 0 || p->em->locals[idx].stype < 0 ||
                !p->em->locals[idx].is_ptr || p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            off = cc_struct_field(p->em, p->em->locals[idx].stype, p->lx->text);
            if (off < 0) {
                fail(p);
                return;
            }
            memcpy(fname, p->lx->text, CC_IDENT_MAX + 1);
            width = cc_struct_field_size(p->em, p->em->locals[idx].stype, fname);
            cc_lex_next(p->lx);
            expect(p, TOK_ASSIGN);
            cc_emit_load_local(p->em, p->em->locals[idx].slot);
            /* Stack locals: higher field → lower address; off is bytes. */
            emit_add_rax_imm(p->em, -off);
            cc_emit_push_rax(p->em);
            parse_expr(p);
            expect(p, TOK_SEMI);
            cc_emit_pop_rcx(p->em);
            cc_emit_store_indirect_width(p->em, width > 0 ? width : 8);
            return;
        }
        if (accept(p, TOK_LBRACK)) {
            if (p->lx->tok == TOK_NUMBER) {
                long dim = p->lx->number;
                long dim2 = 1;
                cc_lex_next(p->lx);
                expect(p, TOK_RBRACK);
                if (accept(p, TOK_LBRACK)) {
                    if (p->lx->tok != TOK_NUMBER) {
                        fail(p);
                        return;
                    }
                    dim2 = p->lx->number;
                    cc_lex_next(p->lx);
                    expect(p, TOK_RBRACK);
                }
                if (accept(p, TOK_SEMI)) {
                    if (dim < 1 || dim2 < 1 || dim * dim2 > CC_LOCAL_MAX) {
                        fail(p);
                        return;
                    }
                    if (cc_local_define(p->em, name, (int)(dim * dim2)) < 0) {
                        fail(p);
                    }
                    return;
                }
                if (accept(p, TOK_ASSIGN)) {
                    idx = cc_local_find(p->em, name);
                    if (idx < 0 || p->em->locals[idx].count < 1) {
                        fail(p);
                        return;
                    }
                    cc_emit_mov_eax_imm(p->em, (unsigned)dim);
                    cc_emit_push_rax(p->em);
                    parse_expr(p);
                    expect(p, TOK_SEMI);
                    cc_emit_pop_rcx(p->em);
                    {
                        int ew = p->em->locals[idx].elem_w > 0
                                     ? p->em->locals[idx].elem_w
                                     : 8;
                        cc_emit_index_store_n(p->em, p->em->locals[idx].slot,
                                              p->em->locals[idx].count, ew);
                    }
                    return;
                }
                fail(p);
                return;
            }
            idx = cc_local_find(p->em, name);
            if (idx < 0) {
                fail(p);
                return;
            }
            if (p->em->locals[idx].stype >= 0) {
                fail(p);
                return;
            }
            if (p->em->locals[idx].count > 1) {
                int ew = p->em->locals[idx].elem_w > 0
                             ? p->em->locals[idx].elem_w
                             : 8;
                parse_expr(p);
                expect(p, TOK_RBRACK);
                expect(p, TOK_ASSIGN);
                cc_emit_push_rax(p->em);
                parse_expr(p);
                expect(p, TOK_SEMI);
                cc_emit_pop_rcx(p->em);
                cc_emit_index_store_n(p->em, p->em->locals[idx].slot,
                                      p->em->locals[idx].count, ew);
                return;
            }
            if (p->em->locals[idx].count == 1) {
                int ew = p->em->locals[idx].elem_w > 0
                             ? p->em->locals[idx].elem_w
                             : 8;
                parse_expr(p);
                expect(p, TOK_RBRACK);
                expect(p, TOK_ASSIGN);
                cc_emit_push_rax(p->em); /* index */
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                cc_emit_pop_rcx(p->em); /* rcx=index, rax=ptr */
                if (ew == 2) {
                    cc_emit_bytes(p->em, "\x48\xC1\xE1\x01", 4);
                } else if (ew == 4) {
                    cc_emit_bytes(p->em, "\x48\xC1\xE1\x02", 4);
                } else if (ew != 1) {
                    cc_emit_bytes(p->em, "\x48\xC1\xE1\x03", 4);
                }
                cc_emit_bytes(p->em, "\x48\x01\xC8", 3); /* add rax, rcx */
                cc_emit_push_rax(p->em);                 /* addr */
                parse_expr(p);
                expect(p, TOK_SEMI);
                cc_emit_pop_rcx(p->em);
                cc_emit_ptr_index_store(p->em, ew);
                return;
            }
            fail(p);
            return;
        }
        expect(p, TOK_ASSIGN);
        if (p->em->failed) {
            return;
        }
        idx = cc_local_find(p->em, name);
        /* M290: struct blit `b=a;` — copy all words. */
        if (idx >= 0 && p->em->locals[idx].stype >= 0 &&
            !p->em->locals[idx].is_ptr && p->lx->tok == TOK_IDENT) {
            int src = cc_local_find(p->em, p->lx->text);
            if (src >= 0 && p->em->locals[src].stype == p->em->locals[idx].stype &&
                !p->em->locals[src].is_ptr &&
                p->em->locals[src].count == p->em->locals[idx].count) {
                int i;
                int n = p->em->locals[idx].count;
                cc_lex_next(p->lx);
                expect(p, TOK_SEMI);
                for (i = 0; i < n; i++) {
                    cc_emit_load_local(p->em, p->em->locals[src].slot + i);
                    cc_emit_store_local(p->em, p->em->locals[idx].slot + i);
                }
                return;
            }
        }
        {
            int from_addr = (p->lx->tok == TOK_AMP);
            parse_expr(p);
            if (p->em->failed) {
                return;
            }
            expect(p, TOK_SEMI);
            if (p->em->failed) {
                return;
            }
            idx = cc_local_find(p->em, name);
            if (idx >= 0) {
                if (p->em->locals[idx].count != 1) {
                    fail(p);
                    return;
                }
                (void)store_local_checked(p, idx);
                if (from_addr) {
                    p->em->locals[idx].is_ptr = 1;
                    if (p->em->locals[idx].elem_w < 1) {
                        p->em->locals[idx].elem_w = 8;
                    }
                }
                return;
            }
            gi = cc_global_find(p->em, name);
            if (gi >= 0) {
                (void)store_global_checked(p, gi);
                return;
            }
            slot = cc_local_slot(p->em, name, 1);
            if (slot < 0) {
                fail(p);
                return;
            }
            cc_emit_store_local(p->em, slot);
            if (from_addr) {
                idx = cc_local_find(p->em, name);
                if (idx >= 0) {
                    p->em->locals[idx].is_ptr = 1;
                    p->em->locals[idx].elem_w = 8;
                }
            }
            return;
        }
    }
    fail(p);
}

static void clear_locals(CcEmit* em) {
    em->nlocals = 0;
    em->next_slot = 0;
    em->nloops = 0;
}

static void parse_typedef_def(CcParser* p) {
    /* M150: typedef [unsigned] (int|long|char) name; — size_t alias etc. */
    char name[CC_IDENT_MAX + 1];
    expect(p, TOK_KW_TYPEDEF);
    (void)accept(p, TOK_KW_UNSIGNED);
    if (!accept(p, TOK_KW_INT) && !accept(p, TOK_KW_LONG) &&
        !accept(p, TOK_KW_CHAR)) {
        fail(p);
        return;
    }
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    expect(p, TOK_SEMI);
    if (cc_typedef_define(p->em, name) < 0) {
        fail(p);
    }
}

static void parse_enum_def(CcParser* p) {
    long next_val = 0;
    expect(p, TOK_KW_ENUM);
    expect(p, TOK_LBRACE);
    while (!p->em->failed && p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
        char name[CC_IDENT_MAX + 1];
        if (p->lx->tok != TOK_IDENT) {
            fail(p);
            return;
        }
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        if (accept(p, TOK_ASSIGN)) {
            if (p->lx->tok != TOK_NUMBER) {
                fail(p);
                return;
            }
            next_val = p->lx->number;
            cc_lex_next(p->lx);
        }
        if (cc_enum_define(p->em, name, next_val) < 0) {
            fail(p);
            return;
        }
        next_val++;
        if (!accept(p, TOK_COMMA)) {
            break;
        }
    }
    expect(p, TOK_RBRACE);
    expect(p, TOK_SEMI);
}

static void parse_fn_rest(CcParser* p, const char* name, int c_style) {
    CcFn* fn;
    int arity = 0;
    size_t n;
    int existing;

    existing = cc_fn_find(p->em, name);
    if (existing >= 0) {
        if (p->em->fns[existing].defined) {
            fail(p);
            return;
        }
        fn = &p->em->fns[existing];
    } else {
        if (p->em->nfns >= CC_FN_MAX) {
            fail(p);
            return;
        }
        fn = &p->em->fns[p->em->nfns];
        n = 0;
        while (n < CC_IDENT_MAX && name[n] != '\0') {
            fn->name[n] = name[n];
            n++;
        }
        fn->name[n] = 0;
        fn->arity = 0;
        p->em->nfns++;
    }
    fn->entry = p->em->code_len;
    fn->defined = 1;
    fn->sysv = p->em->obj_mode ? 1 : 0;
    if (p->em->asm_mode) {
        cc_emit_asm_fn_label(p->em, fn->name);
    }

    clear_locals(p->em);
    expect(p, TOK_LPAREN);
    if (c_style && accept(p, TOK_KW_VOID)) {
        expect(p, TOK_RPAREN);
    } else if (p->lx->tok != TOK_RPAREN) {
        for (;;) {
            int pst = -1;
            int is_ptr = 0;
            int is_char = 0;
            int li;
            char pname[CC_IDENT_MAX + 1];
            if (arity >= CC_FN_ARGS) {
                fail(p);
                return;
            }
            /* M290: optional T / T* / Struct* before param name. */
            if (p->lx->tok == TOK_IDENT) {
                int st = cc_struct_find(p->em, p->lx->text);
                if (st >= 0) {
                    pst = st;
                    cc_lex_next(p->lx);
                    if (accept(p, TOK_STAR)) {
                        is_ptr = 1;
                    }
                } else if (c_style) {
                    fail(p);
                    return;
                }
            } else if (is_type_tok(p) || p->lx->tok == TOK_KW_VOID) {
                if (accept(p, TOK_KW_VOID)) {
                    if (!accept(p, TOK_STAR)) {
                        fail(p);
                        return;
                    }
                    is_ptr = 1;
                } else if (!accept_type_ex(p, &is_char, 0)) {
                    fail(p);
                    return;
                } else if (accept(p, TOK_STAR)) {
                    is_ptr = 1;
                }
            } else if (c_style) {
                fail(p);
                return;
            }
            if (p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            memcpy(pname, p->lx->text, CC_IDENT_MAX + 1);
            if (pst >= 0) {
                if (cc_local_define_st(p->em, pname, 1, pst) < 0) {
                    fail(p);
                    return;
                }
            } else if (cc_local_define(p->em, pname, 1) < 0) {
                fail(p);
                return;
            }
            li = cc_local_find(p->em, pname);
            if (li >= 0) {
                p->em->locals[li].is_ptr = is_ptr || (pst >= 0 && is_ptr);
                if (is_ptr) {
                    p->em->locals[li].is_ptr = 1;
                    p->em->locals[li].elem_w = is_char ? 1 : 8;
                } else if (is_char) {
                    p->em->locals[li].is_char = 1;
                    p->em->locals[li].elem_w = 1;
                }
                if (pst >= 0 && is_ptr) {
                    p->em->locals[li].is_ptr = 1;
                }
            }
            arity++;
            cc_lex_next(p->lx);
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
        expect(p, TOK_RPAREN);
    } else {
        expect(p, TOK_RPAREN);
    }
    if (existing >= 0 && fn->arity != arity) {
        fail(p);
        return;
    }
    fn->arity = arity;
    if (fn->sysv) {
        cc_emit_fn_prologue_sysv(p->em, arity);
    } else {
        cc_emit_fn_prologue(p->em, arity);
    }
    p->em->in_fn = 1;
    parse_block(p);
    p->em->in_fn = 0;
    cc_emit_mov_eax_imm(p->em, 0);
    cc_emit_leave_ret(p->em);
    clear_locals(p->em);
}

static void parse_global_or_c_fn(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    unsigned long long init = 0;
    int is_void = 0;
    int is_char = 0;
    int is_const = 0;
    int gi;

    if (accept(p, TOK_KW_VOID)) {
        is_void = 1;
    } else if (!accept_type_ex(p, &is_char, &is_const)) {
        fail(p);
        return;
    }
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    if (p->lx->tok == TOK_LPAREN) {
        parse_fn_rest(p, name, 1);
        return;
    }
    if (is_void) {
        fail(p);
        return;
    }
    if (accept(p, TOK_ASSIGN)) {
        if (p->lx->tok != TOK_NUMBER) {
            fail(p);
            return;
        }
        init = (unsigned long long)p->lx->number;
        cc_lex_next(p->lx);
    } else if (is_const) {
        fail(p); /* const global must be initialized */
        return;
    }
    expect(p, TOK_SEMI);
    if (is_char) {
        init &= 0xffull;
    }
    gi = cc_global_define(p->em, name, init);
    if (gi < 0) {
        fail(p);
        return;
    }
    p->em->globals[gi].is_const = is_const;
}

static void parse_struct_def(CcParser* p) {
    CcStruct* st;
    size_t n;
    expect(p, TOK_KW_STRUCT);
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    if (cc_struct_find(p->em, p->lx->text) >= 0 ||
        p->em->nstructs >= CC_STRUCT_MAX) {
        fail(p);
        return;
    }
    st = &p->em->structs[p->em->nstructs];
    n = 0;
    while (n < CC_IDENT_MAX && p->lx->text[n] != '\0') {
        st->name[n] = p->lx->text[n];
        n++;
    }
    st->name[n] = 0;
    st->nfields = 0;
    st->ssize = 0;
    cc_lex_next(p->lx);
    expect(p, TOK_LBRACE);
    while (!p->em->failed && p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
        size_t fn;
        int width = 8;
        int is_char = 0;
        int off;
        int align;
        /* M248: compact foffs from fsizes + natural align (char1 / word8). */
        (void)accept(p, TOK_KW_UNSIGNED);
        if (accept(p, TOK_KW_CHAR)) {
            is_char = 1;
            width = 1;
        } else {
            (void)accept(p, TOK_KW_INT);
            (void)accept(p, TOK_KW_LONG);
        }
        if (p->lx->tok != TOK_IDENT || st->nfields >= CC_STRUCT_FIELDS) {
            fail(p);
            return;
        }
        fn = 0;
        while (fn < CC_IDENT_MAX && p->lx->text[fn] != '\0') {
            st->fields[st->nfields][fn] = p->lx->text[fn];
            fn++;
        }
        st->fields[st->nfields][fn] = 0;
        align = width >= 8 ? 8 : width;
        if (align < 1) {
            align = 1;
        }
        off = st->ssize;
        if (align > 1) {
            off = (off + align - 1) & ~(align - 1);
        }
        st->fsizes[st->nfields] = width;
        st->foffs[st->nfields] = off;
        st->ssize = off + width;
        (void)is_char;
        st->nfields++;
        cc_lex_next(p->lx);
        expect(p, TOK_SEMI);
    }
    expect(p, TOK_RBRACE);
    expect(p, TOK_SEMI);
    if (st->nfields < 1) {
        fail(p);
        return;
    }
    /* Final size: align up to max field align (≤8). */
    {
        int max_align = 1;
        int fi;
        for (fi = 0; fi < st->nfields; fi++) {
            int a = st->fsizes[fi] >= 8 ? 8 : st->fsizes[fi];
            if (a > max_align) {
                max_align = a;
            }
        }
        if (max_align > 1) {
            st->ssize = (st->ssize + max_align - 1) & ~(max_align - 1);
        }
        if (st->ssize < 1) {
            st->ssize = 1;
        }
    }
    p->em->nstructs++;
}

static void parse_extern_fn(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    CcFn* fn;
    int arity = 0;
    size_t n;
    expect(p, TOK_KW_EXTERN);
    expect(p, TOK_KW_FN);
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    if (cc_fn_find(p->em, name) >= 0 || p->em->nfns >= CC_FN_MAX) {
        fail(p);
        return;
    }
    fn = &p->em->fns[p->em->nfns];
    n = 0;
    while (n < CC_IDENT_MAX && name[n] != '\0') {
        fn->name[n] = name[n];
        n++;
    }
    fn->name[n] = 0;
    fn->entry = 0;
    fn->defined = 0;
    fn->sysv = 0;
    fn->arity = 0;
    expect(p, TOK_LPAREN);
    if (p->lx->tok != TOK_RPAREN) {
        for (;;) {
            if (p->lx->tok != TOK_IDENT || arity >= CC_FN_ARGS) {
                fail(p);
                return;
            }
            arity++;
            cc_lex_next(p->lx);
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
    }
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMI);
    fn->arity = arity;
    p->em->nfns++;
}

static void parse_import_fn(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    CcFn* fn;
    int arity = 0;
    size_t n;
    expect(p, TOK_KW_IMPORT);
    expect(p, TOK_KW_FN);
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    if (cc_fn_find(p->em, name) >= 0 || p->em->nfns >= CC_FN_MAX) {
        fail(p);
        return;
    }
    fn = &p->em->fns[p->em->nfns];
    n = 0;
    while (n < CC_IDENT_MAX && name[n] != '\0') {
        fn->name[n] = name[n];
        n++;
    }
    fn->name[n] = 0;
    fn->entry = 0;
    fn->defined = 0;
    fn->sysv = 1;
    fn->arity = 0;
    expect(p, TOK_LPAREN);
    if (p->lx->tok != TOK_RPAREN) {
        for (;;) {
            if (p->lx->tok != TOK_IDENT || arity >= CC_FN_ARGS) {
                fail(p);
                return;
            }
            arity++;
            cc_lex_next(p->lx);
            if (!accept(p, TOK_COMMA)) {
                break;
            }
        }
    }
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMI);
    fn->arity = arity;
    p->em->nfns++;
}

static void parse_function(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    expect(p, TOK_KW_FN);
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    parse_fn_rest(p, name, 0);
}

static void parse_defs(CcParser* p) {
    int guard = 0;
    while (!p->em->failed &&
           (p->lx->tok == TOK_KW_FN || p->lx->tok == TOK_KW_STRUCT ||
            p->lx->tok == TOK_KW_INT || p->lx->tok == TOK_KW_LONG ||
            p->lx->tok == TOK_KW_CHAR || p->lx->tok == TOK_KW_CONST ||
            p->lx->tok == TOK_KW_UNSIGNED || p->lx->tok == TOK_KW_ENUM ||
            p->lx->tok == TOK_KW_EXTERN || p->lx->tok == TOK_KW_IMPORT ||
            p->lx->tok == TOK_KW_TYPEDEF || p->lx->tok == TOK_KW_VOID ||
            (p->lx->tok == TOK_IDENT &&
             cc_typedef_find(p->em, p->lx->text) >= 0))) {
        if (++guard > 64) {
            p->em->failed = 1;
            break;
        }
        if (p->lx->tok == TOK_KW_EXTERN) {
            parse_extern_fn(p);
        } else if (p->lx->tok == TOK_KW_IMPORT) {
            parse_import_fn(p);
        } else if (p->lx->tok == TOK_KW_FN) {
            parse_function(p);
        } else if (p->lx->tok == TOK_KW_STRUCT) {
            parse_struct_def(p);
        } else if (p->lx->tok == TOK_KW_ENUM) {
            parse_enum_def(p);
        } else if (p->lx->tok == TOK_KW_TYPEDEF) {
            parse_typedef_def(p);
        } else {
            parse_global_or_c_fn(p);
        }
    }
}

static int resolve_calls(CcEmit* em) {
    int i;
    int ndef = 0;
    for (i = 0; i < em->ncalls; i++) {
        int fi = em->calls[i].fn;
        if (fi < 0 || fi >= em->nfns) {
            return -1;
        }
        if (!em->fns[fi].defined) {
            /* ET_EXEC must resolve; obj_mode emits UND + PLT32 (M125). */
            if (!em->obj_mode) {
                return -1;
            }
            continue;
        }
        if (!em->obj_mode) {
            patch_rel32(em, em->calls[i].at, em->fns[fi].entry);
        }
    }
    for (i = 0; i < em->nfns; i++) {
        if (em->fns[i].defined) {
            ndef++;
        } else if (!em->obj_mode) {
            return -1;
        }
    }
    /* obj_mode needs ≥1 defined export; ET_EXEC may have zero fns (CRT). */
    if (em->obj_mode && ndef < 1) {
        return -1;
    }
    return 0;
}

static int g_last_fail_code;
static int g_last_fail_line;
static int g_last_fail_col;
static char g_asm_buf[CC_SRC_MAX];

int cc_last_fail_code(void) {
    return g_last_fail_code;
}

int cc_last_fail_line(void) {
    return g_last_fail_line;
}

int cc_last_fail_col(void) {
    return g_last_fail_col;
}

void cc_note_fail(int code, int line, int col) {
    g_last_fail_code = code;
    g_last_fail_line = line;
    g_last_fail_col = col;
}

static void publish_fail(CcEmit* em, int code) {
    g_last_fail_code = code;
    g_last_fail_line = em->fail_line;
    g_last_fail_col = em->fail_col;
}

static int cc_compile_ex(const char* src, size_t len, unsigned char** out_elf,
                         size_t* out_len, int obj_mode, int asm_mode,
                         char** out_asm, size_t* out_asm_len) {
    CcLexer lx;
    CcParser p;
    const char magic[] = "CXC_A2";
    size_t i;
    size_t jmp_entry = 0;
    size_t entry_at;
    size_t expanded_len = 0;
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];
    static unsigned char ro_buf[CC_DATA_MAX];
    static char expanded[CC_SRC_MAX];

    g_last_fail_code = 0;
    g_last_fail_line = 0;
    g_last_fail_col = 0;
    memset(&em, 0, sizeof(em));
    em.code_cap = CC_CODE_MAX;
    em.data_cap = CC_DATA_MAX;
    em.ro_cap = CC_DATA_MAX;
    em.code = code_buf;
    em.data = data_buf;
    em.ro = ro_buf;
    em.code_len = 0;
    em.data_len = 0;
    em.nlocals = 0;
    em.next_slot = 0;
    em.nloops = 0;
    em.nfns = 0;
    em.in_fn = 0;
    em.nstructs = 0;
    em.nglobals = 0;
    em.nenums = 0;
    em.ncalls = 0;
    em.nexports = 0;
    em.ntypedefs = 0;
    em.failed = 0;
    em.fail_code = 0;
    em.fail_line = 0;
    em.fail_col = 0;
    em.asm_mode = asm_mode ? 1 : 0;
    em.asm_buf = g_asm_buf;
    em.asm_cap = sizeof(g_asm_buf);
    em.asm_len = 0;
    g_asm_buf[0] = 0;
    em.obj_mode = obj_mode ? 1 : 0;
    cc_reloc_reset();
    cc_link_reset();
    cc_lib_reset();
    cc_und_reset();

    if (cc_preprocess(src, len, expanded, sizeof(expanded), &expanded_len) !=
        0) {
        cc_note_fail(10, cc_prep_last_fail_line(), cc_prep_last_fail_col());
        return -1;
    }
    src = expanded;
    len = expanded_len;

    if (len < sizeof(magic) - 1) {
        return -1;
    }
    for (i = 0; i < sizeof(magic) - 1; i++) {
        if (src[i] != magic[i]) {
            return -1;
        }
    }

    cc_lex_init(&lx, src, len);
    if (lx.tok == TOK_UNKNOWN) {
        g_last_fail_code = 60;
        g_last_fail_line = lx.line;
        g_last_fail_col = lx.col;
        return -1;
    }
    if (lx.tok == TOK_IDENT && strcmp(lx.text, "CXC_A2") == 0) {
        cc_lex_next(&lx);
        if (lx.tok == TOK_UNKNOWN) {
            g_last_fail_code = 60;
            g_last_fail_line = lx.line;
            g_last_fail_col = lx.col;
            return -1;
        }
    }

    p.lx = &lx;
    p.em = &em;

    /* ET_EXEC: jmp over fns to CRT. ET_REL: fn bodies from offset 0. */
    if (!em.obj_mode) {
        jmp_entry = emit_jmp32(&em);
    }
    parse_defs(&p);
    {
        int li;
        static char link_raw[CC_SRC_MAX];
        static char link_exp[CC_SRC_MAX];
        for (li = 0; !em.failed && li < cc_link_count(); li++) {
            const char* path = cc_link_at(li);
            size_t raw_n = 0;
            size_t exp_n = 0;
            CcLexer llx;
            if (!path || cc_read_source(path, link_raw, sizeof(link_raw),
                                        &raw_n) != 0) {
                em.failed = 1;
                break;
            }
            if (cc_preprocess(link_raw, raw_n, link_exp, sizeof(link_exp),
                              &exp_n) != 0) {
                em.failed = 1;
                break;
            }
            cc_lex_init(&llx, link_exp, exp_n);
            if (llx.tok == TOK_IDENT && strcmp(llx.text, "CXC_A2") == 0) {
                cc_lex_next(&llx);
            }
            p.lx = &llx;
            parse_defs(&p);
            if (llx.tok != TOK_EOF) {
                em.failed = 1;
                break;
            }
            p.lx = &lx;
        }
    }
    {
        int li;
        for (li = 0; !em.failed && li < cc_lib_count(); li++) {
            const char* path = cc_lib_at(li);
            if (!path || cc_import_elf(&em, path) != 0) {
                em.failed = 1;
                if (em.fail_code == 0) {
                    em.fail_code = 20;
                }
                break;
            }
        }
        if (!em.failed && cc_und_resolve(&em) != 0) {
            em.failed = 1;
            if (em.fail_code == 0) {
                em.fail_code = 21;
            }
        }
    }
    if (em.failed) {
        publish_fail(&em, em.fail_code ? em.fail_code : 20);
        return -1;
    }
    if (resolve_calls(&em) != 0) {
        publish_fail(&em, em.fail_code ? em.fail_code : 11);
        return -1;
    }
    if (em.obj_mode) {
        if (cc_fn_find(&em, "main") >= 0) {
            publish_fail(&em, 30);
            return -1;
        }
        if (em.nfns < 1) {
            publish_fail(&em, 30);
            return -1;
        }
        if (lx.tok != TOK_EOF) {
            if (em.fail_line == 0) {
                em.fail_line = lx.line;
                em.fail_col = lx.col;
            }
            publish_fail(&em, 30);
            return -1;
        }
        if (em.asm_mode) {
            if (out_asm && out_asm_len) {
                g_asm_buf[em.asm_len] = 0;
                *out_asm = g_asm_buf;
                *out_asm_len = em.asm_len;
            }
            return 0;
        }
        if (cc_build_rel_elf(&em, out_elf, out_len) != 0) {
            publish_fail(&em, 20);
            return -1;
        }
        return 0;
    }
    entry_at = em.code_len;
    if (!em.asm_mode) {
        patch_rel32(&em, jmp_entry, entry_at);
    }
    clear_locals(&em);
    cc_emit_prologue(&em);
    {
        int main_i = cc_fn_find(&em, "main");
        if (main_i >= 0) {
            size_t call_at;
            if (em.fns[main_i].arity != 0 || !em.fns[main_i].defined) {
                return -1;
            }
            call_at = cc_emit_call_rel32(&em);
            if (!em.asm_mode) {
                patch_rel32(&em, call_at, em.fns[main_i].entry);
            }
            cc_emit_bytes(&em, "\x89\xC3", 2); /* mov ebx, eax */
            cc_emit_write_pass(&em);
            cc_emit_exit_rax(&em);
            if (lx.tok != TOK_EOF) {
                return -1;
            }
        } else {
            int guard = 0;
            while (!em.failed && lx.tok != TOK_EOF) {
                if (++guard > 512) {
                    em.failed = 1;
                    break;
                }
                /* Allow int/long/char locals in script body (M140 long). */
                if (lx.tok == TOK_KW_FN || lx.tok == TOK_KW_STRUCT ||
                    lx.tok == TOK_KW_ENUM || lx.tok == TOK_KW_EXTERN ||
                    lx.tok == TOK_KW_IMPORT || lx.tok == TOK_KW_TYPEDEF) {
                    em.failed = 1;
                    break;
                }
                parse_stmt(&p);
            }
        }
    }
    if (em.failed) {
        publish_fail(&em, em.fail_code ? em.fail_code : 12);
        return -1;
    }
    if (em.asm_mode) {
        if (out_asm && out_asm_len) {
            g_asm_buf[em.asm_len] = 0;
            *out_asm = g_asm_buf;
            *out_asm_len = em.asm_len;
        }
        return 0;
    }
    return cc_build_elf(&em, out_elf, out_len);
}

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len) {
    return cc_compile_ex(src, len, out_elf, out_len, 0, 0, 0, 0);
}

int cc_compile_obj(const char* src, size_t len, unsigned char** out_elf,
                   size_t* out_len) {
    return cc_compile_ex(src, len, out_elf, out_len, 1, 0, 0, 0);
}

int cc_compile_asm(const char* src, size_t len, char** out_asm,
                   size_t* out_len) {
    /* -S: ET_REL-shaped (obj_mode) + asm text; no main(); use named fn. */
    return cc_compile_ex(src, len, 0, 0, 1, 1, out_asm, out_len);
}
