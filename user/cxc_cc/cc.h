/* CXC mini-cc (A2): self ET_REL obj + #lib SysV + DMSG/DHINT. */
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
    /* M292: capacity wall lift (ident64 / src64K / code256K / …). */
    CC_SRC_MAX = 65536,
    CC_CODE_MAX = 262144,
    CC_LOCAL_MAX = 128,
    CC_IDENT_MAX = 64,
    CC_PATH_MAX = 48,
    CC_LIT_MAX = 32, /* M291: was 8; long write must not silent-truncate */
    CC_STR_MAX = 64,
    CC_LOOP_MAX = 8,
    CC_BREAK_MAX = 8,
    CC_FN_MAX = 128,
    CC_FN_ARGS = 8,
    CC_STRUCT_MAX = 32,
    CC_STRUCT_FIELDS = 16,
    CC_GLOBAL_MAX = 64,
    CC_ENUM_MAX = 16,
    CC_CALL_MAX = 64,
    CC_LINK_MAX = 96, /* M252: deep musl closure headroom */
    CC_EXPORT_MAX = 256,
    CC_GOT_MAX = 64,
    CC_TEXT_SEC_MAX = 32,
    CC_RELA_SEC_MAX = 40,
    CC_UND_PEND_MAX = 256,
    CC_TYPEDEF_MAX = 8,
    CC_DATA_MAX = 16384, /* raised with deep plants / RO split */
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
    TOK_KW_TYPEDEF,
    TOK_KW_VOID,
    TOK_KW_CHAR,
    TOK_KW_LONG,
    TOK_KW_CONST,
    TOK_KW_UNSIGNED,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_SEMI,
    TOK_COMMA,
    TOK_DOT,
    TOK_ARROW,
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
    TOK_PERCENTEQ, /* M289: %= */
    TOK_PLUSPLUS,
    TOK_MINUSMINUS,
    TOK_LSHIFT,
    TOK_RSHIFT,
    TOK_CARET,  /* M289: ^ */
    TOK_TILDE,  /* M289: ~ */
    TOK_UNKNOWN /* M287: unsupported char — hard fail, never silent EOF */
};

typedef struct CcLexer {
    const char* src;
    size_t len;
    size_t pos;
    int tok;
    long long number; /* M289: 64-bit hex / char literals */
    char text[CC_IDENT_MAX + 1];
    char str[CC_STR_MAX + 1];
    size_t str_len;
    int line;      /* M296: 1-based line of current token */
    int col;       /* M296: 1-based column of current token */
    int scan_line; /* cursor line at pos */
    int scan_col;  /* cursor column at pos */
} CcLexer;

typedef struct CcLocal {
    char name[CC_IDENT_MAX + 1];
    int slot;
    int count;
    int stype; /* -1, or struct type index */
    int is_char;  /* M132: word slot + truncate on store */
    int is_const; /* M132: reject assignment after init */
    int is_ptr;   /* M140: pointer to stype (Pt* p) */
    int elem_w;   /* M288: pointee/array element width in bytes (1/8) */
} CcLocal;

/* M288: expression result type carried through parse_* for scaling. */
typedef struct CcType {
    int is_ptr;
    int elem_w; /* pointee width when is_ptr; else 0 */
    int is_char;
} CcType;

typedef struct CcGlobal {
    char name[CC_IDENT_MAX + 1];
    size_t data_off;
    int count;
    int is_const;
} CcGlobal;

typedef struct CcLoop {
    size_t cont_at;
    size_t brk_at[CC_BREAK_MAX];
    int nbrk;
    size_t cont_patch[CC_BREAK_MAX];
    int ncont;
    int is_switch;
    int asm_cont_id; /* M299: known .LccN for continue (0 = forward patch) */
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

typedef struct CcExport {
    char name[CC_IDENT_MAX + 1];
    size_t entry;
} CcExport;

typedef struct CcGot {
    char name[CC_IDENT_MAX + 1];
    size_t slot; /* code offset of 8-byte GOT entry */
} CcGot;

typedef struct CcTypedef {
    char name[CC_IDENT_MAX + 1];
} CcTypedef;

typedef struct CcStruct {
    char name[CC_IDENT_MAX + 1];
    char fields[CC_STRUCT_FIELDS][CC_IDENT_MAX + 1];
    int fsizes[CC_STRUCT_FIELDS]; /* char→1, else→8 */
    int foffs[CC_STRUCT_FIELDS];  /* M248: compact offs from fsizes */
    int nfields;
    int ssize; /* M248: packed layout + natural align */
} CcStruct;

typedef struct CcEnumConst {
    char name[CC_IDENT_MAX + 1];
    long value;
} CcEnumConst;

typedef struct CcEmit {
    unsigned char* code;
    size_t code_len;
    size_t code_cap;
    unsigned char* data; /* final [RO|RW] after seal, or RW staging */
    size_t data_len;
    size_t data_cap;
    unsigned char* ro; /* M245: staging for .rodata (any order vs .data) */
    size_t ro_len;
    size_t ro_cap;
    size_t bss_len; /* M245: NOBITS after RW (not in file) */
    size_t rw_len;  /* staging RW length before seal into data[] */
    int data_sec;   /* 0 none, 1 ro, 2 rw, 3 bss */
    int sealed;     /* 1: data[] = ro|rw concatenated, ro_len final */
    CcLocal locals[CC_LOCAL_MAX];
    int nlocals;
    int next_slot;
    int failed;
    int fail_code; /* M109 debug: link/parse reason for DMSG */
    int fail_line; /* M296: first-fail line (0 = unset) */
    int fail_col;  /* M296: first-fail column */
    int asm_mode;  /* M296: emit GAS text instead of/alongside bytes */
    char* asm_buf;
    size_t asm_len;
    size_t asm_cap;
    int asm_label_seq; /* M297: .LccN generator */
    /* Pending forward jcc/jmp: code_at is reloc imm offset; label defined at patch. */
    size_t asm_fix_at[64];
    int asm_fix_id[64];
    int nasm_fix;
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
    CcExport exports[CC_EXPORT_MAX];
    int nexports;
    CcGot gots[CC_GOT_MAX];
    int ngots;
    CcTypedef typedefs[CC_TYPEDEF_MAX];
    int ntypedefs;
    int obj_mode; /* 1: emit ET_REL SysV fns (no CRT) */
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
int cc_struct_field_size(CcEmit* em, int stype, const char* field);
void cc_emit_lea_local_bytes(CcEmit* em, int slot, int byte_off);
void cc_emit_load_local_bytes(CcEmit* em, int slot, int byte_off, int width);
void cc_emit_store_local_bytes(CcEmit* em, int slot, int byte_off, int width);
void cc_emit_load_indirect_width(CcEmit* em, int width);
void cc_emit_store_indirect_width(CcEmit* em, int width);
void cc_emit_setup_fs_tcb(CcEmit* em); /* M252: ARCH_SET_FS canary TCB */
int cc_global_find(CcEmit* em, const char* name);
int cc_global_define(CcEmit* em, const char* name, unsigned long long init);
int cc_enum_find(CcEmit* em, const char* name);
int cc_enum_define(CcEmit* em, const char* name, long value);
int cc_typedef_find(CcEmit* em, const char* name);
int cc_typedef_define(CcEmit* em, const char* name);

void cc_emit_asm_puts(CcEmit* em, const char* s); /* M296 asm_mode */
void cc_emit_asm_fn_label(CcEmit* em, const char* name);
int cc_asm_new_label(CcEmit* em);              /* M297 */
void cc_asm_def_label(CcEmit* em, int id);     /* emit .LccN: */
void cc_asm_jmp_label(CcEmit* em, int id);     /* jmp .LccN (asm only) */
void cc_asm_je_label(CcEmit* em, int id);
void cc_asm_jne_label(CcEmit* em, int id);
void cc_emit_asm_test_rax(CcEmit* em); /* test %eax,%eax */
void cc_emit_asm_cmp_rax_imm(CcEmit* em, long imm); /* M299: cmp $imm, %rax */
void cc_emit_byte(CcEmit* em, unsigned char b);
void cc_emit_bytes(CcEmit* em, const void* p, size_t n);
void cc_emit_u32(CcEmit* em, unsigned v);
void cc_emit_i32(CcEmit* em, int v);
size_t cc_data_push(CcEmit* em, const void* p, size_t n);
size_t cc_ro_push(CcEmit* em, const void* p, size_t n);
size_t cc_bss_bump(CcEmit* em, size_t n);
void cc_data_set_sec(CcEmit* em, int sec); /* 1=ro 2=rw 3=bss */
size_t cc_data_cur_off(CcEmit* em); /* current label off (sec-tagged) */
size_t cc_data_resolve_off(const CcEmit* em, size_t tagged);
int cc_data_seal(CcEmit* em); /* concat ro|rw → data[]; fix reloc/globals */

void cc_emit_mov_eax_imm(CcEmit* em, unsigned imm);
void cc_emit_mov_rax_imm64(CcEmit* em, unsigned long long imm);
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
void cc_emit_index_load_n(CcEmit* em, int base_slot, int count, int elem_w);
void cc_emit_index_store_n(CcEmit* em, int base_slot, int count, int elem_w);
void cc_emit_index_lea_n(CcEmit* em, int base_slot, int count, int elem_w);
void cc_emit_ptr_index_load(CcEmit* em, int elem_w);
void cc_emit_ptr_index_store(CcEmit* em, int elem_w);
void cc_emit_ptr_add_scaled(CcEmit* em, int elem_w);
void cc_emit_load_indirect(CcEmit* em);
void cc_emit_store_indirect(CcEmit* em);
void cc_emit_lea_data(CcEmit* em, size_t data_off);
void cc_emit_mov_data(CcEmit* em, size_t data_off);
void cc_emit_load_global(CcEmit* em, size_t data_off);
void cc_emit_store_global(CcEmit* em, size_t data_off);
void cc_emit_prologue(CcEmit* em);
void cc_emit_fn_prologue(CcEmit* em, int arity);
void cc_emit_fn_prologue_sysv(CcEmit* em, int arity);
void cc_emit_leave_ret(CcEmit* em);
size_t cc_emit_call_rel32(CcEmit* em);
void cc_emit_call_name(CcEmit* em, const char* name); /* M297 asm call */
void cc_emit_write_lit2(CcEmit* em, const char* lit);
void cc_emit_write_pass(CcEmit* em);
void cc_emit_print_rax(CcEmit* em);
void cc_emit_input_slot(CcEmit* em, int slot);
void cc_emit_exit_rax(CcEmit* em);
void cc_reloc_reset(void);
int cc_reloc_count(void);
int cc_reloc_set_data(int i, size_t data_off);
void cc_reloc_add(size_t code_off, size_t data_off);
void cc_reloc_add_abs(size_t code_off, size_t data_off);
int cc_build_elf(CcEmit* em, unsigned char** out, size_t* out_len);
int cc_build_rel_elf(CcEmit* em, unsigned char** out, size_t* out_len);

int cc_preprocess(const char* in, size_t in_len, char* out, size_t out_cap,
                  size_t* out_len);
int cc_prep_last_fail_line(void); /* M297: location of last prep failure */
int cc_prep_last_fail_col(void);
void cc_note_fail(int code, int line, int col); /* publish DLOC snapshot */
void cc_link_reset(void);
int cc_link_count(void);
const char* cc_link_at(int i);
int cc_read_source(const char* path, char* buf, size_t cap, size_t* out_n);
void cc_lib_reset(void);
int cc_lib_count(void);
const char* cc_lib_at(int i);
int cc_import_elf(CcEmit* em, const char* path);
void cc_und_reset(void);
int cc_und_resolve(CcEmit* em);
int cc_last_fail_code(void);
int cc_last_fail_line(void); /* M296 DLOC */
int cc_last_fail_col(void);

int cc_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len);
int cc_compile_obj(const char* src, size_t len, unsigned char** out_elf,
                   size_t* out_len);
/* M296: compile to GAS text (asm_mode); out is NUL-terminated asm. */
int cc_compile_asm(const char* src, size_t len, char** out_asm,
                   size_t* out_len);

/* M127: freestanding GAS subset → SysV ET_REL. */
int as_compile(const char* src, size_t len, unsigned char** out_elf,
               size_t* out_len);
int as_last_fail_code(void);

/* M129: mini-ld — ET_REL paths + CRT → ET_EXEC. */
int ld_link(const char* const* paths, int npaths, unsigned char** out_elf,
            size_t* out_len);
int ld_last_fail_code(void);
