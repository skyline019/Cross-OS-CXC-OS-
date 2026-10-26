/* M134: fixed-seed toolchain chaos — mutate .c/.s/.o/rsp; reject, no crash. */
#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <string.h>

enum { CHAOS_SEED = 0xC134U, CHAOS_ROUNDS = 64 };

static unsigned g_rng;

static void rng_seed(unsigned s) {
    g_rng = s ? s : 1u;
}

static unsigned rng_next(void) {
    /* LCG: reproducible across hosts. */
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static size_t mutate_trunc(unsigned char* buf, size_t n) {
    size_t keep;
    (void)buf;
    if (n < 2) {
        return n;
    }
    keep = 1 + (rng_next() % (n - 1));
    return keep;
}

static void mutate_flip(unsigned char* buf, size_t n) {
    size_t i;
    unsigned flips = 1 + (rng_next() % 4);
    if (n == 0) {
        return;
    }
    while (flips--) {
        i = rng_next() % n;
        buf[i] ^= (unsigned char)(1u + (rng_next() & 0x7fu));
    }
}

static int write_file(const char* path, const void* p, size_t n) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    if (n > 0 && fwrite(p, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f) != 0 ? -1 : 0;
}

static int chaos_src_c(void) {
    static const char good[] =
        "CXC_A2\n"
        "fn main(){\n"
        "return 42;\n"
        "}\n";
    unsigned char buf[512];
    const size_t gn = strlen(good);
    unsigned char* elf = 0;
    size_t en = 0;
    unsigned r;
    if (gn >= sizeof(buf)) {
        return -1;
    }
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        size_t n = gn;
        memcpy(buf, good, gn);
        if (rng_next() & 1u) {
            n = mutate_trunc(buf, n);
        } else {
            mutate_flip(buf, n);
        }
        elf = 0;
        en = 0;
        /* Must not crash; corrupted input should reject (or rarely still parse). */
        (void)cc_compile((const char*)buf, n, &elf, &en);
    }
    return 0;
}

static int chaos_src_s(void) {
    static const char good[] =
        ".text\n"
        ".globl main\n"
        "main:\n"
        "mov $42, %eax\n"
        "ret\n";
    unsigned char buf[512];
    const size_t gn = strlen(good);
    unsigned char* elf = 0;
    size_t en = 0;
    unsigned r;
    if (gn >= sizeof(buf)) {
        return -1;
    }
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        size_t n = gn;
        memcpy(buf, good, gn);
        if (rng_next() & 1u) {
            n = mutate_trunc(buf, n);
        } else {
            mutate_flip(buf, n);
        }
        elf = 0;
        en = 0;
        (void)as_compile((const char*)buf, n, &elf, &en);
    }
    return 0;
}

static int chaos_obj_ld(void) {
    static const char main_s[] =
        ".text\n.globl main\nmain:\nmov $42, %eax\nret\n";
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    unsigned char pristine[4096];
    size_t pn = 0;
    unsigned char blob[4096];
    unsigned r;
    const char* paths[1];

    if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o || on < 64 ||
        on > sizeof(pristine)) {
        fprintf(stderr, "FAIL chaos: baseline as main\n");
        return -1;
    }
    memcpy(pristine, o, on);
    pn = on;
    if (write_file("disk/chaos_main.o", pristine, pn) != 0) {
        return -1;
    }

    for (r = 0; r < CHAOS_ROUNDS; r++) {
        size_t n = pn;
        memcpy(blob, pristine, pn);
        if (rng_next() & 1u) {
            n = mutate_trunc(blob, n);
        } else {
            mutate_flip(blob, n);
        }
        if (write_file("disk/chaos_mut.o", blob, n) != 0) {
            return -1;
        }
        paths[0] = "disk/chaos_mut.o";
        elf = 0;
        en = 0;
        (void)ld_link(paths, 1, &elf, &en);
    }
    return 0;
}

static int chaos_rsp_order(void) {
    /* Scramble/truncate rsp text; ld_link via written objs + garbage paths. */
    static const char lines[] =
        "disk/chaos_main.o\n"
        "disk/chaos_lib.o\n"
        "disk/nope.o\n"
        "disk/trunc.o\n";
    unsigned char buf[256];
    const size_t gn = strlen(lines);
    unsigned r;
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[4];
    int npaths;
    size_t i;
    size_t start;

    if (gn >= sizeof(buf)) {
        return -1;
    }
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        size_t n = gn;
        memcpy(buf, lines, gn + 1);
        if (rng_next() & 1u) {
            n = mutate_trunc(buf, n);
            buf[n] = 0;
        } else {
            mutate_flip(buf, n);
            buf[n] = 0;
        }
        /* Parse up to 4 non-empty lines into paths (may be garbage). */
        npaths = 0;
        i = 0;
        while (i < n && npaths < 4) {
            while (i < n && (buf[i] == '\n' || buf[i] == ' ')) {
                i++;
            }
            if (i >= n) {
                break;
            }
            start = i;
            while (i < n && buf[i] != '\n') {
                i++;
            }
            buf[i] = 0;
            paths[npaths++] = (const char*)(buf + start);
            if (i < n) {
                i++;
            }
        }
        if (npaths < 1) {
            continue;
        }
        elf = 0;
        en = 0;
        (void)ld_link(paths, npaths, &elf, &en);
    }
    return 0;
}

static int success_samples(void) {
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    const char* paths[2];
    const char* main_s =
        ".text\n.globl main\nmain:\nmov $42, %eax\nret\n";
    const char* lib_s =
        ".text\n.globl bump\nbump:\nmov $7, %eax\nret\n";
    {
        const char* src = "CXC_A2\nfn main(){\nreturn 42;\n}\n";
        if (cc_compile(src, strlen(src), &elf, &en) != 0 || !elf || en < 64) {
            fprintf(stderr, "FAIL chaos ok: cc compile\n");
            return -1;
        }
    }
    if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o) {
        fprintf(stderr, "FAIL chaos ok: as main\n");
        return -1;
    }
    if (write_file("disk/chaos_main.o", o, on) != 0) {
        return -1;
    }
    o = 0;
    on = 0;
    if (as_compile(lib_s, strlen(lib_s), &o, &on) != 0 || !o) {
        fprintf(stderr, "FAIL chaos ok: as lib\n");
        return -1;
    }
    if (write_file("disk/chaos_lib.o", o, on) != 0) {
        return -1;
    }
    paths[0] = "disk/chaos_main.o";
    paths[1] = "disk/chaos_lib.o";
    if (ld_link(paths, 2, &elf, &en) != 0 || !elf || elf[16] != 2) {
        fprintf(stderr, "FAIL chaos ok: ld_link\n");
        return -1;
    }
    return 0;
}

int main(void) {
    int fails = 0;
    rng_seed(CHAOS_SEED);
    if (success_samples() != 0) {
        fails++;
    }
    if (chaos_src_c() != 0) {
        fprintf(stderr, "FAIL chaos: src.c\n");
        fails++;
    }
    if (chaos_src_s() != 0) {
        fprintf(stderr, "FAIL chaos: src.s\n");
        fails++;
    }
    if (chaos_obj_ld() != 0) {
        fprintf(stderr, "FAIL chaos: obj/ld\n");
        fails++;
    }
    if (chaos_rsp_order() != 0) {
        fprintf(stderr, "FAIL chaos: rsp\n");
        fails++;
    }
    if (fails != 0) {
        fprintf(stderr, "cc_chaos_test: %d failures\n", fails);
        return 1;
    }
    printf("cc_chaos_test: PASS seed=%u rounds=%u\n", CHAOS_SEED, CHAOS_ROUNDS);
    return 0;
}
