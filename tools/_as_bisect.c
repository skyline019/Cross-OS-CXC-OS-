#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int try_as(const char* s) {
    unsigned char* o = 0;
    size_t on = 0;
    int rc = as_compile(s, strlen(s), &o, &on);
    printf("rc=%d fail=%d n=%zu :: %.60s%s\n", rc, as_last_fail_code(), on, s,
           strlen(s) > 60 ? "..." : "");
    return rc;
}

int main(void) {
    char* full = 0;
    size_t n = 0;
    FILE* f = fopen("tools/_probe_main.s", "rb");
    if (!f) {
        return 2;
    }
    fseek(f, 0, SEEK_END);
    n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    full = (char*)malloc(n + 1);
    fread(full, 1, n, f);
    fclose(f);
    full[n] = 0;

    /* Drop trailing data/rodata and append minimal to isolate text parse. */
    {
        char* cut = strstr(full, "\n.data\n");
        char buf[8192];
        size_t tn;
        if (!cut) {
            puts("no .data");
            return 3;
        }
        tn = (size_t)(cut - full);
        memcpy(buf, full, tn);
        buf[tn] = 0;
        strcat(buf,
               "\n.data\nbuf:\n.zero 16\nbuf2:\n.zero 16\n"
               ".rodata\nsa:\n.asciz \"CL\"\n"
               "s4:\n.asciz \"4\"\n"
               "s2:\n.asciz \"2\"\n"
               "s42:\n.asciz \"42\"\n"
               "q:\n.quad 42\n");
        try_as(buf);
    }

    /* Progressive line trim from end of text. */
    {
        char* cut = strstr(full, "\n.data\n");
        char text[8192];
        size_t tn = (size_t)(cut - full);
        char* lines[256];
        int lc = 0;
        char* p;
        int i;
        memcpy(text, full, tn);
        text[tn] = 0;
        p = strtok(text, "\n");
        while (p && lc < 256) {
            lines[lc++] = p;
            p = strtok(0, "\n");
        }
        for (i = 10; i <= lc; i++) {
            char body[8192];
            int j;
            size_t off = 0;
            body[0] = 0;
            for (j = 0; j < i; j++) {
                size_t L = strlen(lines[j]);
                memcpy(body + off, lines[j], L);
                off += L;
                body[off++] = '\n';
            }
            /* ensure labels exist for early branches */
            memcpy(body + off,
                   ".Lim:\nret\n.Ldv:\nret\n.Lmv:\nret\n.Lsc:\nret\n"
                   ".Lstr:\nret\n.Lmc:\nret\n.Lout:\nret\n"
                   ".data\nbuf:\n.zero 16\nbuf2:\n.zero 16\n"
                   ".rodata\nsa:\n.asciz \"CL\"\n"
                   "s4:\n.asciz \"4\"\ns2:\n.asciz \"2\"\n"
                   "s42:\n.asciz \"42\"\nq:\n.quad 42\n",
                   0);
            /* strlen of suffix */
            {
                const char* suf =
                    ".Lim:\nret\n.Ldv:\nret\n.Lmv:\nret\n.Lsc:\nret\n"
                    ".Lstr:\nret\n.Lmc:\nret\n.Lout:\nret\n"
                    ".data\nbuf:\n.zero 16\nbuf2:\n.zero 16\n"
                    ".rodata\nsa:\n.asciz \"CL\"\n"
                    "s4:\n.asciz \"4\"\ns2:\n.asciz \"2\"\n"
                    "s42:\n.asciz \"42\"\nq:\n.quad 42\n";
                strcpy(body + off, suf);
            }
            printf("lines=%d ", i);
            if (try_as(body) != 0) {
                printf("FAIL at adding line: %s\n", lines[i - 1]);
                break;
            }
        }
    }
    free(full);
    return 0;
}
