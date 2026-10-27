#define CC_HOST 1
#include <stdio.h>
#include <string.h>
#include "cxc_cc/cc.h"

int main(void) {
    const char* ok =
        ".text\n.globl main\nmain:\n"
        "lea s(%rip), %rax\n"
        "mov $42, %eax\n"
        "ret\n"
        ".data\n"
        "buf:\n.zero 8\n"
        ".bss\n"
        "zb:\n.zero 8\n"
        ".rodata\n"
        "s:\n.asciz \"HD\"\n";
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[1];
    int rc = as_compile(ok, strlen(ok), &o, &on);
    printf("as rc=%d on=%zu fail=%d\n", rc, on, as_last_fail_code());
    if (rc != 0 || !o) {
        return 1;
    }
    {
        FILE* f = fopen("disk/m245.o", "wb");
        fwrite(o, 1, on, f);
        fclose(f);
        printf("ET_REL e_type=%u shnum=%u\n", o[16], o[60]);
    }
    paths[0] = "disk/m245.o";
    rc = ld_link(paths, 1, &elf, &en);
    printf("ld rc=%d en=%zu fail=%d\n", rc, en, ld_last_fail_code());
    if (elf) {
        printf("e_type=%u e_phnum=%u e_phnum_hi=%u\n", elf[16], elf[56],
               elf[57]);
        if (elf[56] >= 1) {
            unsigned char* ph0 = elf + 64;
            printf("ph0 type=%u flags=%u\n", ph0[0], ph0[4]);
        }
        if (elf[56] >= 2) {
            unsigned char* ph1 = elf + 64 + 56;
            printf("ph1 type=%u flags=%u\n", ph1[0], ph1[4]);
        }
        if (elf[56] >= 3) {
            unsigned char* ph2 = elf + 64 + 112;
            printf("ph2 type=%u flags=%u\n", ph2[0], ph2[4]);
        }
    }
    return 0;
}
