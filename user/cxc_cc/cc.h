/* CXC mini-cc (A2): statements, expressions, if/while. Freestanding. */
#pragma once

#include "mini_libc/stdlib.h"
#include "mini_libc/string.h"
#include "mini_libc/unistd.h"

enum {
    CC_SRC_MAX = 4096,
    CC_CODE_MAX = 12288,
    CC_DATA_MAX = 2048,
    CC_LOCAL_MAX = 16,
    CC_IDENT_MAX = 8,
    CC_LIT_MAX = 8,
    CC_LOAD_VA = 0x400000
};

enum CcTok {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_LIT, /* write operand: HI */
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_RETURN,
    TOK_KW_WRITE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMI,
    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE
};

typedef struct CcLexer {
    const char* src;
    size_t len;
    size_t pos;
    int tok;
    long number;
    char text[CC_IDENT_MAX + 1];
} CcLexer;

typedef struct CcLocal {
    char name[CC_IDENT_MAX + 1];
    int slot; /* 0..CC_LOCAL_MAX-1 */
} CcLocal;

typedef struct CcEmit {
    unsigned char* code;
    size_t code_len;
    size_t code_cap;
    unsigned char* data;
    size_t data_len;
    size_t data_cap;
    CcLocal locals[CC_LOCAL_MAX];
    int nlocals;
    int failed;
} CcEmit;

void cc_lex_init(CcLexer* lx, const char* src, size_t len);
void cc_lex_next(CcLexer* lx);

int cc_local_slot(CcEmit* em, const char* name, int create);
void cc_emit_byte(CcEmit* em, unsigned char b);
void cc_emit_bytes(CcEmit* em, const void* p, size_t n);
void cc_emit_u32(CcEmit* em, unsigned v);
void cc_emit_i32(CcEmit* em, int v);
size_t cc_data_push(CcEmit* em, const void* p, size_t n);

void cc_emit_mov_eax_imm(CcEmit* em, unsigned imm);
void cc_emit_mov_edi_eax(CcEmit* em);
void cc_emit_mov_edi_imm(CcEmit* em, unsigned imm);
void cc_emit_int80(CcEmit* em);
void cc_emit_push_rax(CcEmit* em);
void cc_emit_pop_rcx(CcEmit* em);
void cc_emit_store_local(CcEmit* em, int slot);
void cc_emit_load_local(CcEmit* em, int slot);
void cc_emit_prologue(CcEmit* em);
void cc_emit_write_lit2(CcEmit* em, const char* lit);
void cc_emit_write_pass(CcEmit* em);
void cc_emit_exit_rax(CcEmit* em);
void cc_reloc_reset(void);
int cc_build_elf(CcEmit* em, unsigned char** out, size_t* out_len);

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len);
