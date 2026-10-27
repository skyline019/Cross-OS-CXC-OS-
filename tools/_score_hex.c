#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>

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
        "return n;\n"
        "}\n";
    unsigned char* o = 0;
    size_t on = 0;
    size_t i;
    /* Find .text content - for ET_REL, code starts after ELF headers.
       Simpler: search for prologue 55 48 89 e5 */
    if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0) {
        printf("fail %d\n", cc_last_fail_code());
        return 1;
    }
    for (i = 0; i + 3 < on; i++) {
        if (o[i] == 0x55 && o[i + 1] == 0x48 && o[i + 2] == 0x89 &&
            o[i + 3] == 0xe5) {
            size_t j;
            printf("prologue at %zu\n", i);
            for (j = i; j < on && j < i + 200; j++) {
                printf("%02x ", o[j]);
                if ((j - i + 1) % 16 == 0) {
                    printf("\n");
                }
            }
            printf("\n");
            break;
        }
    }
    return 0;
}
