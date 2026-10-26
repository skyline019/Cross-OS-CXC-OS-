#include "cxc_cc/cc.h"

enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_EXIT = 60
};

/* Shared by ET_EXEC / ET_REL emitters (not re-entrant). */
static unsigned char g_elf_buf[CC_CODE_MAX + CC_DATA_MAX + 256];

static void patch_rel32_local(CcEmit* em, size_t at, size_t target) {
    int rel = (int)(target - (at + 4));
    em->code[at] = (unsigned char)(rel);
    em->code[at + 1] = (unsigned char)(rel >> 8);
    em->code[at + 2] = (unsigned char)(rel >> 16);
    em->code[at + 3] = (unsigned char)(rel >> 24);
}

int cc_local_find(CcEmit* em, const char* name) {
    int i;
    for (i = 0; i < em->nlocals; i++) {
        if (strcmp(em->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_local_define_st(CcEmit* em, const char* name, int count, int stype) {
    CcLocal* loc;
    size_t n = 0;
    if (count < 1 || em->nlocals >= CC_LOCAL_MAX ||
        em->next_slot + count > CC_LOCAL_MAX) {
        return -1;
    }
    if (cc_local_find(em, name) >= 0) {
        return -1;
    }
    loc = &em->locals[em->nlocals];
    while (n < CC_IDENT_MAX && name[n] != '\0') {
        loc->name[n] = name[n];
        n++;
    }
    loc->name[n] = 0;
    loc->slot = em->next_slot;
    loc->count = count;
    loc->stype = stype;
    em->next_slot += count;
    em->nlocals++;
    return loc->slot;
}

int cc_local_define(CcEmit* em, const char* name, int count) {
    return cc_local_define_st(em, name, count, -1);
}

int cc_local_slot(CcEmit* em, const char* name, int create) {
    int idx = cc_local_find(em, name);
    if (idx >= 0) {
        if (em->locals[idx].count != 1) {
            return -1;
        }
        return em->locals[idx].slot;
    }
    if (!create) {
        return -1;
    }
    return cc_local_define(em, name, 1);
}

int cc_fn_find(CcEmit* em, const char* name) {
    int i;
    for (i = 0; i < em->nfns; i++) {
        if (strcmp(em->fns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_struct_find(CcEmit* em, const char* name) {
    int i;
    for (i = 0; i < em->nstructs; i++) {
        if (strcmp(em->structs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_struct_field(CcEmit* em, int stype, const char* field) {
    int i;
    if (stype < 0 || stype >= em->nstructs) {
        return -1;
    }
    for (i = 0; i < em->structs[stype].nfields; i++) {
        if (strcmp(em->structs[stype].fields[i], field) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_global_find(CcEmit* em, const char* name) {
    int i;
    for (i = 0; i < em->nglobals; i++) {
        if (strcmp(em->globals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_enum_find(CcEmit* em, const char* name) {
    int i;
    for (i = 0; i < em->nenums; i++) {
        if (strcmp(em->enums[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_enum_define(CcEmit* em, const char* name, long value) {
    CcEnumConst* e;
    size_t n = 0;
    if (em->nenums >= CC_ENUM_MAX || cc_enum_find(em, name) >= 0 ||
        cc_global_find(em, name) >= 0 || cc_typedef_find(em, name) >= 0) {
        return -1;
    }
    e = &em->enums[em->nenums];
    while (n < CC_IDENT_MAX && name[n] != '\0') {
        e->name[n] = name[n];
        n++;
    }
    e->name[n] = 0;
    e->value = value;
    em->nenums++;
    return em->nenums - 1;
}

int cc_typedef_find(CcEmit* em, const char* name) {
    int i;
    for (i = 0; i < em->ntypedefs; i++) {
        if (strcmp(em->typedefs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cc_typedef_define(CcEmit* em, const char* name) {
    CcTypedef* t;
    size_t n = 0;
    if (em->ntypedefs >= CC_TYPEDEF_MAX || cc_typedef_find(em, name) >= 0 ||
        cc_global_find(em, name) >= 0 || cc_enum_find(em, name) >= 0 ||
        cc_fn_find(em, name) >= 0) {
        return -1;
    }
    t = &em->typedefs[em->ntypedefs];
    while (n < CC_IDENT_MAX && name[n] != '\0') {
        t->name[n] = name[n];
        n++;
    }
    t->name[n] = 0;
    em->ntypedefs++;
    return em->ntypedefs - 1;
}

int cc_global_define(CcEmit* em, const char* name, unsigned long long init) {
    CcGlobal* g;
    size_t n = 0;
    unsigned char words[8];
    int i;
    if (em->nglobals >= CC_GLOBAL_MAX || cc_global_find(em, name) >= 0 ||
        cc_local_find(em, name) >= 0 || cc_typedef_find(em, name) >= 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        words[i] = (unsigned char)(init >> (8 * i));
    }
    g = &em->globals[em->nglobals];
    while (n < CC_IDENT_MAX && name[n] != '\0') {
        g->name[n] = name[n];
        n++;
    }
    g->name[n] = 0;
    g->data_off = cc_data_push(em, words, 8);
    g->count = 1;
    em->nglobals++;
    return em->nglobals - 1;
}

void cc_emit_byte(CcEmit* em, unsigned char b) {
    if (em->failed || em->code_len >= em->code_cap) {
        em->failed = 1;
        return;
    }
    em->code[em->code_len++] = b;
}

void cc_emit_bytes(CcEmit* em, const void* p, size_t n) {
    size_t i;
    const unsigned char* b = (const unsigned char*)p;
    for (i = 0; i < n; i++) {
        cc_emit_byte(em, b[i]);
    }
}

void cc_emit_u32(CcEmit* em, unsigned v) {
    cc_emit_byte(em, (unsigned char)(v & 0xff));
    cc_emit_byte(em, (unsigned char)((v >> 8) & 0xff));
    cc_emit_byte(em, (unsigned char)((v >> 16) & 0xff));
    cc_emit_byte(em, (unsigned char)((v >> 24) & 0xff));
}

void cc_emit_i32(CcEmit* em, int v) {
    cc_emit_u32(em, (unsigned)v);
}

size_t cc_data_push(CcEmit* em, const void* p, size_t n) {
    size_t at;
    if (em->failed || em->data_len + n > em->data_cap) {
        em->failed = 1;
        return 0;
    }
    at = em->data_len;
    memcpy(em->data + em->data_len, p, n);
    em->data_len += n;
    return at;
}

void cc_emit_mov_eax_imm(CcEmit* em, unsigned imm) {
    cc_emit_byte(em, 0xB8);
    cc_emit_u32(em, imm);
}

void cc_emit_mov_edi_eax(CcEmit* em) {
    cc_emit_bytes(em, "\x89\xC7", 2);
}

void cc_emit_mov_edi_imm(CcEmit* em, unsigned imm) {
    cc_emit_byte(em, 0xBF);
    cc_emit_u32(em, imm);
}

void cc_emit_int80(CcEmit* em) {
    cc_emit_bytes(em, "\xCD\x80", 2);
}

void cc_emit_push_rax(CcEmit* em) {
    cc_emit_byte(em, 0x50);
}

void cc_emit_pop_rcx(CcEmit* em) {
    cc_emit_byte(em, 0x59);
}

void cc_emit_local_addr_rsi(CcEmit* em, int slot) {
    /* lea rsi, [rbp - (slot+1)*8] */
    cc_emit_bytes(em, "\x48\x8D\xB5", 3);
    cc_emit_i32(em, -((slot + 1) * 8));
}

void cc_emit_store_local(CcEmit* em, int slot) {
    /* mov [rbp - (slot+1)*8], rax */
    cc_emit_bytes(em, "\x48\x89\x85", 3);
    cc_emit_i32(em, -((slot + 1) * 8));
}

void cc_emit_load_local(CcEmit* em, int slot) {
    /* mov rax, [rbp - (slot+1)*8] */
    cc_emit_bytes(em, "\x48\x8B\x85", 3);
    cc_emit_i32(em, -((slot + 1) * 8));
}

void cc_emit_lea_local(CcEmit* em, int slot) {
    /* lea rax, [rbp - (slot+1)*8] */
    cc_emit_bytes(em, "\x48\x8D\x85", 3);
    cc_emit_i32(em, -((slot + 1) * 8));
}

void cc_emit_index_load(CcEmit* em, int base_slot) {
    /* rax = index; element at rbp-(base+1+index)*8 */
    cc_emit_bytes(em, "\x48\x8D\xB5", 3); /* lea rsi, [rbp-(base+1)*8] */
    cc_emit_i32(em, -((base_slot + 1) * 8));
    cc_emit_bytes(em, "\x48\xC1\xE0\x03", 4); /* shl rax, 3 */
    cc_emit_bytes(em, "\x48\x29\xC6", 3);     /* sub rsi, rax */
    cc_emit_bytes(em, "\x48\x8B\x06", 3);     /* mov rax, [rsi] */
}

void cc_emit_index_store(CcEmit* em, int base_slot) {
    /* value in rax, index in rcx */
    cc_emit_bytes(em, "\x48\x8D\xB5", 3); /* lea rsi, [rbp-(base+1)*8] */
    cc_emit_i32(em, -((base_slot + 1) * 8));
    cc_emit_bytes(em, "\x48\xC1\xE1\x03", 4); /* shl rcx, 3 */
    cc_emit_bytes(em, "\x48\x29\xCE", 3);     /* sub rsi, rcx */
    cc_emit_bytes(em, "\x48\x89\x06", 3);     /* mov [rsi], rax */
}

void cc_emit_index_lea(CcEmit* em, int base_slot) {
    /* rax = index → address of element in rax */
    cc_emit_bytes(em, "\x48\x8D\xB5", 3);
    cc_emit_i32(em, -((base_slot + 1) * 8));
    cc_emit_bytes(em, "\x48\xC1\xE0\x03", 4);
    cc_emit_bytes(em, "\x48\x29\xC6", 3);
    cc_emit_bytes(em, "\x48\x89\xF0", 3); /* mov rax, rsi */
}

void cc_emit_load_indirect(CcEmit* em) {
    cc_emit_bytes(em, "\x48\x8B\x00", 3); /* mov rax, [rax] */
}

void cc_emit_store_indirect(CcEmit* em) {
    /* value in rax, address in rcx */
    cc_emit_bytes(em, "\x48\x89\x01", 3); /* mov [rcx], rax */
}

void cc_emit_prologue(CcEmit* em) {
    /* push rbp; mov rbp, rsp; sub rsp, CC_LOCAL_MAX*8 */
    cc_emit_byte(em, 0x55);
    cc_emit_bytes(em, "\x48\x89\xE5", 3);
    cc_emit_bytes(em, "\x48\x81\xEC", 3);
    cc_emit_u32(em, (unsigned)(CC_LOCAL_MAX * 8));
    /* zero locals */
    cc_emit_bytes(em, "\x48\x89\xE7", 3); /* mov rdi, rsp */
    cc_emit_mov_eax_imm(em, 0);
    cc_emit_bytes(em, "\xB9", 1);
    cc_emit_u32(em, (unsigned)CC_LOCAL_MAX);
    /* stosq loop: rep stosq needs rax=0 rcx=count rdi=dest */
    cc_emit_bytes(em, "\xF3\x48\xAB", 3);
}

void cc_emit_fn_prologue(CcEmit* em, int arity) {
    int i;
    cc_emit_prologue(em);
    /* Caller pushes args left-to-right → last arg at [rbp+16]. */
    for (i = 0; i < arity; i++) {
        cc_emit_bytes(em, "\x48\x8B\x85", 3);
        cc_emit_i32(em, 16 + (arity - 1 - i) * 8);
        cc_emit_store_local(em, i);
    }
}

void cc_emit_fn_prologue_sysv(CcEmit* em, int arity) {
    /* Frame without zeroing first — rep stosq would clobber rdi/rsi/rdx/rcx. */
    cc_emit_byte(em, 0x55);                      /* push rbp */
    cc_emit_bytes(em, "\x48\x89\xE5", 3);         /* mov rbp, rsp */
    cc_emit_bytes(em, "\x48\x81\xEC", 3);         /* sub rsp, imm */
    cc_emit_u32(em, (unsigned)(CC_LOCAL_MAX * 8));
    if (arity >= 1) {
        cc_emit_bytes(em, "\x48\x89\xF8", 3); /* mov rax, rdi */
        cc_emit_store_local(em, 0);
    }
    if (arity >= 2) {
        cc_emit_bytes(em, "\x48\x89\xF0", 3); /* mov rax, rsi */
        cc_emit_store_local(em, 1);
    }
    if (arity >= 3) {
        cc_emit_bytes(em, "\x48\x89\xD0", 3); /* mov rax, rdx */
        cc_emit_store_local(em, 2);
    }
    if (arity >= 4) {
        cc_emit_bytes(em, "\x48\x89\xC8", 3); /* mov rax, rcx */
        cc_emit_store_local(em, 3);
    }
}

void cc_emit_leave_ret(CcEmit* em) {
    cc_emit_byte(em, 0xC9); /* leave */
    cc_emit_byte(em, 0xC3); /* ret */
}

size_t cc_emit_call_rel32(CcEmit* em) {
    size_t at;
    cc_emit_byte(em, 0xE8);
    at = em->code_len;
    cc_emit_i32(em, 0);
    return at;
}

/* Reloc: lea rsi sites → data VA */
typedef struct CcReloc {
    size_t code_off; /* offset of disp32 after 48 8D 35 */
    size_t data_off;
} CcReloc;

enum { CC_RELOC_MAX = 64 };

static CcReloc g_relocs[CC_RELOC_MAX];
static int g_nrelocs;

void cc_reloc_reset(void) {
    g_nrelocs = 0;
}

void cc_reloc_add(size_t code_off, size_t data_off) {
    if (g_nrelocs >= CC_RELOC_MAX) {
        return;
    }
    g_relocs[g_nrelocs].code_off = code_off;
    g_relocs[g_nrelocs].data_off = data_off;
    g_nrelocs++;
}

void cc_emit_lea_data(CcEmit* em, size_t data_off) {
    size_t lea_disp_at;
    cc_emit_bytes(em, "\x48\x8D\x05", 3); /* lea rax, [rip+disp] */
    lea_disp_at = em->code_len;
    cc_emit_i32(em, 0);
    cc_reloc_add(lea_disp_at, data_off);
}

void cc_emit_load_global(CcEmit* em, size_t data_off) {
    cc_emit_lea_data(em, data_off);
    cc_emit_bytes(em, "\x48\x8B\x00", 3); /* mov rax, [rax] */
}

void cc_emit_store_global(CcEmit* em, size_t data_off) {
    /* value in rax → [global] */
    cc_emit_push_rax(em);
    cc_emit_lea_data(em, data_off);
    cc_emit_bytes(em, "\x48\x89\xC1", 3); /* mov rcx, rax (addr) */
    cc_emit_byte(em, 0x58);               /* pop rax (value) */
    cc_emit_bytes(em, "\x48\x89\x01", 3); /* mov [rcx], rax */
}

void cc_emit_write_lit2(CcEmit* em, const char* lit) {
    char buf[16];
    size_t lit_len = 0;
    size_t n;
    size_t data_at;
    size_t lea_disp_at;
    while (lit_len < CC_LIT_MAX && lit[lit_len] != '\0') {
        lit_len++;
    }
    buf[0] = 'W';
    buf[1] = 'M';
    buf[2] = 'S';
    buf[3] = 'G';
    buf[4] = ':';
    for (n = 0; n < lit_len; n++) {
        buf[5 + n] = lit[n];
    }
    buf[5 + lit_len] = '\n';
    n = 5 + lit_len + 1;
    data_at = cc_data_push(em, buf, n);

    cc_emit_mov_eax_imm(em, SYS_WRITE);
    cc_emit_mov_edi_imm(em, 1);
    cc_emit_bytes(em, "\x48\x8D\x35", 3);
    lea_disp_at = em->code_len;
    cc_emit_i32(em, 0);
    cc_reloc_add(lea_disp_at, data_at);
    cc_emit_byte(em, 0xBA);
    cc_emit_u32(em, (unsigned)n);
    cc_emit_int80(em);
}

void cc_emit_write_pass(CcEmit* em) {
    static const char pass[] = "ONBOX_OBJ_PASS\n";
    size_t data_at = cc_data_push(em, pass, sizeof(pass) - 1);
    size_t lea_disp_at;
    cc_emit_mov_eax_imm(em, SYS_WRITE);
    cc_emit_mov_edi_imm(em, 1);
    cc_emit_bytes(em, "\x48\x8D\x35", 3);
    lea_disp_at = em->code_len;
    cc_emit_i32(em, 0);
    cc_reloc_add(lea_disp_at, data_at);
    cc_emit_byte(em, 0xBA);
    cc_emit_u32(em, (unsigned)(sizeof(pass) - 1));
    cc_emit_int80(em);
}

void cc_emit_print_rax(CcEmit* em) {
    /* rax → stdout "NMSG:<digits>\n" using a 40-byte stack scratch. */
    size_t jz_zero;
    size_t jmp_fin;
    size_t loop_at;
    size_t jnz_loop;
    cc_emit_bytes(em, "\x48\x83\xEC\x28", 4);       /* sub rsp, 40 */
    cc_emit_bytes(em, "\x48\x89\x44\x24\x20", 5);   /* mov [rsp+32], rax */
    cc_emit_bytes(em, "\x48\x8D\x5C\x24\x1F", 5);   /* lea rbx, [rsp+31] */
    cc_emit_bytes(em, "\xC6\x03\x0A", 3);           /* mov byte [rbx], '\n' */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);           /* dec rbx */
    cc_emit_bytes(em, "\x8B\x44\x24\x20", 4);       /* mov eax, [rsp+32] */
    cc_emit_bytes(em, "\x85\xC0", 2);               /* test eax, eax */
    cc_emit_bytes(em, "\x0F\x84", 2);
    jz_zero = em->code_len;
    cc_emit_i32(em, 0);
    loop_at = em->code_len;
    cc_emit_bytes(em, "\x31\xD2", 2);               /* xor edx, edx */
    cc_emit_bytes(em, "\xB9\x0A\x00\x00\x00", 5);   /* mov ecx, 10 */
    cc_emit_bytes(em, "\xF7\xF1", 2);               /* div ecx */
    cc_emit_bytes(em, "\x80\xC2\x30", 3);           /* add dl, '0' */
    cc_emit_bytes(em, "\x88\x13", 2);               /* mov [rbx], dl */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);           /* dec rbx */
    cc_emit_bytes(em, "\x85\xC0", 2);               /* test eax, eax */
    cc_emit_bytes(em, "\x0F\x85", 2);
    jnz_loop = em->code_len;
    cc_emit_i32(em, 0);
    patch_rel32_local(em, jnz_loop, loop_at);
    cc_emit_byte(em, 0xE9);
    jmp_fin = em->code_len;
    cc_emit_i32(em, 0);
    patch_rel32_local(em, jz_zero, em->code_len);
    cc_emit_bytes(em, "\xC6\x03\x30", 3);           /* mov byte [rbx], '0' */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);           /* dec rbx */
    patch_rel32_local(em, jmp_fin, em->code_len);
    /* prefix NMSG: leftward */
    cc_emit_bytes(em, "\xC6\x03\x3A", 3); /* ':' */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);
    cc_emit_bytes(em, "\xC6\x03\x47", 3); /* 'G' */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);
    cc_emit_bytes(em, "\xC6\x03\x53", 3); /* 'S' */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);
    cc_emit_bytes(em, "\xC6\x03\x4D", 3); /* 'M' */
    cc_emit_bytes(em, "\x48\xFF\xCB", 3);
    cc_emit_bytes(em, "\xC6\x03\x4E", 3); /* 'N' */
    cc_emit_bytes(em, "\x48\x89\xDE", 3);           /* mov rsi, rbx */
    cc_emit_bytes(em, "\x48\x8D\x54\x24\x20", 5);   /* lea rdx, [rsp+32] */
    cc_emit_bytes(em, "\x48\x29\xF2", 3);           /* sub rdx, rsi */
    cc_emit_mov_eax_imm(em, SYS_WRITE);
    cc_emit_mov_edi_imm(em, 1);
    cc_emit_int80(em);
    cc_emit_bytes(em, "\x48\x83\xC4\x28", 4); /* add rsp, 40 */
}

void cc_emit_input_slot(CcEmit* em, int slot) {
    /* open("disk/io.in"), read decimal, store local slot. */
    static const char path[] = "disk/io.in";
    size_t data_at = cc_data_push(em, path, sizeof(path));
    size_t lea_at;
    size_t js_fail;
    size_t jmp_store;
    size_t parse_at;
    size_t ja_end;
    size_t jb_end;
    size_t jmp_parse;

    cc_emit_mov_eax_imm(em, SYS_OPEN);
    cc_emit_bytes(em, "\x48\x8D\x3D", 3); /* lea rdi, [rip+path] */
    lea_at = em->code_len;
    cc_emit_i32(em, 0);
    cc_reloc_add(lea_at, data_at);
    cc_emit_bytes(em, "\x31\xF6", 2); /* xor esi, esi  O_RDONLY */
    cc_emit_bytes(em, "\x31\xD2", 2); /* xor edx, edx */
    cc_emit_int80(em);
    cc_emit_bytes(em, "\x85\xC0", 2); /* test eax, eax */
    cc_emit_bytes(em, "\x0F\x88", 2); /* js fail */
    js_fail = em->code_len;
    cc_emit_i32(em, 0);

    cc_emit_byte(em, 0x50); /* push rax = fd */
    cc_emit_bytes(em, "\x48\x83\xEC\x10", 4); /* sub rsp, 16 */
    cc_emit_mov_eax_imm(em, SYS_READ);
    cc_emit_bytes(em, "\x8B\x7C\x24\x10", 4); /* mov edi, [rsp+16] fd */
    cc_emit_bytes(em, "\x48\x89\xE6", 3);     /* mov rsi, rsp */
    cc_emit_byte(em, 0xBA);
    cc_emit_u32(em, 15);
    cc_emit_int80(em);

    cc_emit_bytes(em, "\x31\xC0", 2);         /* xor eax, eax result */
    cc_emit_bytes(em, "\x48\x89\xE6", 3);     /* mov rsi, rsp */
    parse_at = em->code_len;
    cc_emit_bytes(em, "\x0F\xB6\x0E", 3);     /* movzx ecx, byte [rsi] */
    cc_emit_bytes(em, "\x83\xF9\x30", 3);     /* cmp ecx, '0' */
    cc_emit_bytes(em, "\x0F\x82", 2);         /* jb end */
    jb_end = em->code_len;
    cc_emit_i32(em, 0);
    cc_emit_bytes(em, "\x83\xF9\x39", 3); /* cmp ecx, '9' */
    cc_emit_bytes(em, "\x0F\x87", 2);     /* ja end */
    ja_end = em->code_len;
    cc_emit_i32(em, 0);
    cc_emit_bytes(em, "\x83\xE9\x30", 3);           /* sub ecx, '0' */
    cc_emit_bytes(em, "\x69\xC0\x0A\x00\x00\x00", 6); /* imul eax, eax, 10 */
    cc_emit_bytes(em, "\x01\xC8", 2);               /* add eax, ecx */
    cc_emit_bytes(em, "\x48\xFF\xC6", 3);           /* inc rsi */
    cc_emit_byte(em, 0xE9);
    jmp_parse = em->code_len;
    cc_emit_i32(em, 0);
    patch_rel32_local(em, jmp_parse, parse_at);
    patch_rel32_local(em, jb_end, em->code_len);
    patch_rel32_local(em, ja_end, em->code_len);

    cc_emit_bytes(em, "\x89\xC3", 2); /* mov ebx, eax keep value */
    cc_emit_mov_eax_imm(em, SYS_CLOSE);
    cc_emit_bytes(em, "\x8B\x7C\x24\x10", 4); /* mov edi, [rsp+16] */
    cc_emit_int80(em);
    cc_emit_bytes(em, "\x48\x83\xC4\x10", 4); /* add rsp, 16 */
    cc_emit_byte(em, 0x59);                   /* pop rcx discard fd */
    cc_emit_bytes(em, "\x89\xD8", 2);         /* mov eax, ebx */
    cc_emit_byte(em, 0xE9);
    jmp_store = em->code_len;
    cc_emit_i32(em, 0);

    patch_rel32_local(em, js_fail, em->code_len);
    cc_emit_bytes(em, "\x31\xC0", 2); /* xor eax, eax */
    patch_rel32_local(em, jmp_store, em->code_len);
    cc_emit_store_local(em, slot);
}

void cc_emit_exit_rax(CcEmit* em) {
    /* Caller must leave status in ebx (write clobbers eax). */
    cc_emit_bytes(em, "\x89\xDF", 2); /* mov edi, ebx */
    cc_emit_mov_eax_imm(em, SYS_EXIT);
    cc_emit_int80(em);
    cc_emit_bytes(em, "\x0F\x0B", 2);
}

int cc_build_elf(CcEmit* em, unsigned char** out, size_t* out_len) {
    const size_t ehdr = 64;
    const size_t phdr = 56;
    const size_t file_off = ehdr + phdr;
    size_t body_len;
    size_t data_file_off;
    size_t total;
    size_t memsz;
    unsigned char* elf;
    size_t i;
    int rel;

    if (em->failed) {
        return -1;
    }
    body_len = em->code_len + em->data_len;
    data_file_off = file_off + em->code_len;
    total = file_off + body_len;
    memsz = (total + 4095u) & ~4095u;
    if (memsz < 4096) {
        memsz = 4096;
    }
    /* Cap emitted ELF so a static buffer can hold it (ehdr+phdr+code+data). */
    if (total > sizeof(g_elf_buf)) {
        return -1;
    }
    elf = g_elf_buf;
    memset(elf, 0, total);

    /* ELF64 header */
    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;
    elf[5] = 1;
    elf[6] = 1;
    /* e_type=2 ET_EXEC, e_machine=62 */
    elf[16] = 2;
    elf[18] = 62;
    elf[20] = 1; /* version */
    /* e_entry — use ull so host Win32 builds don't trip -Wshift-count-overflow */
    {
        unsigned long long entry = (unsigned long long)CC_LOAD_VA;
        size_t o = 24;
        elf[o] = (unsigned char)(entry);
        elf[o + 1] = (unsigned char)(entry >> 8);
        elf[o + 2] = (unsigned char)(entry >> 16);
        elf[o + 3] = (unsigned char)(entry >> 24);
        elf[o + 4] = (unsigned char)(entry >> 32);
        elf[o + 5] = (unsigned char)(entry >> 40);
        elf[o + 6] = (unsigned char)(entry >> 48);
        elf[o + 7] = (unsigned char)(entry >> 56);
    }
    /* e_phoff = 64 */
    elf[32] = 64;
    /* e_ehsize=64, e_phentsize=56, e_phnum=1 */
    elf[52] = 64;
    elf[54] = 56;
    elf[56] = 1;

    /* Phdr: PT_LOAD PF_R|PF_X|PF_W (7) — need W for locals? Locals on stack.
       Text+rodata R+X=5; but we put data in same segment — use 5 and put
       strings in RX (OK read). */
    {
        unsigned char* p = elf + ehdr;
        unsigned long long va = (unsigned long long)CC_LOAD_VA;
        unsigned long long fsz = (unsigned long long)body_len;
        unsigned long long msz = (unsigned long long)(memsz - file_off);
        if (msz < fsz) {
            msz = fsz;
        }
        p[0] = 1; /* PT_LOAD */
        p[4] = 5; /* PF_R|PF_X */
        /* p_offset */
        p[8] = (unsigned char)(file_off);
        p[9] = (unsigned char)(file_off >> 8);
        /* p_vaddr / p_paddr */
        for (i = 0; i < 8; i++) {
            p[16 + i] = (unsigned char)(va >> (8 * i));
            p[24 + i] = (unsigned char)(va >> (8 * i));
        }
        for (i = 0; i < 8; i++) {
            p[32 + i] = (unsigned char)(fsz >> (8 * i));
            p[40 + i] = (unsigned char)(msz >> (8 * i));
        }
        p[48] = 0x00;
        p[49] = 0x10; /* align 0x1000 */
    }

    /* Patch RIP-relative lea: disp = (data_file_off + data_off) - (file_off + code_off + 4) */
    for (i = 0; (int)i < g_nrelocs; i++) {
        size_t code_off = g_relocs[i].code_off;
        size_t data_off = g_relocs[i].data_off;
        size_t next_ip = file_off + code_off + 4;
        size_t target = data_file_off + data_off;
        rel = (int)(target - next_ip);
        /* But lea is relative to RIP at runtime = LOAD_VA + (code_off+4) */
        next_ip = CC_LOAD_VA + code_off + 4;
        target = CC_LOAD_VA + em->code_len + data_off;
        rel = (int)(target - next_ip);
        em->code[code_off] = (unsigned char)(rel);
        em->code[code_off + 1] = (unsigned char)(rel >> 8);
        em->code[code_off + 2] = (unsigned char)(rel >> 16);
        em->code[code_off + 3] = (unsigned char)(rel >> 24);
    }

    memcpy(elf + file_off, em->code, em->code_len);
    memcpy(elf + data_file_off, em->data, em->data_len);
    *out = elf;
    *out_len = total;
    return 0;
}

/* ET_REL for #lib: .text + STB_GLOBAL STT_FUNC exports (SysV bodies). */
int cc_build_rel_elf(CcEmit* em, unsigned char** out, size_t* out_len) {
    enum { MAX_SYM = CC_FN_MAX };
    int order[MAX_SYM];
    size_t sizes[MAX_SYM];
    int nfn = 0;
    int i;
    int j;
    size_t strtab_len;
    size_t shstr_len;
    size_t off_text;
    size_t off_shstr;
    size_t off_sym;
    size_t off_str;
    size_t off_sh;
    size_t nsym;
    size_t total;
    size_t name_off;
    unsigned char* elf;
    static const char shstr[] = "\0.text\0.shstrtab\0.symtab\0.strtab";

    if (!em || em->failed || em->code_len == 0 ||
        em->code_len > CC_CODE_MAX) {
        return -1;
    }
    /* M113: no .data / RIP lea in objects (keep import path simple). */
    if (em->data_len != 0 || g_nrelocs != 0) {
        return -1;
    }
    if (em->nfns < 1 || em->nfns > CC_FN_MAX) {
        return -1;
    }
    for (i = 0; i < em->nfns; i++) {
        if (!em->fns[i].defined || em->fns[i].name[0] == 0) {
            continue;
        }
        if (nfn >= MAX_SYM) {
            return -1;
        }
        order[nfn++] = i;
    }
    if (nfn < 1) {
        return -1;
    }
    /* Sort by entry ascending for size calc. */
    for (i = 0; i < nfn - 1; i++) {
        for (j = i + 1; j < nfn; j++) {
            if (em->fns[order[j]].entry < em->fns[order[i]].entry) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (i = 0; i < nfn; i++) {
        size_t start = em->fns[order[i]].entry;
        size_t end =
            (i + 1 < nfn) ? em->fns[order[i + 1]].entry : em->code_len;
        if (end <= start || end > em->code_len) {
            return -1;
        }
        sizes[i] = end - start;
    }

    strtab_len = 1;
    for (i = 0; i < nfn; i++) {
        size_t nl = 0;
        while (nl < CC_IDENT_MAX && em->fns[order[i]].name[nl] != 0) {
            nl++;
        }
        if (nl == 0 || em->fns[order[i]].name[nl] != 0) {
            return -1;
        }
        strtab_len += nl + 1;
    }
    shstr_len = sizeof(shstr);
    nsym = (size_t)nfn + 1;
    off_text = 64;
    if (em->code_len > sizeof(g_elf_buf) - 64) {
        return -1;
    }
    off_shstr = off_text + em->code_len;
    off_sym = (off_shstr + shstr_len + 7u) & ~(size_t)7;
    off_str = off_sym + nsym * 24;
    off_sh = (off_str + strtab_len + 7u) & ~(size_t)7;
    total = off_sh + 5 * 64;
    if (total < off_sh || total > sizeof(g_elf_buf)) {
        return -1;
    }
    elf = g_elf_buf;
    memset(elf, 0, total);

    /* ELF64 ET_REL header */
    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;
    elf[5] = 1;
    elf[6] = 1;
    elf[16] = 1; /* ET_REL */
    elf[18] = 62;
    elf[20] = 1;
    {
        unsigned long long shoff = (unsigned long long)off_sh;
        size_t o = 40;
        for (i = 0; i < 8; i++) {
            elf[o + i] = (unsigned char)(shoff >> (8 * i));
        }
    }
    elf[52] = 64; /* e_ehsize */
    elf[58] = 64; /* e_shentsize */
    elf[60] = 5;  /* e_shnum */
    elf[62] = 2;  /* e_shstrndx */

    memcpy(elf + off_text, em->code, em->code_len);
    memcpy(elf + off_shstr, shstr, shstr_len);

    /* null symbol */
    memset(elf + off_sym, 0, 24);
    name_off = 1;
    elf[off_str] = 0;
    {
        size_t so = 1;
        for (i = 0; i < nfn; i++) {
            unsigned char* sy = elf + off_sym + (size_t)(i + 1) * 24;
            size_t nl = 0;
            while (nl < CC_IDENT_MAX && em->fns[order[i]].name[nl] != 0) {
                nl++;
            }
            memcpy(elf + off_str + so, em->fns[order[i]].name, nl);
            elf[off_str + so + nl] = 0;
            sy[0] = (unsigned char)name_off;
            sy[1] = (unsigned char)(name_off >> 8);
            sy[2] = (unsigned char)(name_off >> 16);
            sy[3] = (unsigned char)(name_off >> 24);
            sy[4] = (unsigned char)((1u << 4) | 2u); /* STB_GLOBAL STT_FUNC */
            sy[6] = 1;                               /* .text shndx */
            {
                unsigned long long v =
                    (unsigned long long)em->fns[order[i]].entry;
                unsigned long long sz = (unsigned long long)sizes[i];
                int k;
                for (k = 0; k < 8; k++) {
                    sy[8 + k] = (unsigned char)(v >> (8 * k));
                    sy[16 + k] = (unsigned char)(sz >> (8 * k));
                }
            }
            name_off += nl + 1;
            so += nl + 1;
        }
    }

    /* Section headers: 0 null, 1 .text, 2 .shstrtab, 3 .symtab, 4 .strtab */
    {
        unsigned char* sh = elf + off_sh;
        /* 0 */
        memset(sh, 0, 64);
        /* 1 .text name=1 type=PROGBITS flags=6 */
        {
            unsigned char* s = sh + 64;
            unsigned long long off = (unsigned long long)off_text;
            unsigned long long sz = (unsigned long long)em->code_len;
            int k;
            s[0] = 1;
            s[4] = 1; /* SHT_PROGBITS */
            s[8] = 6; /* ALLOC|EXEC */
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1; /* addralign */
        }
        /* 2 .shstrtab name=7 */
        {
            unsigned char* s = sh + 128;
            unsigned long long off = (unsigned long long)off_shstr;
            unsigned long long sz = (unsigned long long)shstr_len;
            int k;
            s[0] = 7;
            s[4] = 3; /* SHT_STRTAB */
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
        }
        /* 3 .symtab name=17 link=4 info=1 entsize=24 */
        {
            unsigned char* s = sh + 192;
            unsigned long long off = (unsigned long long)off_sym;
            unsigned long long sz = (unsigned long long)(nsym * 24);
            int k;
            s[0] = 17;
            s[4] = 2; /* SHT_SYMTAB */
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[40] = 4; /* link .strtab */
            s[44] = 1; /* info first global */
            s[48] = 8; /* addralign */
            s[56] = 24;
        }
        /* 4 .strtab name=25 */
        {
            unsigned char* s = sh + 256;
            unsigned long long off = (unsigned long long)off_str;
            unsigned long long sz = (unsigned long long)strtab_len;
            int k;
            s[0] = 25;
            s[4] = 3;
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
        }
    }

    *out = elf;
    *out_len = total;
    return 0;
}
