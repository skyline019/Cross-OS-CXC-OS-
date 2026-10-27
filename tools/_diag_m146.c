#define CC_HOST 1
#include <stdio.h>
#include <string.h>
#include "cxc_cc/cc.h"

int main(void) {
    const char* main_s =
        ".text\n.globl main\nmain:\n"
        "push %rbp\n"
        "mov %rsp, %rbp\n"
        "lea s(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "call atoi\n"
        "leave\n"
        "ret\n"
        ".rodata\ns:\n.asciz \"42\"\n";
    unsigned char* o = 0;
    size_t n = 0;
    int rc = as_compile(main_s, strlen(main_s), &o, &n);
    printf("rc=%d n=%zu as_fail=%d\n", rc, n, as_last_fail_code());
    return rc != 0;
}
