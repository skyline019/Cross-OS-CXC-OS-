#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <string.h>

/* Minimal main that calls strtol("42") — exercises deep closure. */
static const char main_s[] =
    ".text\n"
    ".globl main\n"
    "main:\n"
    "push %rbp\n"
    "mov %rsp, %rbp\n"
    "lea s42(%rip), %rdi\n"
    "xor %esi, %esi\n"
    "mov $10, %edx\n"
    "call strtol\n"
    "leave\n"
    "ret\n"
    ".rodata\n"
    "s42:\n"
    ".asciz \"42\"\n";

int main(void) {
    unsigned char* main_o = 0;
    size_t main_n = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[16];
    int n = 0;
    FILE* f;

    if (as_compile(main_s, sizeof(main_s) - 1, &main_o, &main_n) != 0 ||
        !main_o) {
        printf("FAIL as_compile code=%d\n", as_last_fail_code());
        return 1;
    }
    f = fopen("/tmp/hard_main.o", "wb");
    if (!f || fwrite(main_o, 1, main_n, f) != main_n) {
        printf("FAIL write main.o\n");
        return 1;
    }
    fclose(f);

    paths[n++] = "/tmp/hard_main.o";
    paths[n++] = "user/prebuilt/musl_strtol.o";
    paths[n++] = "user/prebuilt/musl_intscan.o";
    paths[n++] = "user/prebuilt/musl_shgetc.o";
    paths[n++] = "user/prebuilt/musl_memrchr.o";
    paths[n++] = "user/prebuilt/hard_stubs.o";
    paths[n++] = "user/prebuilt/musl_memcpy.o";
    paths[n++] = "user/prebuilt/musl_strlen.o";
    paths[n++] = "user/prebuilt/musl_memset.o";
    paths[n++] = "user/prebuilt/musl_strchrnul.o";

    if (ld_link(paths, n, &elf, &en) != 0 || !elf) {
        printf("FAIL ld_link code=%d\n", ld_last_fail_code());
        return 1;
    }
    printf("ld_link OK elf_len=%zu phnum=%u\n", en,
           (unsigned)elf[56] | ((unsigned)elf[57] << 8));
    return 0;
}
