#define CC_HOST 1
#include <stdio.h>
#include <string.h>
#include "cxc_cc/cc.h"

int main(void) {
    const char* main_s =
        ".text\n.globl main\nmain:\n"
        "lea buf(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea s4(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strcpy\n"
        "mov $42, %eax\n"
        "ret\n"
        ".data\n"
        "buf:\n"
        ".zero 16\n"
        ".rodata\n"
        "s4:\n"
        ".asciz \"4\"\n";
    const char* lib = "CXC_A2\nfn score(){\nreturn 42;\n}\n";
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    FILE* f;
    const char* paths[14];
    int i;
    if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0) {
        fprintf(stderr, "cc fail\n");
        return 1;
    }
    f = fopen("disk/lib.o", "wb");
    fwrite(o, 1, on, f);
    fclose(f);
    if (as_compile(main_s, strlen(main_s), &o, &on) != 0) {
        fprintf(stderr, "as fail %d\n", as_last_fail_code());
        return 1;
    }
    f = fopen("disk/main.o", "wb");
    fwrite(o, 1, on, f);
    fclose(f);
    paths[0] = "disk/main.o";
    paths[1] = "disk/lib.o";
    paths[2] = "disk/atoi.o";
    paths[3] = "disk/scmp.o";
    paths[4] = "disk/labs.o";
    paths[5] = "disk/dig.o";
    paths[6] = "disk/scat.o";
    paths[7] = "disk/schr.o";
    paths[8] = "disk/snul.o";
    paths[9] = "disk/scpy.o";
    paths[10] = "disk/stpc.o";
    paths[11] = "disk/sncmp.o";
    paths[12] = "disk/mcmp.o";
    paths[13] = "disk/io.elf";
    if (ld_link(paths, 14, &elf, &en) != 0 || !elf) {
        fprintf(stderr, "ld fail %d\n", ld_last_fail_code());
        return 1;
    }
    f = fopen("disk/phdr_check.elf", "wb");
    fwrite(elf, 1, en, f);
    fclose(f);
    printf("en=%zu phnum=%u\n", en, (unsigned)elf[56]);
    for (i = 0; i < (int)elf[56]; i++) {
        const unsigned char* p = elf + 64 + i * 56;
        unsigned long long off = 0, va = 0, fsz = 0;
        int b;
        for (b = 0; b < 8; b++) {
            off |= (unsigned long long)p[8 + b] << (8 * b);
            va |= (unsigned long long)p[16 + b] << (8 * b);
            fsz |= (unsigned long long)p[32 + b] << (8 * b);
        }
        printf("ph%d flags=%u off=0x%llx va=0x%llx fsz=0x%llx\n", i,
               (unsigned)p[4], (unsigned long long)off, (unsigned long long)va,
               (unsigned long long)fsz);
    }
    return 0;
}
