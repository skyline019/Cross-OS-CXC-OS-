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
        "z=sizeof(char);\n"
        "n=n+z-1;\n"
        "z=sizeof(char*);\n"
        "n=n+z-8;\n"
        "z=sizeof(long);\n"
        "n=n+z-8;\n"
        "z=sizeof(size_t);\n"
        "n=n+z-8;\n"
        "cp=\"x\";\n"
        "z=sizeof(cp);\n"
        "n=n+z-8;\n"
        "return n;\n"
        "}\n";
    unsigned char* o = 0;
    size_t on = 0;
    size_t i;
    if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0) {
        printf("fail %d\n", cc_last_fail_code());
        return 1;
    }
    printf("obj n=%zu\n", on);
    /* Print code-ish bytes: look for leave/ret pairs and preceding ops. */
    for (i = 0; i + 1 < on; i++) {
        if (o[i] == 0xC9 && o[i + 1] == 0xC3) {
            size_t a = i > 24 ? i - 24 : 0;
            size_t b;
            printf("leave/ret at %zu, prior:", i);
            for (b = a; b <= i + 1; b++) {
                printf(" %02x", o[b]);
            }
            printf("\n");
        }
    }
    /* mov rax, [rbp+disp] = 48 8b 85 xx xx xx xx */
    for (i = 0; i + 6 < on; i++) {
        if (o[i] == 0x48 && o[i + 1] == 0x8B && o[i + 2] == 0x85) {
            int disp = (int)(o[i + 3] | (o[i + 4] << 8) | (o[i + 5] << 16) |
                             (o[i + 6] << 24));
            int slot = (-disp) / 8 - 1;
            printf("load_local slot~%d disp=%d at %zu\n", slot, disp, i);
        }
        if (o[i] == 0x48 && o[i + 1] == 0x89 && o[i + 2] == 0x85) {
            int disp = (int)(o[i + 3] | (o[i + 4] << 8) | (o[i + 5] << 16) |
                             (o[i + 6] << 24));
            int slot = (-disp) / 8 - 1;
            printf("store_local slot~%d disp=%d at %zu\n", slot, disp, i);
        }
        if (o[i] == 0x48 && o[i + 1] == 0x8D && o[i + 2] == 0x05) {
            printf("lea_data rip at %zu\n", i);
        }
        if (o[i] == 0x48 && o[i + 1] == 0x8D && o[i + 2] == 0x85) {
            int disp = (int)(o[i + 3] | (o[i + 4] << 8) | (o[i + 5] << 16) |
                             (o[i + 6] << 24));
            int slot = (-disp) / 8 - 1;
            printf("lea_local slot~%d at %zu\n", slot, i);
        }
    }
    return 0;
}
