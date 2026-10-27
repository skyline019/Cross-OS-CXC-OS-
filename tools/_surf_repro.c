/* Quick host repro of M184 success link.rsp. */
#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>

static const char main_s[] =
    ".text\n"
    ".globl main\n"
    "main:\n"
    "push %rbp\n"
    "mov %rsp, %rbp\n"
    "sub $32, %rsp\n"
    "mov $6, %eax\n"
    "imul $7, %eax\n"
    "cmp $42, %eax\n"
    "jne .Lim\n"
    "mov $84, %eax\n"
    "cdq\n"
    "mov $2, %ecx\n"
    "idiv %ecx\n"
    "cmp $42, %eax\n"
    "jne .Ldv\n"
    "xor %edx, %edx\n"
    "mov $84, %eax\n"
    "mov $2, %ecx\n"
    "div %ecx\n"
    "cmp $42, %eax\n"
    "jne .Ldv\n"
    "mov $21, %eax\n"
    "shl $1, %eax\n"
    "mov $42, %ecx\n"
    "cmp %ecx, %eax\n"
    "jne .Ldv\n"
    "mov $42, %eax\n"
    "mov $255, %ecx\n"
    "and %ecx, %eax\n"
    "cmp $42, %eax\n"
    "jne .Ldv\n"
    "mov $298, %eax\n"
    "movzbl %al, %eax\n"
    "cmp $42, %eax\n"
    "jne .Ldv\n"
    "xor %edx, %edx\n"
    "mov q(%rip), %rax\n"
    "cmp $42, %eax\n"
    "jne .Lmv\n"
    "call score\n"
    "cmp $42, %eax\n"
    "jne .Lsc\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea s4(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call strcpy\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea s2(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call strcat\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea s42(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "mov $2, %edx\n"
    "call strncmp\n"
    "cmp $0, %eax\n"
    "jne .Lstr\n"
    "lea buf2(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea sa(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call stpcpy\n"
    "lea buf2(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea sa(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call strcmp\n"
    "cmp $0, %eax\n"
    "jne .Lstr\n"
    "mov $53, %edi\n"
    "call isdigit\n"
    "test %eax, %eax\n"
    "je .Lstr\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "call atoi\n"
    "cmp $42, %eax\n"
    "jne .Lstr\n"
    "lea -16(%rbp), %rax\n"
    "mov %rax, %rdi\n"
    "lea s42(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "mov $3, %edx\n"
    "call memcpy\n"
    "lea -16(%rbp), %rax\n"
    "mov %rax, %rdi\n"
    "mov $52, %esi\n"
    "mov $2, %edx\n"
    "call memchr\n"
    "test %rax, %rax\n"
    "je .Lmc\n"
    "lea -16(%rbp), %rax\n"
    "mov %rax, %rdi\n"
    "lea s42(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "mov $2, %edx\n"
    "call memcmp\n"
    "cmp $0, %eax\n"
    "jne .Lmc\n"
    "lea s42(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "mov $50, %esi\n"
    "call strchr\n"
    "test %rax, %rax\n"
    "je .Lmc\n"
    "lea s42(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "xor %esi, %esi\n"
    "call strchrnul\n"
    "test %rax, %rax\n"
    "je .Lmc\n"
    "mov $65, %edi\n"
    "call tolower\n"
    "cmp $97, %eax\n"
    "jne .Lmc\n"
    "mov $97, %edi\n"
    "call toupper\n"
    "cmp $65, %eax\n"
    "jne .Lmc\n"
    "mov $32, %edi\n"
    "call isspace\n"
    "test %eax, %eax\n"
    "je .Lmc\n"
    "mov $65, %edi\n"
    "call isalpha\n"
    "test %eax, %eax\n"
    "je .Lmc\n"
    "lea -8(%rbp), %rax\n"
    "mov %rax, %rdi\n"
    "xor %esi, %esi\n"
    "mov $4, %edx\n"
    "call memset\n"
    "mov $42, %edi\n"
    "call labs\n"
    "mov %rbp, %rsp\n"
    "pop %rbp\n"
    "and $255, %eax\n"
    "ret\n"
    ".Lim:\n"
    "mov $22, %eax\n"
    "jmp .Lout\n"
    ".Ldv:\n"
    "mov $25, %eax\n"
    "jmp .Lout\n"
    ".Lmv:\n"
    "mov $23, %eax\n"
    "jmp .Lout\n"
    ".Lsc:\n"
    "and $255, %eax\n"
    "jmp .Lout\n"
    ".Lstr:\n"
    "mov $24, %eax\n"
    "jmp .Lout\n"
    ".Lmc:\n"
    "mov $26, %eax\n"
    "jmp .Lout\n"
    ".Lout:\n"
    "mov %rbp, %rsp\n"
    "pop %rbp\n"
    "ret\n"
    ".data\n"
    "buf:\n"
    ".zero 16\n"
    "buf2:\n"
    ".zero 16\n"
    ".rodata\n"
    "sa:\n"
    ".asciz \"SF\"\n"
    "s4:\n"
    ".asciz \"4\"\n"
    "s2:\n"
    ".asciz \"2\"\n"
    "s42:\n"
    ".asciz \"42\"\n"
    "q:\n"
    ".quad 42\n";

static const char lib_c[] =
    "CXC_A2\n"
    "struct Pt {\n"
    "int x;\n"
    "int y;\n"
    "};\n"
    "typedef unsigned long size_t;\n"
    "fn score(){\n"
    "Pt p;\n"
    "Pt* sp;\n"
    "void* vp;\n"
    "void** vpp;\n"
    "long n;\n"
    "unsigned u;\n"
    "size_t z;\n"
    "char* cp;\n"
    "char** pp;\n"
    "p.x=6;\n"
    "p.y=7;\n"
    "sp=&p;\n"
    "vp=sp;\n"
    "vpp=&vp;\n"
    "n=sp->x*sp->y;\n"
    "u=42;\n"
    "z=sizeof(char);\n"
    "if(z!=1){return 1;}\n"
    "z=sizeof(size_t);\n"
    "if(z!=8){return 2;}\n"
    "z=sizeof(void*);\n"
    "if(z!=8){return 3;}\n"
    "z=sizeof(char**);\n"
    "if(z!=8){return 5;}\n"
    "z=sizeof(void**);\n"
    "if(z!=8){return 7;}\n"
    "cp=\"ok\";\n"
    "pp=&cp;\n"
    "if(n!=42){return 4;}\n"
    "if(u!=42){return 6;}\n"
    "return n;\n"
    "}\n";

int main(void) {
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[24];
    int i = 0;
    FILE* f;

    if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o) {
        fprintf(stderr, "as main fail code=%d\n", as_last_fail_code());
        return 1;
    }
    f = fopen("disk/main.o", "wb");
    fwrite(o, 1, on, f);
    fclose(f);

    o = 0;
    on = 0;
    if (cc_compile_obj(lib_c, strlen(lib_c), &o, &on) != 0 || !o) {
        fprintf(stderr, "cc lib fail code=%d\n", cc_last_fail_code());
        return 1;
    }
    f = fopen("disk/lib.o", "wb");
    fwrite(o, 1, on, f);
    fclose(f);

    paths[i++] = "disk/main.o";
    paths[i++] = "disk/lib.o";
    paths[i++] = "disk/atoi.o";
    paths[i++] = "disk/scmp.o";
    paths[i++] = "disk/labs.o";
    paths[i++] = "disk/dig.o";
    paths[i++] = "disk/scat.o";
    paths[i++] = "disk/schr.o";
    paths[i++] = "disk/snul.o";
    paths[i++] = "disk/scpy.o";
    paths[i++] = "disk/stpc.o";
    paths[i++] = "disk/sncmp.o";
    paths[i++] = "disk/mcmp.o";
    paths[i++] = "disk/mchr.o";
    paths[i++] = "disk/tlwr.o";
    paths[i++] = "disk/tupr.o";
    paths[i++] = "disk/ispc.o";
    paths[i++] = "disk/ialp.o";
    paths[i++] = "disk/mset.o";
    paths[i++] = "disk/mcpy.o";
    paths[i++] = "disk/slen.o";

    if (ld_link(paths, i, &elf, &en) != 0 || !elf) {
        fprintf(stderr, "ld fail code=%d\n", ld_last_fail_code());
        return 1;
    }
    printf("SURF repro ld PASS bytes=%zu e_type=%u\n", en, elf[16]);
    return 0;
}
