#define CC_HOST 1
#include <stdio.h>
#include <string.h>
#include "cxc_cc/cc.h"

int main(void) {
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[1];
    paths[0] = "disk/m245.o";
    int rc = ld_link(paths, 1, &elf, &en);
    printf("ld rc=%d en=%zu fail=%d\n", rc, en, ld_last_fail_code());
    if (elf && en > 60) {
        printf("phnum=%u type=%u\n", elf[56], elf[16]);
    }
    return 0;
}
