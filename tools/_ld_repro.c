#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const char* objs[] = {
        "user/prebuilt/musl_strtol.o",
        "user/prebuilt/musl_intscan.o",
        "user/prebuilt/musl_shgetc.o",
        "user/prebuilt/musl_memrchr.o",
        "user/prebuilt/hard_stubs.o",
        "user/prebuilt/musl_memcpy.o",
        "user/prebuilt/musl_strlen.o",
    };
    static CcEmit em;
    static unsigned char code[65536];
    static unsigned char data[16384];
    static unsigned char ro[16384];
    int i;

    memset(&em, 0, sizeof(em));
    em.code = code;
    em.data = data;
    em.ro = ro;
    em.code_cap = 65536;
    em.data_cap = 16384;
    em.ro_cap = 16384;
    cc_reloc_reset();
    cc_und_reset();

    for (i = 0; i < (int)(sizeof(objs) / sizeof(objs[0])); i++) {
        em.fail_code = 0;
        em.failed = 0;
        printf("import %s...\n", objs[i]);
        if (cc_import_elf(&em, objs[i]) != 0) {
            printf("FAIL import %s code=%d\n", objs[i], em.fail_code);
            return 1;
        }
        printf("  ok code_len=%zu data_len=%zu ro_len=%zu bss=%zu nexp=%d\n",
               em.code_len, em.data_len, em.ro_len, em.bss_len, em.nexports);
    }
    if (cc_und_resolve(&em) != 0) {
        printf("FAIL und_resolve code=%d\n", em.fail_code);
        return 1;
    }
    printf("ALL OK\n");
    return 0;
}
