/* M134/M142/M152/M162: fixed-seed toolchain chaos — mutate .c/.s/.o/rsp/phdr. */
#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <string.h>

enum { CHAOS_SEED = 0xC2C6U, CHAOS_ROUNDS = 96 };

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

static int chaos_multisection_hdr(void) {
    /* Corrupt ET_REL section headers / e_machine on a multi-section object. */
    static const char src[] =
        ".text\n.globl main\nmain:\n"
        "lea s(%rip), %rax\n"
        "ret\n"
        ".rodata\ns:\n.asciz \"CX\"\n";
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char pristine[8192];
    size_t pn = 0;
    unsigned char blob[8192];
    unsigned r;
    const char* paths[1];
    unsigned char* elf = 0;
    size_t en = 0;

    if (as_compile(src, strlen(src), &o, &on) != 0 || !o || on < 64 ||
        on > sizeof(pristine)) {
        fprintf(stderr, "FAIL chaos: baseline multi-sec as\n");
        return -1;
    }
    memcpy(pristine, o, on);
    pn = on;
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        size_t n = pn;
        memcpy(blob, pristine, pn);
        /* Prefer header/section-table region when present. */
        if (n > 64 && (rng_next() & 1u)) {
            size_t at = 16 + (rng_next() % (n > 256 ? 240u : (unsigned)(n - 16)));
            blob[at] ^= (unsigned char)(1u + (rng_next() & 0x3fu));
            if (n > 58) {
                blob[18] ^= (unsigned char)(rng_next() & 0xffu); /* e_machine */
            }
        } else if (rng_next() & 1u) {
            n = mutate_trunc(blob, n);
        } else {
            mutate_flip(blob, n);
        }
        if (write_file("disk/chaos_msh.o", blob, n) != 0) {
            return -1;
        }
        paths[0] = "disk/chaos_msh.o";
        elf = 0;
        en = 0;
        (void)ld_link(paths, 1, &elf, &en);
    }
    return 0;
}

static int chaos_musl_trunc(void) {
    /* Truncate a planted musl-shaped .o if present; else synthesize. */
    static const char syn[] =
        ".text\n.globl atoi\natoi:\nmov $0, %eax\nret\n";
    unsigned char buf[4096];
    size_t n = 0;
    FILE* f;
    unsigned r;
    const char* paths[1];
    unsigned char* elf = 0;
    size_t en = 0;
    unsigned char* o = 0;
    size_t on = 0;

    f = fopen("disk/atoi.o", "rb");
    if (f) {
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
    }
    if (n < 64) {
        if (as_compile(syn, strlen(syn), &o, &on) != 0 || !o || on < 64 ||
            on > sizeof(buf)) {
            fprintf(stderr, "FAIL chaos: musl trunc baseline\n");
            return -1;
        }
        memcpy(buf, o, on);
        n = on;
    }
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        unsigned char mut[4096];
        size_t mn = n;
        if (mn > sizeof(mut)) {
            mn = sizeof(mut);
        }
        memcpy(mut, buf, mn);
        mn = mutate_trunc(mut, mn);
        if (write_file("disk/chaos_musl_trunc.o", mut, mn) != 0) {
            return -1;
        }
        paths[0] = "disk/chaos_musl_trunc.o";
        elf = 0;
        en = 0;
        (void)ld_link(paths, 1, &elf, &en);
    }
    return 0;
}

static int chaos_phdr_flags(void) {
    /* M162: mutate ET_EXEC phdr count/flags after a good link. */
    unsigned char* o = 0;
    size_t on = 0;
    unsigned char* elf = 0;
    size_t en = 0;
    unsigned r;
    const char* paths[1];
    const char* src =
        ".text\n.globl main\nmain:\n"
        "push %rbp\n"
        "mov %rsp, %rbp\n"
        "sub $16, %rsp\n"
        "lea -8(%rbp), %rax\n"
        "mov $42, %eax\n"
        "mov %rbp, %rsp\n"
        "pop %rbp\n"
        "ret\n";
    if (as_compile(src, strlen(src), &o, &on) != 0 || !o) {
        fprintf(stderr, "FAIL chaos: phdr baseline as\n");
        return -1;
    }
    if (write_file("disk/chaos_phdr.o", o, on) != 0) {
        return -1;
    }
    paths[0] = "disk/chaos_phdr.o";
    if (ld_link(paths, 1, &elf, &en) != 0 || !elf || en < 128 ||
        elf[16] != 2) {
        fprintf(stderr, "FAIL chaos: phdr baseline ld\n");
        return -1;
    }
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        unsigned char blob[8192];
        size_t n = en < sizeof(blob) ? en : sizeof(blob);
        unsigned mode = rng_next() % 4u;
        memcpy(blob, elf, n);
        if (mode == 0 && n > 56) {
            blob[56] = (unsigned char)(1 + (rng_next() % 8u)); /* e_phnum */
        } else if (mode == 1 && n > 64 + 4) {
            blob[64 + 4] ^= 0xFF; /* first phdr p_flags */
        } else if (mode == 2 && n > 64 + 56 + 4) {
            blob[64 + 56 + 4] ^= 0x03; /* second phdr flags if present */
        } else if (n > 80) {
            blob[64 + (rng_next() % 40u)] ^= (unsigned char)(1 + (rng_next() % 255u));
        }
        if (write_file("disk/chaos_phdr_mut.elf", blob, n) != 0) {
            return -1;
        }
        /* Loader not invoked here; ensure mutator itself stays deterministic. */
        (void)blob;
    }
    return 0;
}

static int chaos_rsp_garbage(void) {
    /* M152: corrupt multi-line link.rsp content / truncate paths. */
    unsigned char* o = 0;
    size_t on = 0;
    unsigned r;
    const char* main_s =
        ".text\n.globl main\nmain:\nmov $42, %eax\nret\n";
    if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o) {
        fprintf(stderr, "FAIL chaos: rsp garbage baseline\n");
        return -1;
    }
    if (write_file("disk/chaos_rg_main.o", o, on) != 0) {
        return -1;
    }
    for (r = 0; r < CHAOS_ROUNDS; r++) {
        char rsp[256];
        size_t n;
        unsigned mode = rng_next() % 3u;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[4];
        int npaths;
        if (mode == 0) {
            /* Empty / whitespace-only path slots via truncated names. */
            paths[0] = "disk/chaos_rg_main.o";
            paths[1] = "";
            npaths = 2;
        } else if (mode == 1) {
            paths[0] = "disk/nope-chaos.o";
            npaths = 1;
        } else {
            n = 0;
            while (n < sizeof(rsp) - 1) {
                rsp[n] = (char)(1 + (rng_next() % 90));
                n++;
            }
            rsp[n] = 0;
            if (write_file("disk/chaos_rsp.bin", rsp, n) != 0) {
                return -1;
            }
            paths[0] = "disk/chaos_rg_main.o";
            paths[1] = "disk/chaos_rsp.bin";
            npaths = 2;
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
        ".text\n.globl main\nmain:\n"
        "push %rbp\n"
        "mov %rsp, %rbp\n"
        "mov $21, %eax\n"
        "shl $1, %eax\n"
        "mov $42, %ecx\n"
        "cmp %ecx, %eax\n"
        "mov q(%rip), %rax\n"
        "mov %rbp, %rsp\n"
        "pop %rbp\n"
        "ret\n"
        ".rodata\nq:\n.quad 42\n";
    const char* lib_s =
        ".text\n.globl bump\nbump:\nmov $7, %eax\nret\n";
    {
        const char* src =
            "CXC_A2\n"
            "typedef unsigned long size_t;\n"
            "fn main(){\n"
            "char* cp;\n"
            "char** pp;\n"
            "void* vp;\n"
            "unsigned u;\n"
            "cp=\"x\";\n"
            "pp=&cp;\n"
            "vp=0;\n"
            "u=42;\n"
            "return u;\n"
            "}\n";
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
    if (chaos_multisection_hdr() != 0) {
        fprintf(stderr, "FAIL chaos: multisection hdr\n");
        fails++;
    }
    if (chaos_musl_trunc() != 0) {
        fprintf(stderr, "FAIL chaos: musl trunc\n");
        fails++;
    }
    if (chaos_rsp_garbage() != 0) {
        fprintf(stderr, "FAIL chaos: rsp garbage\n");
        fails++;
    }
    if (chaos_phdr_flags() != 0) {
        fprintf(stderr, "FAIL chaos: phdr flags\n");
        fails++;
    }
    if (fails != 0) {
        fprintf(stderr, "cc_chaos_test: %d failures\n", fails);
        return 1;
    }
    printf("cc_chaos_test: PASS seed=%u rounds=%u\n", CHAOS_SEED, CHAOS_ROUNDS);
    return 0;
}
