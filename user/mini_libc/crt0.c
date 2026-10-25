/* Thin CRT: ELF _start → main(argc, argv) → _exit (not musl). */
#include "mini_libc/unistd.h"

extern int main(int argc, char** argv);

void _start(void) {
    static char arg0[] = "crt";
    static char* argv[] = {arg0, 0};
    const int status = main(1, argv);
    _exit(status);
}
