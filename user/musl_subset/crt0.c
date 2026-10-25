/* musl-subset CRT: ELF _start → main → _exit (not full musl crt1). */
#include "unistd.h"

extern int main(int argc, char** argv);

void _start(void) {
    static char arg0[] = "musl";
    static char* argv[] = {arg0, 0};
    const int status = main(1, argv);
    _exit(status);
}
