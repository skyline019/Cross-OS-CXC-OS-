/* Thin CRT: ELF _start → main(argc, argv) from execve stack → _exit. */
#include "mini_libc/unistd.h"

extern int main(int argc, char** argv);

/* Kernel seeds [rsp]=argc, [rsp+8]=argv[0]… (same layout as Linux).
 * Match musl_subset/crt0: snapshot rsp first; %% escapes GCC operands. */
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
