#include "cxc_cc/cc.h"

typedef struct CcParser {
    CcLexer* lx;
    CcEmit* em;
} CcParser;

static void fail(CcParser* p) {
    p->em->failed = 1;
}

static int accept(CcParser* p, int tok) {
    if (p->lx->tok == tok) {
        cc_lex_next(p->lx);
        return 1;
    }
    return 0;
}

static void expect(CcParser* p, int tok) {
    if (!accept(p, tok)) {
        fail(p);
    }
}

static void parse_expr(CcParser* p);

static size_t emit_je32(CcEmit* em) {
    size_t at;
    cc_emit_bytes(em, "\x0F\x84", 2);
    at = em->code_len;
    cc_emit_i32(em, 0);
    return at;
}

static size_t emit_jne32(CcEmit* em) {
    size_t at;
    cc_emit_bytes(em, "\x0F\x85", 2);
    at = em->code_len;
    cc_emit_i32(em, 0);
    return at;
}

static size_t emit_jmp32(CcEmit* em) {
    size_t at;
    cc_emit_byte(em, 0xE9);
    at = em->code_len;
    cc_emit_i32(em, 0);
    return at;
}

static void patch_rel32(CcEmit* em, size_t at, size_t target) {
    int rel = (int)(target - (at + 4));
    em->code[at] = (unsigned char)(rel);
    em->code[at + 1] = (unsigned char)(rel >> 8);
    em->code[at + 2] = (unsigned char)(rel >> 16);
    em->code[at + 3] = (unsigned char)(rel >> 24);
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
    call_at = cc_emit_call_rel32(p->em);
    if (p->em->fns[fi].defined) {
        patch_rel32(p->em, call_at, p->em->fns[fi].entry);
    } else {
        if (p->em->ncalls >= CC_CALL_MAX) {
            fail(p);
            return;
        }
        p->em->calls[p->em->ncalls].at = call_at;
        p->em->calls[p->em->ncalls].fn = fi;
        p->em->ncalls++;
    }
    if (argc > 0) {
        /* add rsp, 8*argc */
        cc_emit_bytes(p->em, "\x48\x81\xC4", 3);
        cc_emit_u32(p->em, (unsigned)(argc * 8));
    }
}

static void parse_primary(CcParser* p) {
    if (p->lx->tok == TOK_NUMBER) {
        cc_emit_mov_eax_imm(p->em, (unsigned)p->lx->number);
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
                parse_expr(p);
                expect(p, TOK_RBRACK);
                cc_emit_index_load(p->em, p->em->locals[idx].slot);
                return;
            }
            if (p->em->locals[idx].count == 1) {
                /* pointer word index: *(p + i) with 8-byte scale */
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                cc_emit_push_rax(p->em);
                parse_expr(p);
                expect(p, TOK_RBRACK);
                cc_emit_bytes(p->em, "\x48\xC1\xE0\x03", 4); /* shl rax, 3 */
                cc_emit_pop_rcx(p->em);
                cc_emit_bytes(p->em, "\x48\x01\xC8", 3); /* add rax, rcx */
                cc_emit_load_indirect(p->em);
                return;
            }
            fail(p);
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
            cc_emit_load_local(p->em, p->em->locals[idx].slot + off);
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
        cc_emit_lea_local(p->em, p->em->locals[idx].slot + off);
        return;
    }
    if (accept(p, TOK_LBRACK)) {
        if (p->em->locals[idx].count < 1) {
            fail(p);
            return;
        }
        parse_expr(p);
        expect(p, TOK_RBRACK);
        cc_emit_index_lea(p->em, p->em->locals[idx].slot);
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
        unsigned sz = 8;
        expect(p, TOK_LPAREN);
        if (accept(p, TOK_KW_INT)) {
            sz = 8;
        } else if (p->lx->tok == TOK_IDENT) {
            int st = cc_struct_find(p->em, p->lx->text);
            if (st >= 0) {
                sz = (unsigned)(p->em->structs[st].nfields * 8);
                cc_lex_next(p->lx);
            } else {
                parse_expr(p);
                sz = 8;
            }
        } else {
            parse_expr(p);
            sz = 8;
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
    parse_mul(p);
    while (p->lx->tok == TOK_PLUS || p->lx->tok == TOK_MINUS) {
        int op = p->lx->tok;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_mul(p);
        cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
        cc_emit_pop_rcx(p->em);
        cc_emit_bytes(p->em, "\x48\x89\xC8", 3);
        if (op == TOK_PLUS) {
            cc_emit_bytes(p->em, "\x48\x01\xD0", 3);
        } else {
            cc_emit_bytes(p->em, "\x48\x29\xD0", 3);
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
        cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
        cc_emit_pop_rcx(p->em);
        cc_emit_bytes(p->em, "\x48\x39\xD1", 3);
        if (op == TOK_EQ) {
            cc_emit_bytes(p->em, "\x0F\x94\xC0", 3);
        } else if (op == TOK_NE) {
            cc_emit_bytes(p->em, "\x0F\x95\xC0", 3);
        } else if (op == TOK_LT) {
            cc_emit_bytes(p->em, "\x0F\x9C\xC0", 3);
        } else if (op == TOK_LE) {
            cc_emit_bytes(p->em, "\x0F\x9E\xC0", 3);
        } else if (op == TOK_GT) {
            cc_emit_bytes(p->em, "\x0F\x9F\xC0", 3);
        } else {
            cc_emit_bytes(p->em, "\x0F\x9D\xC0", 3);
        }
        cc_emit_bytes(p->em, "\x0F\xB6\xC0", 3);
    }
}

static void parse_and(CcParser* p) {
    parse_cmp(p);
    while (p->lx->tok == TOK_AMPAMP) {
        size_t je_at;
        cc_lex_next(p->lx);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_cmp(p);
        patch_rel32(p->em, je_at, p->em->code_len);
    }
}

static void parse_or(CcParser* p) {
    parse_and(p);
    while (p->lx->tok == TOK_BARBAR) {
        size_t jne_at;
        cc_lex_next(p->lx);
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
        int slot;
        if (p->lx->tok != TOK_IDENT) {
            fail(p);
            return;
        }
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        expect(p, TOK_ASSIGN);
        slot = cc_local_slot(p->em, name, 0);
        if (slot < 0) {
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
        size_t loop = p->em->code_len;
        size_t je_at;
        size_t back;
        expect(p, TOK_LPAREN);
        if (loop_push(p->em, loop, 0) != 0) {
            return;
        }
        parse_expr(p);
        expect(p, TOK_RPAREN);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_stmt(p);
        loop_patch_continues(p->em, loop);
        back = emit_jmp32(p->em);
        patch_rel32(p->em, back, loop);
        patch_rel32(p->em, je_at, p->em->code_len);
        loop_pop(p->em, p->em->code_len);
        return;
    }
    if (accept(p, TOK_KW_DO)) {
        size_t body = p->em->code_len;
        size_t jnz_at;
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
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        jnz_at = emit_jne32(p->em);
        patch_rel32(p->em, jnz_at, body);
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
        cond_at = p->em->code_len;
        if (accept(p, TOK_SEMI)) {
            cc_emit_mov_eax_imm(p->em, 1);
        } else {
            parse_expr(p);
            expect(p, TOK_SEMI);
        }
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
        back = emit_jmp32(p->em);
        patch_rel32(p->em, back, cond_at);
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
        at = emit_jmp32(p->em);
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
        cc_emit_bytes(p->em, "\x48\x83\xC4\x08", 4); /* add rsp, 8 */
        return;
    }
    if (accept(p, TOK_KW_WRITE)) {
        char lit[CC_LIT_MAX + 1];
        size_t n = 0;
        if (p->lx->tok == TOK_STR) {
            while (n < CC_LIT_MAX && n < p->lx->str_len) {
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
        parse_expr(p);
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
    accept(p, TOK_KW_INT);
    if (p->lx->tok == TOK_IDENT) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        int slot;
        int st;
        int gi;
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        st = cc_struct_find(p->em, name);
        if (st >= 0) {
            char inst[CC_IDENT_MAX + 1];
            if (p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            memcpy(inst, p->lx->text, CC_IDENT_MAX + 1);
            cc_lex_next(p->lx);
            expect(p, TOK_SEMI);
            if (cc_local_define_st(p->em, inst, p->em->structs[st].nfields,
                                   st) < 0) {
                fail(p);
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
                cc_emit_store_local(p->em, p->em->locals[idx].slot);
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
            cc_emit_store_global(p->em, p->em->globals[gi].data_off);
            return;
        }
        if (p->lx->tok == TOK_PLUSEQ || p->lx->tok == TOK_MINUSEQ) {
            int add = (p->lx->tok == TOK_PLUSEQ);
            cc_lex_next(p->lx);
            idx = cc_local_find(p->em, name);
            if (idx >= 0 && p->em->locals[idx].count == 1) {
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                cc_emit_push_rax(p->em);
                parse_expr(p);
                expect(p, TOK_SEMI);
                cc_emit_pop_rcx(p->em);
                if (add) {
                    cc_emit_bytes(p->em, "\x48\x01\xC8", 3); /* add rax, rcx */
                } else {
                    cc_emit_bytes(p->em, "\x48\x89\xC2", 3); /* mov rdx, rax */
                    cc_emit_bytes(p->em, "\x48\x89\xC8", 3); /* mov rax, rcx */
                    cc_emit_bytes(p->em, "\x48\x29\xD0", 3); /* sub rax, rdx */
                }
                cc_emit_store_local(p->em, p->em->locals[idx].slot);
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
            cc_emit_pop_rcx(p->em);
            if (add) {
                cc_emit_bytes(p->em, "\x48\x01\xC8", 3);
            } else {
                cc_emit_bytes(p->em, "\x48\x89\xC2", 3);
                cc_emit_bytes(p->em, "\x48\x89\xC8", 3);
                cc_emit_bytes(p->em, "\x48\x29\xD0", 3);
            }
            cc_emit_store_global(p->em, p->em->globals[gi].data_off);
            return;
        }
        if (accept(p, TOK_DOT)) {
            int off;
            idx = cc_local_find(p->em, name);
            if (idx < 0 || p->em->locals[idx].stype < 0 ||
                p->lx->tok != TOK_IDENT) {
                fail(p);
                return;
            }
            off = cc_struct_field(p->em, p->em->locals[idx].stype, p->lx->text);
            if (off < 0) {
                fail(p);
                return;
            }
            cc_lex_next(p->lx);
            expect(p, TOK_ASSIGN);
            parse_expr(p);
            expect(p, TOK_SEMI);
            cc_emit_store_local(p->em, p->em->locals[idx].slot + off);
            return;
        }
        if (accept(p, TOK_LBRACK)) {
            if (p->lx->tok == TOK_NUMBER) {
                long dim = p->lx->number;
                cc_lex_next(p->lx);
                expect(p, TOK_RBRACK);
                if (accept(p, TOK_SEMI)) {
                    if (dim < 1 || dim > CC_LOCAL_MAX) {
                        fail(p);
                        return;
                    }
                    if (cc_local_define(p->em, name, (int)dim) < 0) {
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
                    cc_emit_index_store(p->em, p->em->locals[idx].slot);
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
                parse_expr(p);
                expect(p, TOK_RBRACK);
                expect(p, TOK_ASSIGN);
                cc_emit_push_rax(p->em);
                parse_expr(p);
                expect(p, TOK_SEMI);
                cc_emit_pop_rcx(p->em);
                cc_emit_index_store(p->em, p->em->locals[idx].slot);
                return;
            }
            if (p->em->locals[idx].count == 1) {
                parse_expr(p);
                expect(p, TOK_RBRACK);
                expect(p, TOK_ASSIGN);
                cc_emit_push_rax(p->em); /* index */
                cc_emit_load_local(p->em, p->em->locals[idx].slot);
                cc_emit_pop_rcx(p->em);
                cc_emit_bytes(p->em, "\x48\xC1\xE1\x03", 4); /* shl rcx, 3 */
                cc_emit_bytes(p->em, "\x48\x01\xC8", 3);     /* add rax, rcx */
                cc_emit_push_rax(p->em); /* addr */
                parse_expr(p);
                expect(p, TOK_SEMI);
                cc_emit_pop_rcx(p->em);
                cc_emit_store_indirect(p->em);
                return;
            }
            fail(p);
            return;
        }
        expect(p, TOK_ASSIGN);
        if (p->em->failed) {
            return;
        }
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
            cc_emit_store_local(p->em, p->em->locals[idx].slot);
            return;
        }
        gi = cc_global_find(p->em, name);
        if (gi >= 0) {
            cc_emit_store_global(p->em, p->em->globals[gi].data_off);
            return;
        }
        slot = cc_local_slot(p->em, name, 1);
        if (slot < 0) {
            fail(p);
            return;
        }
        cc_emit_store_local(p->em, slot);
        return;
    }
    fail(p);
}

static void clear_locals(CcEmit* em) {
    em->nlocals = 0;
    em->next_slot = 0;
    em->nloops = 0;
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

static void parse_global_def(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    unsigned long long init = 0;
    expect(p, TOK_KW_INT);
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
        init = (unsigned long long)p->lx->number;
        cc_lex_next(p->lx);
    }
    expect(p, TOK_SEMI);
    if (cc_global_define(p->em, name, init) < 0) {
        fail(p);
    }
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
    cc_lex_next(p->lx);
    expect(p, TOK_LBRACE);
    while (!p->em->failed && p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
        size_t fn;
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
    CcFn* fn;
    int arity = 0;
    size_t n;
    int existing;

    expect(p, TOK_KW_FN);
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
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

    clear_locals(p->em);
    expect(p, TOK_LPAREN);
    if (p->lx->tok != TOK_RPAREN) {
        for (;;) {
            if (p->lx->tok != TOK_IDENT || arity >= CC_FN_ARGS) {
                fail(p);
                return;
            }
            if (cc_local_define(p->em, p->lx->text, 1) < 0) {
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
    if (existing >= 0 && fn->arity != arity) {
        fail(p);
        return;
    }
    fn->arity = arity;
    cc_emit_fn_prologue(p->em, arity);
    p->em->in_fn = 1;
    parse_block(p);
    p->em->in_fn = 0;
    /* fallthrough return 0 */
    cc_emit_mov_eax_imm(p->em, 0);
    cc_emit_leave_ret(p->em);
    clear_locals(p->em);
}

static void parse_defs(CcParser* p) {
    int guard = 0;
    while (!p->em->failed &&
           (p->lx->tok == TOK_KW_FN || p->lx->tok == TOK_KW_STRUCT ||
            p->lx->tok == TOK_KW_INT || p->lx->tok == TOK_KW_ENUM ||
            p->lx->tok == TOK_KW_EXTERN)) {
        if (++guard > 64) {
            p->em->failed = 1;
            break;
        }
        if (p->lx->tok == TOK_KW_EXTERN) {
            parse_extern_fn(p);
        } else if (p->lx->tok == TOK_KW_FN) {
            parse_function(p);
        } else if (p->lx->tok == TOK_KW_STRUCT) {
            parse_struct_def(p);
        } else if (p->lx->tok == TOK_KW_ENUM) {
            parse_enum_def(p);
        } else {
            parse_global_def(p);
        }
    }
}

static int resolve_calls(CcEmit* em) {
    int i;
    for (i = 0; i < em->ncalls; i++) {
        int fi = em->calls[i].fn;
        if (fi < 0 || fi >= em->nfns || !em->fns[fi].defined) {
            return -1;
        }
        patch_rel32(em, em->calls[i].at, em->fns[fi].entry);
    }
    for (i = 0; i < em->nfns; i++) {
        if (!em->fns[i].defined) {
            return -1;
        }
    }
    return 0;
}

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len) {
    CcLexer lx;
    CcParser p;
    const char magic[] = "CXC_A2";
    size_t i;
    size_t jmp_entry;
    size_t entry_at;
    size_t expanded_len = 0;
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];
    static char expanded[CC_SRC_MAX];

    memset(&em, 0, sizeof(em));
    em.code_cap = CC_CODE_MAX;
    em.data_cap = CC_DATA_MAX;
    em.code = code_buf;
    em.data = data_buf;
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
    em.failed = 0;
    cc_reloc_reset();
    cc_link_reset();

    if (cc_preprocess(src, len, expanded, sizeof(expanded), &expanded_len) !=
        0) {
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
    if (lx.tok == TOK_IDENT && strcmp(lx.text, "CXC_A2") == 0) {
        cc_lex_next(&lx);
    }

    p.lx = &lx;
    p.em = &em;

    /* jmp entry; fn/struct/global/extern defs; linked TUs; CRT _start */
    jmp_entry = emit_jmp32(&em);
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
    if (resolve_calls(&em) != 0) {
        return -1;
    }
    entry_at = em.code_len;
    patch_rel32(&em, jmp_entry, entry_at);
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
            patch_rel32(&em, call_at, em.fns[main_i].entry);
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
                if (lx.tok == TOK_KW_FN || lx.tok == TOK_KW_STRUCT ||
                    lx.tok == TOK_KW_INT || lx.tok == TOK_KW_ENUM ||
                    lx.tok == TOK_KW_EXTERN) {
                    em.failed = 1;
                    break;
                }
                parse_stmt(&p);
            }
        }
    }
    if (em.failed) {
        return -1;
    }
    return cc_build_elf(&em, out_elf, out_len);
}
