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
    patch_rel32(p->em, call_at, p->em->fns[fi].entry);
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
    if (p->lx->tok == TOK_IDENT) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
        if (p->lx->tok == TOK_LPAREN) {
            parse_call(p, name);
            return;
        }
        idx = cc_local_find(p->em, name);
        if (idx < 0) {
            fail(p);
            return;
        }
        if (accept(p, TOK_LBRACK)) {
            if (p->em->locals[idx].count < 1) {
                fail(p);
                return;
            }
            parse_expr(p);
            expect(p, TOK_RBRACK);
            cc_emit_index_load(p->em, p->em->locals[idx].slot);
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
    if (p->lx->tok != TOK_IDENT) {
        fail(p);
        return;
    }
    memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
    cc_lex_next(p->lx);
    idx = cc_local_find(p->em, name);
    if (idx < 0) {
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
    parse_primary(p);
}

static void parse_mul(CcParser* p) {
    parse_unary(p);
    while (p->lx->tok == TOK_STAR || p->lx->tok == TOK_SLASH) {
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
            cc_emit_bytes(p->em, "\x48\x89\xC1", 3);
            cc_emit_byte(p->em, 0x58);
            cc_emit_bytes(p->em, "\x48\x99", 2);
            cc_emit_bytes(p->em, "\x48\xF7\xF9", 3);
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

static void parse_cmp(CcParser* p) {
    parse_add(p);
    while (p->lx->tok == TOK_EQ || p->lx->tok == TOK_NE || p->lx->tok == TOK_LT ||
           p->lx->tok == TOK_LE || p->lx->tok == TOK_GT || p->lx->tok == TOK_GE) {
        int op = p->lx->tok;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_add(p);
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

static void parse_expr(CcParser* p) {
    parse_or(p);
}

static void parse_stmt(CcParser* p);

static void parse_block(CcParser* p) {
    expect(p, TOK_LBRACE);
    while (!p->em->failed && p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
        parse_stmt(p);
    }
    expect(p, TOK_RBRACE);
}

static int loop_push(CcEmit* em, size_t cont_at) {
    CcLoop* lp;
    if (em->nloops >= CC_LOOP_MAX) {
        em->failed = 1;
        return -1;
    }
    lp = &em->loops[em->nloops++];
    lp->cont_at = cont_at;
    lp->nbrk = 0;
    return 0;
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
        if (loop_push(p->em, loop) != 0) {
            return;
        }
        parse_expr(p);
        expect(p, TOK_RPAREN);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_stmt(p);
        back = emit_jmp32(p->em);
        patch_rel32(p->em, back, loop);
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
        size_t at;
        expect(p, TOK_SEMI);
        if (p->em->nloops <= 0) {
            fail(p);
            return;
        }
        at = emit_jmp32(p->em);
        patch_rel32(p->em, at, p->em->loops[p->em->nloops - 1].cont_at);
        return;
    }
    if (accept(p, TOK_KW_WRITE)) {
        char lit[CC_LIT_MAX + 1];
        size_t n = 0;
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
    if (p->lx->tok == TOK_IDENT) {
        char name[CC_IDENT_MAX + 1];
        int idx;
        int slot;
        int st;
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
            if (idx < 0 || p->em->locals[idx].count < 1) {
                fail(p);
                return;
            }
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

static void parse_function(CcParser* p) {
    char name[CC_IDENT_MAX + 1];
    CcFn* fn;
    int arity = 0;
    size_t n;

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
    fn->entry = p->em->code_len;
    fn->arity = 0;
    p->em->nfns++;

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

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len) {
    CcLexer lx;
    CcParser p;
    const char magic[] = "CXC_A2";
    size_t i;
    size_t jmp_entry;
    size_t entry_at;
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];

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
    em.failed = 0;
    cc_reloc_reset();

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

    /* jmp entry; fn/struct defs; entry: prologue + top-level */
    jmp_entry = emit_jmp32(&em);
    {
        int guard = 0;
        while (!em.failed &&
               (lx.tok == TOK_KW_FN || lx.tok == TOK_KW_STRUCT)) {
            if (++guard > 32) {
                em.failed = 1;
                break;
            }
            if (lx.tok == TOK_KW_FN) {
                parse_function(&p);
            } else {
                parse_struct_def(&p);
            }
        }
    }
    entry_at = em.code_len;
    patch_rel32(&em, jmp_entry, entry_at);
    clear_locals(&em);
    cc_emit_prologue(&em);
    {
        int guard = 0;
        while (!em.failed && lx.tok != TOK_EOF) {
            if (++guard > 512) {
                em.failed = 1;
                break;
            }
            if (lx.tok == TOK_KW_FN || lx.tok == TOK_KW_STRUCT) {
                em.failed = 1;
                break;
            }
            parse_stmt(&p);
        }
    }
    if (em.failed) {
        return -1;
    }
    return cc_build_elf(&em, out_elf, out_len);
}
