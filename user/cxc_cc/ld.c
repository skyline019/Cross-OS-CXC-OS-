/* M129: freestanding mini-ld — ET_REL objs + CRT → ET_EXEC (A2 ld subset). */
#include "cxc_cc/cc.h"

enum { LD_FAIL = 50 };

static int g_ld_fail;

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

static int find_export(CcEmit* em, const char* name, size_t* out) {
    int i;
    size_t k;
    for (i = 0; i < em->nexports; i++) {
        k = 0;
        while (name[k] && em->exports[i].name[k] &&
               name[k] == em->exports[i].name[k]) {
            k++;
        }
        if (name[k] == 0 && em->exports[i].name[k] == 0) {
            *out = em->exports[i].entry;
            return 0;
        }
    }
    return -1;
}

int ld_link(const char* const* paths, int npaths, unsigned char** out_elf,
            size_t* out_len) {
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];
    size_t jmp_entry;
    size_t entry_at;
    size_t main_at;
    size_t call_at;
    int i;

    g_ld_fail = 0;
    if (!paths || npaths < 1 || npaths > CC_LINK_MAX) {
        g_ld_fail = LD_FAIL;
        return -1;
    }

    memset(&em, 0, sizeof(em));
    em.code = code_buf;
    em.data = data_buf;
    em.code_cap = CC_CODE_MAX;
    em.data_cap = CC_DATA_MAX;
    em.obj_mode = 0;
    cc_reloc_reset();
    cc_und_reset();
    cc_lib_reset();
    cc_link_reset();

    jmp_entry = emit_jmp32(&em);
    if (em.failed) {
        g_ld_fail = LD_FAIL;
        return -1;
    }

    for (i = 0; i < npaths; i++) {
        if (!paths[i] || paths[i][0] == 0) {
            g_ld_fail = LD_FAIL;
            return -1;
        }
        if (cc_import_elf(&em, paths[i]) != 0) {
            g_ld_fail = em.fail_code ? em.fail_code : LD_FAIL;
            return -1;
        }
    }
    if (cc_und_resolve(&em) != 0) {
        g_ld_fail = em.fail_code ? em.fail_code : 21;
        return -1;
    }

    entry_at = em.code_len;
    patch_rel32(&em, jmp_entry, entry_at);
    cc_emit_prologue(&em);

    if (find_export(&em, "main", &main_at) != 0) {
        g_ld_fail = LD_FAIL;
        return -1;
    }
    call_at = cc_emit_call_rel32(&em);
    patch_rel32(&em, call_at, main_at);
    cc_emit_bytes(&em, "\x89\xC3", 2); /* mov ebx, eax (status) */
    cc_emit_byte(&em, 0x53);           /* push rbx — print clobbers rbx */
    cc_emit_bytes(&em, "\x89\xD8", 2); /* mov eax, ebx for NMSG */
    cc_emit_print_rax(&em);
    cc_emit_byte(&em, 0x5B); /* pop rbx */
    cc_emit_write_lit2(&em, "MX");
    cc_emit_write_pass(&em);
    cc_emit_exit_rax(&em);

    if (em.failed) {
        g_ld_fail = LD_FAIL;
        return -1;
    }
    if (cc_build_elf(&em, out_elf, out_len) != 0) {
        g_ld_fail = 20;
        return -1;
    }
    return 0;
}

int ld_last_fail_code(void) {
    return g_ld_fail;
}
