/* Thin sbrk-backed malloc: each alloc grows the break (no free reuse). */
#include "mini_libc/stdlib.h"

void* malloc(size_t size) {
    if (size == 0) {
        return 0;
    }
    const size_t need = (size + 15u) & ~(size_t)15u;
    void* const block = sbrk((intptr_t)need);
    if (block == (void*)(intptr_t)-1) {
        return 0;
    }
    return block;
}

void free(void* ptr) {
    (void)ptr;
}
