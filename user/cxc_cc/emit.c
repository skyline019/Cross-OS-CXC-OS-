#include "cxc_cc/cc.h"

enum {
    SYS_WRITE = 1,
    SYS_EXIT = 60
};

int cc_local_slot(CcEmit* em, const char* name, int create) {
    int i;
    if (em->nlocals < 0 || em->nlocals > CC_LOCAL_MAX) {
        em->nlocals = 0;
    }
    for (i = 0; i < em->nlocals; i++) {
        if (strcmp(em->locals[i].name, name) == 0) {
            return em->locals[i].slot;
        }
    }
    if (!create || em->nlocals >= CC_LOCAL_MAX) {
        return -1;
    }
    {
        CcLocal* loc = &em->locals[em->nlocals];
        size_t n = 0;
        while (n < CC_IDENT_MAX && name[n] != '\0') {
            loc->name[n] = name[n];
            n++;
        }
        loc->name[n] = 0;
        loc->slot = em->nlocals;
        em->nlocals++;
        return loc->slot;
    }
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

/* Reloc: lea rsi sites → data VA */
typedef struct CcReloc {
    size_t code_off; /* offset of disp32 after 48 8D 35 */
    size_t data_off;
} CcReloc;

enum { CC_RELOC_MAX = 32 };

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
    static const char pass[] = "ONBOX_CTRL_PASS\n";
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
    static unsigned char elf_buf[CC_CODE_MAX + CC_DATA_MAX + 256];
    if (total > sizeof(elf_buf)) {
        return -1;
    }
    elf = elf_buf;
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
    /* e_entry */
    {
        unsigned long entry = CC_LOAD_VA;
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
        unsigned long va = CC_LOAD_VA;
        unsigned long fsz = body_len;
        unsigned long msz = memsz - file_off;
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
