#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char* o = 0;
    size_t n = 0;
    size_t i;
    const char* s = "CXC_A2\nfn bump(x){\nreturn x+7;\n}\n";
    if (cc_compile_obj(s, strlen(s), &o, &n) != 0 || !o) {
        puts("fail compile_obj");
        return 1;
    }
    printf("len=%zu etype=%u\n", n, (unsigned)o[16]);
    for (i = 64; i < n && i < 64 + 96; i++) {
        printf("%02x", o[i]);
        if ((i - 64) % 16 == 15) {
            puts("");
        } else {
            putchar(' ');
        }
    }
    puts("");
    return 0;
}
