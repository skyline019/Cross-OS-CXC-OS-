#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char* src =
        "CXC_A2\n"
        "fn main(){\n"
        "int z;\n"
        "z=sizeof(char);\n"
        "return z;\n"
        "}\n";
    unsigned char* elf = 0;
    size_t n = 0;
    size_t i;
    if (cc_compile(src, strlen(src), &elf, &n) != 0) {
        printf("compile fail %d\n", cc_last_fail_code());
        return 1;
    }
    for (i = 0; i + 5 < n; i++) {
        if (elf[i] == 0xb8) {
            unsigned imm = (unsigned)elf[i + 1] | ((unsigned)elf[i + 2] << 8) |
                           ((unsigned)elf[i + 3] << 16) |
                           ((unsigned)elf[i + 4] << 24);
            if (imm == 1 || imm == 8) {
                printf("mov eax,%u at %zu\n", imm, i);
            }
        }
    }
    return 0;
}
