/* CXC mini-cc (A2): CRT/enum + #link + #lib ET_REL import SysV. */
#pragma once

#ifdef CC_HOST
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#else
#include "mini_libc/stdlib.h"
#include "mini_libc/string.h"
#include "mini_libc/unistd.h"
#endif

enum {
    CC_SRC_MAX = 8192,
    CC_CODE_MAX = 16384,
    CC_DATA_MAX = 4096,
    CC_LOCAL_MAX = 32,
    CC_IDENT_MAX = 8,
    CC_LIT_MAX = 8,
    CC_STR_MAX = 16,
    CC_LOOP_MAX = 4,
    CC_BREAK_MAX = 8,
    CC_FN_MAX = 8,
    CC_FN_ARGS = 4,
    CC_STRUCT_MAX = 4,
    CC_STRUCT_FIELDS = 4,
    CC_GLOBAL_MAX = 8,
    CC_ENUM_MAX = 16,
    CC_CALL_MAX = 32,
    CC_LINK_MAX = 4,
    CC_LOAD_VA = 0x400000
};

enum CcTok {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_LIT,
    TOK_STR,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_DO,
    TOK_KW_RETURN,
    TOK_KW_WRITE,
    TOK_KW_PRINT,
    TOK_KW_INPUT,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_FN,
    TOK_KW_STRUCT,
    TOK_KW_FOR,
    TOK_KW_INT,
    TOK_KW_SWITCH,
    TOK_KW_CASE,
    TOK_KW_DEFAULT,
    TOK_KW_ENUM,
    TOK_KW_SIZEOF,
    TOK_KW_EXTERN,
    TOK_KW_IMPORT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_QUESTION,
    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_BANG,
    TOK_AMP,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_AMPAMP,
    TOK_BARBAR,
    TOK_PLUSEQ,
    TOK_MINUSEQ,
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_LSHIFT,
    TOK_RSHIFT
};

typedef struct CcLexer {
    const char* src;
    size_t len;
    size_t pos;
    int tok;
    long number;
    char text[CC_IDENT_MAX + 1];
    char str[CC_STR_MAX + 1];
    size_t str_len;
} CcLexer;

typedef struct CcLocal {
    char name[CC_IDENT_MAX + 1];
    int slot;
    int count;
    int stype; /* -1, or struct type index */
} CcLocal;

typedef struct CcGlobal {
    char name[CC_IDENT_MAX + 1];
    size_t data_off;
    int count;
} CcGlobal;

typedef struct CcLoop {
    size_t cont_at;
    size_t brk_at[CC_BREAK_MAX];
    int nbrk;
    size_t cont_patch[CC_BREAK_MAX];
    int ncont;
    int is_switch;
} CcLoop;

typedef struct CcFn {
    char name[CC_IDENT_MAX + 1];
    int arity;
    size_t entry;
    int defined;
    int sysv;
} CcFn;

typedef struct CcCallFix {
    size_t at;
    int fn;
} CcCallFix;

typedef struct CcStruct {
    char name[CC_IDENT_MAX + 1];
    char fields[CC_STRUCT_FIELDS][CC_IDENT_MAX + 1];
    int nfields;
} CcStruct;

typedef struct CcEnumConst {
    char name[CC_IDENT_MAX + 1];
    long value;
} CcEnumConst;

typedef struct CcEmit {
    unsigned char* code;
    size_t code_len;
    size_t code_cap;
    unsigned char* data;
    size_t data_len;
    size_t data_cap;
    CcLocal locals[CC_LOCAL_MAX];
    int nlocals;
    int next_slot;
    int failed;
    CcLoop loops[CC_LOOP_MAX];
    int nloops;
    CcFn fns[CC_FN_MAX];
    int nfns;
    int in_fn;
    CcStruct structs[CC_STRUCT_MAX];
    int nstructs;
    CcGlobal globals[CC_GLOBAL_MAX];
    int nglobals;
    CcEnumConst enums[CC_ENUM_MAX];
    int nenums;
    CcCallFix calls[CC_CALL_MAX];
    int ncalls;
} CcEmit;

void cc_lex_init(CcLexer* lx, const char* src, size_t len);
void cc_lex_next(CcLexer* lx);

int cc_local_find(CcEmit* em, const char* name);
int cc_local_define(CcEmit* em, const char* name, int count);
int cc_local_define_st(CcEmit* em, const char* name, int count, int stype);
int cc_local_slot(CcEmit* em, const char* name, int create);
int cc_fn_find(CcEmit* em, const char* name);
int cc_struct_find(CcEmit* em, const char* name);
int cc_struct_field(CcEmit* em, int stype, const char* field);
int cc_global_find(CcEmit* em, const char* name);
int cc_global_define(CcEmit* em, const char* name, unsigned long long init);
int cc_enum_find(CcEmit* em, const char* name);
int cc_enum_define(CcEmit* em, const char* name, long value);

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
void cc_emit_lea_local(CcEmit* em, int slot);
void cc_emit_index_load(CcEmit* em, int base_slot);
void cc_emit_index_store(CcEmit* em, int base_slot);
void cc_emit_index_lea(CcEmit* em, int base_slot);
void cc_emit_load_indirect(CcEmit* em);
void cc_emit_store_indirect(CcEmit* em);
void cc_emit_lea_data(CcEmit* em, size_t data_off);
void cc_emit_load_global(CcEmit* em, size_t data_off);
void cc_emit_store_global(CcEmit* em, size_t data_off);
void cc_emit_prologue(CcEmit* em);
void cc_emit_fn_prologue(CcEmit* em, int arity);
void cc_emit_leave_ret(CcEmit* em);
size_t cc_emit_call_rel32(CcEmit* em);
void cc_emit_write_lit2(CcEmit* em, const char* lit);
void cc_emit_write_pass(CcEmit* em);
void cc_emit_print_rax(CcEmit* em);
void cc_emit_input_slot(CcEmit* em, int slot);
void cc_emit_exit_rax(CcEmit* em);
void cc_reloc_reset(void);
int cc_build_elf(CcEmit* em, unsigned char** out, size_t* out_len);

int cc_preprocess(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_len);
void cc_link_reset(void);
int cc_link_count(void);
const char* cc_link_at(int i);
int cc_read_source(const char* path, char* buf, size_t cap, size_t* out_n);
void cc_lib_reset(void);
int cc_lib_count(void);
const char* cc_lib_at(int i);
int cc_import_elf(CcEmit* em, const char* path);

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len);
