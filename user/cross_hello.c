/* Freestanding user program for M46: host gcc → ELF64 ET_EXEC on ESP. */

void _start(void) {
    static const char msg[] = "USER_CROSS_ELF_PASS\n";
    register long rax __asm__("rax") = 1; /* SYS_write */
    register long rdi __asm__("rdi") = 1;
    register long rsi __asm__("rsi") = (long)msg;
    register long rdx __asm__("rdx") = (long)(sizeof(msg) - 1);
    __asm__ volatile("int $0x80"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx)
                     : "memory", "rcx", "r11");
    rax = 60; /* SYS_exit */
    rdi = 0;
    __asm__ volatile("int $0x80"
                     : "+r"(rax)
                     : "r"(rdi)
                     : "memory", "rcx", "r11");
    for (;;) {
    }
}
