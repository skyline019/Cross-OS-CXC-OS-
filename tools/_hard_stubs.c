/* Freestanding stubs for deep musl HARD closure (M250–M252).
 * Non-zero .data seeds keep abs relocs off .bss (elflink RO/RW path). */
static long errno_cell = 1;

long *___errno_location(void) {
    return &errno_cell;
}

void __stack_chk_fail(void) {
    for (;;) {
        __asm__ volatile("ud2");
    }
}

int __uflow(void *f) {
    (void)f;
    return -1;
}

/* Seeded so section is PROGBITS .data, not NOBITS .bss. */
static char heap[512] = {1};
static unsigned long heap_off = 16; /* 16-byte aligned bump start */

void *malloc(unsigned long n) {
    unsigned long a = (n + 15ull) & ~15ull;
    if (heap_off + a > sizeof(heap)) {
        return 0;
    }
    void *p = heap + heap_off;
    heap_off += a;
    return p;
}

void free(void *p) {
    (void)p;
}
