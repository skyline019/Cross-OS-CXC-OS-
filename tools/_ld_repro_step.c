#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int do_as(const char* in_s, const char* out_o) {
    FILE* f;
    char* buf;
    long n;
    unsigned char* elf = 0;
    size_t en = 0;
    f = fopen(in_s, "rb");
    if (!f) {
        perror(in_s);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char*)malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return 1;
    }
    fclose(f);
    buf[n] = 0;
    if (as_compile(buf, (size_t)n, &elf, &en) != 0 || !elf) {
        fprintf(stderr, "as_compile fail code=%d\n", as_last_fail_code());
        return 1;
    }
    f = fopen(out_o, "wb");
    if (!f || fwrite(elf, 1, en, f) != en) {
        fprintf(stderr, "write %s fail\n", out_o);
        return 1;
    }
    fclose(f);
    free(buf);
    return 0;
}

static int do_import(int argc, char** argv) {
    static CcEmit em;
    static unsigned char code[65536];
    static unsigned char data[16384];
    static unsigned char ro[16384];
    int i;
    memset(&em, 0, sizeof(em));
    em.code = code;
    em.data = data;
    em.ro = ro;
    em.code_cap = 65536;
    em.data_cap = 16384;
    em.ro_cap = 16384;
    cc_reloc_reset();
    cc_und_reset();
    for (i = 0; i < argc; i++) {
        em.fail_code = 0;
        em.failed = 0;
        printf("import %s\n", argv[i]);
        if (cc_import_elf(&em, argv[i]) != 0) {
            printf("FAIL import %s code=%d\n", argv[i], em.fail_code);
            return 1;
        }
        printf("  ok code=%zu data=%zu ro=%zu bss=%zu nexp=%d nund=%d\n",
               em.code_len, em.data_len, em.ro_len, em.bss_len, em.nexports,
               0);
    }
    if (cc_und_resolve(&em) != 0) {
        printf("FAIL und_resolve code=%d\n", em.fail_code);
        return 1;
    }
    printf("import+und OK\n");
    return 0;
}

static int do_ld(int argc, char** argv) {
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[128];
    int i;
    if (argc > 128) {
        printf("too many paths\n");
        return 1;
    }
    for (i = 0; i < argc; i++) {
        paths[i] = argv[i];
    }
    if (ld_link(paths, argc, &elf, &en) != 0 || !elf) {
        printf("FAIL ld_link code=%d npaths=%d\n", ld_last_fail_code(), argc);
        return 1;
    }
    printf("ld_link OK len=%zu\n", en);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: step as in.s out.o | import paths... | ld paths...\n");
        return 1;
    }
    if (strcmp(argv[1], "as") == 0 && argc == 4) {
        return do_as(argv[2], argv[3]);
    }
    if (strcmp(argv[1], "import") == 0) {
        return do_import(argc - 2, argv + 2);
    }
    if (strcmp(argv[1], "ld") == 0) {
        return do_ld(argc - 2, argv + 2);
    }
    fprintf(stderr, "bad mode\n");
    return 1;
}
