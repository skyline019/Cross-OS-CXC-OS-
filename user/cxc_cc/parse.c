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

static void parse_primary(CcParser* p) {
    if (p->lx->tok == TOK_NUMBER) {
        cc_emit_mov_eax_imm(p->em, (unsigned)p->lx->number);
        cc_lex_next(p->lx);
        return;
    }
    if (p->lx->tok == TOK_IDENT) {
        int slot = cc_local_slot(p->em, p->lx->text, 0);
        if (slot < 0) {
            fail(p);
            return;
        }
        cc_emit_load_local(p->em, slot);
        cc_lex_next(p->lx);
        return;
    }
    if (accept(p, TOK_LPAREN)) {
        parse_expr(p);
        expect(p, TOK_RPAREN);
        return;
    }
    fail(p);
}

static void parse_mul(CcParser* p) {
    parse_primary(p);
    while (p->lx->tok == TOK_STAR || p->lx->tok == TOK_SLASH) {
        int op = p->lx->tok;
        cc_lex_next(p->lx);
        cc_emit_push_rax(p->em);
        parse_primary(p);
        if (op == TOK_STAR) {
            cc_emit_bytes(p->em, "\x48\x89\xC2", 3); /* mov rdx, rax */
            cc_emit_pop_rcx(p->em);
            cc_emit_bytes(p->em, "\x48\x89\xC8", 3); /* mov rax, rcx */
            cc_emit_bytes(p->em, "\x48\x0F\xAF\xC2", 4); /* imul rax, rdx */
        } else {
            cc_emit_bytes(p->em, "\x48\x89\xC1", 3); /* mov rcx, rax divisor */
            cc_emit_byte(p->em, 0x58);                /* pop rax = dividend */
            cc_emit_bytes(p->em, "\x48\x99", 2);       /* cqo */
            cc_emit_bytes(p->em, "\x48\xF7\xF9", 3);   /* idiv rcx */
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
        cc_emit_bytes(p->em, "\x48\x89\xC2", 3); /* mov rdx, rax */
        cc_emit_pop_rcx(p->em);
        cc_emit_bytes(p->em, "\x48\x89\xC8", 3); /* mov rax, rcx */
        if (op == TOK_PLUS) {
            cc_emit_bytes(p->em, "\x48\x01\xD0", 3); /* add rax, rdx */
        } else {
            cc_emit_bytes(p->em, "\x48\x29\xD0", 3); /* sub rax, rdx */
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
        cc_emit_bytes(p->em, "\x48\x39\xD1", 3); /* cmp rcx, rdx */
        /* setcc al; movzx eax, al */
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
        cc_emit_bytes(p->em, "\x0F\xB6\xC0", 3); /* movzx eax, al */
    }
}

static void parse_expr(CcParser* p) {
    parse_cmp(p);
}

static void parse_stmt(CcParser* p);

static size_t emit_je32(CcEmit* em) {
    size_t at;
    cc_emit_bytes(em, "\x0F\x84", 2);
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

static void parse_block(CcParser* p) {
    expect(p, TOK_LBRACE);
    while (!p->em->failed && p->lx->tok != TOK_RBRACE && p->lx->tok != TOK_EOF) {
        parse_stmt(p);
    }
    expect(p, TOK_RBRACE);
}

static void parse_stmt(CcParser* p) {
    if (p->em->failed) {
        return;
    }
    if (p->lx->tok == TOK_LBRACE) {
        parse_block(p);
        return;
    }
    if (accept(p, TOK_KW_IF)) {
        size_t je_at;
        size_t jmp_end = 0;
        int has_else;
        expect(p, TOK_LPAREN);
        parse_expr(p);
        expect(p, TOK_RPAREN);
        /* test eax,eax; je else */
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
        expect(p, TOK_LPAREN);
        parse_expr(p);
        expect(p, TOK_RPAREN);
        cc_emit_bytes(p->em, "\x85\xC0", 2);
        je_at = emit_je32(p->em);
        parse_stmt(p);
        {
            size_t back = emit_jmp32(p->em);
            patch_rel32(p->em, back, loop);
        }
        patch_rel32(p->em, je_at, p->em->code_len);
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
    if (accept(p, TOK_KW_RETURN)) {
        parse_expr(p);
        expect(p, TOK_SEMI);
        /* Preserve status across write_pass (syscall clobbers eax). */
        cc_emit_bytes(p->em, "\x89\xC3", 2); /* mov ebx, eax */
        cc_emit_write_pass(p->em);
        cc_emit_exit_rax(p->em);
        return;
    }
    if (p->lx->tok == TOK_IDENT) {
        char name[CC_IDENT_MAX + 1];
        int slot;
        memcpy(name, p->lx->text, CC_IDENT_MAX + 1);
        cc_lex_next(p->lx);
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

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len) {
    CcLexer lx;
    CcParser p;
    const char magic[] = "CXC_A2";
    size_t i;
    /* Keep emit state + code/data off the  user stack (corruption was hanging
     * strcmp in cc_local_slot after deep write() frames). */
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
    /* skip magic tokens: CXC_A2 may lex as IDENT CXC_A2 */
    if (lx.tok == TOK_IDENT && strcmp(lx.text, "CXC_A2") == 0) {
        cc_lex_next(&lx);
    }

    p.lx = &lx;
    p.em = &em;
    cc_emit_prologue(&em);
    {
        int guard = 0;
        while (!em.failed && lx.tok != TOK_EOF) {
            if (++guard > 256) {
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
