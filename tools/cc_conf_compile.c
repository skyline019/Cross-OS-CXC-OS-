/* Host helper: compile one CXC_A2 source file to a raw ET_EXEC blob on stdout.
 * Used by tools/cc_conformance.py (M287). */
#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    FILE* f;
    char* src = 0;
    long n = 0;
    unsigned char* elf = 0;
    size_t elf_n = 0;
    int code;

    if (argc != 2) {
        fprintf(stderr, "usage: cc_conf_compile <src.c>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "open failed\n");
        return 2;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 2;
    }
    n = ftell(f);
    if (n < 0 || n > CC_SRC_MAX) {
        fclose(f);
        return 2;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 2;
    }
    src = (char*)malloc((size_t)n + 1);
    if (!src) {
        fclose(f);
        return 2;
    }
    if (fread(src, 1, (size_t)n, f) != (size_t)n) {
        free(src);
        fclose(f);
        return 2;
    }
    fclose(f);
    src[n] = 0;
    if (cc_compile(src, (size_t)n, &elf, &elf_n) != 0 || !elf || elf_n < 64) {
        code = cc_last_fail_code();
        fprintf(stderr, "COMPILE_FAIL:%d\n", code);
        free(src);
        return 1;
    }
    if (fwrite(elf, 1, elf_n, stdout) != elf_n) {
        free(src);
        return 2;
    }
    free(src);
    return 0;
}
