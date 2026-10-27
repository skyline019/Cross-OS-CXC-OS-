#define CC_HOST 1
#include <stdio.h>
#include <string.h>
#include "cxc_cc/cc.h"

int main(void) {
    static CcEmit em;
    static unsigned char code_buf[CC_CODE_MAX];
    static unsigned char data_buf[CC_DATA_MAX];
    static unsigned char ro_buf[CC_DATA_MAX];
    int rc;
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
    rc = cc_import_elf(&em, "disk/m245.o");
    printf("import rc=%d failed=%d fail_code=%d code_len=%zu ro=%zu rw=%zu "
           "bss=%zu nexp=%d nrelocs=%d\n",
           rc, em.failed, em.fail_code, em.code_len, em.ro_len, em.rw_len,
           em.bss_len, em.nexports, cc_reloc_count());
    return 0;
}
