/* musl-subset CRT: read argc/argv from user RSP before any C prologue. */
#include "unistd.h"

extern int main(int argc, char** argv);

__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile(
        "movq %%rsp, %%rdi\n\t"
        "movl (%%rdi), %%esi\n\t"
        "leaq 8(%%rdi), %%rdx\n\t"
        "movl %%esi, %%edi\n\t"
        "movq %%rdx, %%rsi\n\t"
        "call main\n\t"
        "movl %%eax, %%edi\n\t"
        "call _exit\n\t"
        :
        :
        : "memory");
}
