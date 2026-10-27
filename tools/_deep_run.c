#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Same score+frame checks as onbox probe (no musl). */
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
        "and $255, %eax\n"
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
    int rc;

    if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0) {
        fprintf(stderr, "lib fail %d\n", cc_last_fail_code());
        return 1;
    }
    f = fopen("disk/lib.o", "wb");
    fwrite(o, 1, on, f);
    fclose(f);
    o = 0;
    on = 0;
    if (as_compile(main_s, strlen(main_s), &o, &on) != 0) {
        fprintf(stderr, "as fail %d\n", as_last_fail_code());
        return 1;
    }
    f = fopen("disk/main.o", "wb");
    fwrite(o, 1, on, f);
    fclose(f);
    paths[0] = "disk/main.o";
    paths[1] = "disk/lib.o";
    if (ld_link(paths, 2, &elf, &en) != 0) {
        fprintf(stderr, "ld fail %d\n", ld_last_fail_code());
        return 1;
    }
    f = fopen("disk/deep_out.elf", "wb");
    fwrite(elf, 1, en, f);
    fclose(f);
    printf("wrote disk/deep_out.elf n=%zu\n", en);
    rc = system("chmod +x disk/deep_out.elf; "
                "(command -v qemu-x86_64 >/dev/null && "
                "qemu-x86_64 disk/deep_out.elf; echo qemu_exit=$?) || "
                "(./disk/deep_out.elf; echo native_exit=$?)");
    (void)rc;
    return 0;
}
