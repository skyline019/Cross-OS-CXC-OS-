/* One-off: assemble onbox probe main.s excerpt from stdin. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int as_compile(const char* src, size_t n, unsigned char** out, size_t* out_n);

int main(void) {
    char* buf = 0;
    size_t cap = 0;
    size_t n = 0;
    int c;
    unsigned char* o = 0;
    size_t on = 0;
    while ((c = getchar()) != EOF) {
        if (n + 1 >= cap) {
            size_t nc = cap ? cap * 2 : 4096;
            char* nb = (char*)realloc(buf, nc);
            if (!nb) {
                return 2;
            }
            buf = nb;
            cap = nc;
        }
        buf[n++] = (char)c;
    }
    if (as_compile(buf, n, &o, &on) != 0) {
        fprintf(stderr, "as_compile FAIL n=%zu\n", n);
        return 1;
    }
    fprintf(stderr, "as_compile OK on=%zu\n", on);
    return 0;
}
