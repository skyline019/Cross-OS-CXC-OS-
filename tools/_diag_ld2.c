#define CC_HOST 1
#include <stdio.h>
#include <string.h>
#include "cxc_cc/cc.h"

/* Peek ld internals via duplicating critical path. */
int main(void) {
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];
    static unsigned char ro_buf[CC_DATA_MAX];
    size_t jmp_entry;
    memset(&em, 0, sizeof(em));
    em.code = code_buf;
    em.data = data_buf;
    em.ro = ro_buf;
    em.code_cap = CC_CODE_MAX;
    em.data_cap = CC_DATA_MAX;
    em.ro_cap = CC_DATA_MAX;
    cc_reloc_reset();
    cc_und_reset();
    cc_lib_reset();
    cc_link_reset();

    /* emit short jmp like ld */
    cc_emit_byte(&em, 0xE9);
    jmp_entry = em.code_len;
    cc_emit_i32(&em, 0);
    printf("after jmp failed=%d len=%zu\n", em.failed, em.code_len);

    if (cc_import_elf(&em, "disk/m245.o") != 0) {
        printf("import fail code=%d failed=%d\n", em.fail_code, em.failed);
        return 1;
    }
    printf("after import nexp=%d failed=%d\n", em.nexports, em.failed);
    {
        int i;
        for (i = 0; i < em.nexports; i++) {
            printf("  exp[%d]=%s at %zu\n", i, em.exports[i].name,
                   em.exports[i].entry);
        }
    }
    if (cc_und_resolve(&em) != 0) {
        printf("und fail %d\n", em.fail_code);
        return 1;
    }
    cc_emit_prologue(&em);
    printf("after prologue failed=%d len=%zu\n", em.failed, em.code_len);
    cc_emit_setup_fs_tcb(&em);
    printf("after fs failed=%d len=%zu rw=%zu relocs=%d\n", em.failed,
           em.code_len, em.rw_len, cc_reloc_count());
    {
        unsigned char* out = 0;
        size_t n = 0;
        int rc = cc_build_elf(&em, &out, &n);
        printf("build_elf rc=%d n=%zu failed=%d\n", rc, n, em.failed);
        if (out && n > 60) {
            printf("phnum=%u\n", out[56]);
        }
    }
    (void)jmp_entry;
    return 0;
}
