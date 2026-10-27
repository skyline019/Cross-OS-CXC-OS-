#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char* lib =
        "CXC_A2\n"
        "typedef unsigned long size_t;\n"
        "struct Pt {\n"
        "x;\n"
        "y;\n"
        "};\n"
        "fn score(){\n"
        "Pt p;\n"
        "Pt* sp;\n"
        "long n;\n"
        "size_t z;\n"
        "char* cp;\n"
        "p.x=6;\n"
        "p.y=7;\n"
        "sp=&p;\n"
        "n=sp->x*sp->y;\n"
        "z=sizeof(char);\n"
        "if(z!=1){\n"
        "return 1;\n"
        "}\n"
        "z=sizeof(char*);\n"
        "if(z!=8){\n"
        "return 1;\n"
        "}\n"
        "z=sizeof(long);\n"
        "if(z!=8){\n"
        "return 1;\n"
        "}\n"
        "z=sizeof(size_t);\n"
        "if(z!=8){\n"
        "return 1;\n"
        "}\n"
        "cp=\"ok\";\n"
        "return n;\n"
        "}\n";
    const char* main_s =
        ".text\n"
        ".globl main\n"
        "main:\n"
        "jmp .Lstart\n"
        ".Lbad:\n"
        "mov $1, %eax\n"
        "ret\n"
        ".Lstart:\n"
        "push %rbp\n"
        "push %rbx\n"
        "call score\n"
        "pop %rbx\n"
        "pop %rbp\n"
        "cmp $42, %eax\n"
        "jne .Lbad\n"
        "mov $6, %eax\n"
        "imul $7, %eax\n"
        "cmp $42, %eax\n"
        "jne .Lbad\n"
        "mov q(%rip), %rax\n"
        "cmp $42, %eax\n"
        "jne .Lbad\n"
        "mov $42, %eax\n"
        "ret\n"
        ".rodata\n"
        "q:\n"
        ".quad 42\n";
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[2];
    FILE* f;
    int r;

    r = cc_compile_obj(lib, strlen(lib), &o, &on);
    printf("lib r=%d n=%zu fail=%d\n", r, on, cc_last_fail_code());
    if (r != 0) {
        return 1;
    }
    f = fopen("disk/lib.o", "wb");
    if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
        return 2;
    }
    o = 0;
    on = 0;
    r = as_compile(main_s, strlen(main_s), &o, &on);
    printf("as r=%d n=%zu fail=%d\n", r, on, as_last_fail_code());
    if (r != 0) {
        return 3;
    }
    f = fopen("disk/main.o", "wb");
    if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
        return 4;
    }
    paths[0] = "disk/main.o";
    paths[1] = "disk/lib.o";
    r = ld_link(paths, 2, &elf, &en);
    printf("ld r=%d n=%zu fail=%d et=%d\n", r, en, ld_last_fail_code(),
           elf ? elf[16] : -1);
    return r != 0;
}
