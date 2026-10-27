/* M261: musl-static mini assembler — AT&T subset → ET_REL.
 * Evidence: musl-gcc -static -Os -s; nm/readelf on prebuilt/as.
 * Accepts: .text .globl LABEL: mov $imm,%eax/%rax ret nop endbr64
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

enum { MAX_SRC = 65536, MAX_CODE = 4096, MAX_SYM = 64, MAX_RELA = 32 };

static char src[MAX_SRC];
static unsigned char code[MAX_CODE];
static unsigned code_len;

struct Sym {
    char name[32];
    unsigned offset;
    int defined;
    int global;
};
static struct Sym syms[MAX_SYM];
static int nsym;

static int streq(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static int sym_find(const char* name) {
    int i;
    for (i = 0; i < nsym; ++i) {
        if (streq(syms[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int sym_add(const char* name) {
    int i = sym_find(name);
    int n;
    if (i >= 0) {
        return i;
    }
    if (nsym >= MAX_SYM) {
        return -1;
    }
    i = nsym++;
    for (n = 0; n < 31 && name[n]; ++n) {
        syms[i].name[n] = name[n];
    }
    syms[i].name[n] = 0;
    syms[i].offset = 0;
    syms[i].defined = 0;
    syms[i].global = 0;
    return i;
}

static void skip_ws(const char** p) {
    while (**p == ' ' || **p == '\t' || **p == '\r') {
        ++(*p);
    }
}

static int parse_ident(const char** p, char* out, int cap) {
    int n = 0;
    skip_ws(p);
    /* Allow leading '%' for AT&T registers (%eax / %rax / %edi). */
    if (!(**p == '_' || **p == '%' || (**p >= 'a' && **p <= 'z') ||
          (**p >= 'A' && **p <= 'Z') || **p == '.')) {
        return 0;
    }
    while (n + 1 < cap &&
           (**p == '_' || **p == '.' || **p == '%' ||
            (**p >= 'a' && **p <= 'z') || (**p >= 'A' && **p <= 'Z') ||
            (**p >= '0' && **p <= '9'))) {
        out[n++] = *(*p)++;
    }
    out[n] = 0;
    return n > 0;
}

static int parse_imm(const char** p, long* out) {
    long v = 0;
    int neg = 0;
    skip_ws(p);
    if (**p != '$') {
        return 0;
    }
    ++(*p);
    if (**p == '-') {
        neg = 1;
        ++(*p);
    }
    if (**p < '0' || **p > '9') {
        return 0;
    }
    while (**p >= '0' && **p <= '9') {
        v = v * 10 + (**p - '0');
        ++(*p);
    }
    *out = neg ? -v : v;
    return 1;
}

static void emit(unsigned char b) {
    if (code_len < MAX_CODE) {
        code[code_len++] = b;
    }
}

static void emit_u32(unsigned v) {
    emit((unsigned char)(v & 0xff));
    emit((unsigned char)((v >> 8) & 0xff));
    emit((unsigned char)((v >> 16) & 0xff));
    emit((unsigned char)((v >> 24) & 0xff));
}

static int assemble(void) {
    const char* p = src;
    char tok[64];
    while (*p) {
        skip_ws(&p);
        if (*p == '\n' || *p == 0) {
            if (*p == '\n') {
                ++p;
            }
            continue;
        }
        if (*p == '#') {
            while (*p && *p != '\n') {
                ++p;
            }
            continue;
        }
        if (!parse_ident(&p, tok, (int)sizeof(tok))) {
            return 1;
        }
        skip_ws(&p);
        if (*p == ':') {
            int si = sym_add(tok);
            ++p;
            if (si < 0) {
                return 2;
            }
            syms[si].offset = code_len;
            syms[si].defined = 1;
            continue;
        }
        if (streq(tok, ".text")) {
            continue;
        }
        if (streq(tok, ".globl") || streq(tok, ".global")) {
            char name[32];
            if (!parse_ident(&p, name, (int)sizeof(name))) {
                return 3;
            }
            {
                int si = sym_add(name);
                if (si < 0) {
                    return 4;
                }
                syms[si].global = 1;
            }
            continue;
        }
        if (streq(tok, "endbr64")) {
            emit(0xf3);
            emit(0x0f);
            emit(0x1e);
            emit(0xfa);
            continue;
        }
        if (streq(tok, "nop")) {
            emit(0x90);
            continue;
        }
        if (streq(tok, "ret")) {
            emit(0xc3);
            continue;
        }
        if (streq(tok, "mov")) {
            long imm = 0;
            char reg[16];
            if (!parse_imm(&p, &imm)) {
                return 5;
            }
            skip_ws(&p);
            if (*p != ',') {
                return 6;
            }
            ++p;
            if (!parse_ident(&p, reg, (int)sizeof(reg))) {
                return 7;
            }
            if (streq(reg, "%eax")) {
                emit(0xb8);
                emit_u32((unsigned)(uint32_t)imm);
            } else if (streq(reg, "%rax")) {
                emit(0x48);
                emit(0xc7);
                emit(0xc0);
                emit_u32((unsigned)(uint32_t)imm);
            } else if (streq(reg, "%edi")) {
                emit(0xbf);
                emit_u32((unsigned)(uint32_t)imm);
            } else {
                return 8;
            }
            continue;
        }
        return 9;
    }
    return 0;
}

static void wr16(unsigned char* p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void wr32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void wr64(unsigned char* p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static int write_elf(const char* path) {
    /* Minimal ET_REL: null, .text, .shstrtab, .symtab, .strtab */
    unsigned char shstr[] = "\0.text\0.shstrtab\0.symtab\0.strtab";
    unsigned str_len = 1;
    char strtab[1024];
    unsigned char symtab[16 + MAX_SYM * 24];
    unsigned sym_bytes = 24; /* null symbol */
    int i;
    unsigned text_off, shstr_off, sym_off, str_off, sh_off;
    unsigned char ehdr[64];
    unsigned char shdr[5 * 64];
    int fd;
    strtab[0] = 0;
    for (i = 0; i < 24; ++i) {
        symtab[i] = 0;
    }
    for (i = 0; i < nsym; ++i) {
        unsigned name_off = str_len;
        int n = 0;
        while (syms[i].name[n] && str_len + 1 < sizeof(strtab)) {
            strtab[str_len++] = syms[i].name[n++];
        }
        strtab[str_len++] = 0;
        {
            unsigned char* s = symtab + sym_bytes;
            int k;
            for (k = 0; k < 24; ++k) {
                s[k] = 0;
            }
            wr32(s + 0, name_off);
            s[4] = (unsigned char)(syms[i].defined ? (syms[i].global ? 0x10 : 0x00) : 0x10);
            /* STB_GLOBAL<<4 | STT_FUNC, or STB_LOCAL */
            if (syms[i].defined) {
                s[4] = (unsigned char)((syms[i].global ? 1 : 0) << 4 | 2);
                s[5] = 0;
                wr16(s + 6, 1); /* .text */
                wr64(s + 8, syms[i].offset);
                wr64(s + 16, 0);
            } else {
                s[4] = (unsigned char)(1 << 4);
                wr16(s + 6, 0);
            }
            sym_bytes += 24;
        }
        (void)name_off;
    }
    text_off = 64;
    shstr_off = text_off + code_len;
    sym_off = shstr_off + (unsigned)sizeof(shstr);
    str_off = sym_off + sym_bytes;
    sh_off = (str_off + str_len + 7u) & ~7u;

    for (i = 0; i < 64; ++i) {
        ehdr[i] = 0;
    }
    ehdr[0] = 0x7f;
    ehdr[1] = 'E';
    ehdr[2] = 'L';
    ehdr[3] = 'F';
    ehdr[4] = 2;
    ehdr[5] = 1;
    ehdr[6] = 1;
    wr16(ehdr + 16, 1); /* ET_REL */
    wr16(ehdr + 18, 62);
    wr32(ehdr + 20, 1);
    wr64(ehdr + 32, sh_off);
    wr16(ehdr + 40, 64);
    wr16(ehdr + 46, 64);
    wr16(ehdr + 48, 5);
    wr16(ehdr + 50, 2); /* shstrndx */

    for (i = 0; i < (int)sizeof(shdr); ++i) {
        shdr[i] = 0;
    }
    /* [1] .text */
    wr32(shdr + 64 + 0, 1);
    wr32(shdr + 64 + 4, 1); /* SHT_PROGBITS */
    wr64(shdr + 64 + 8, 6); /* ALLOC|EXEC */
    wr64(shdr + 64 + 24, text_off);
    wr64(shdr + 64 + 32, code_len);
    wr64(shdr + 64 + 48, 16);
    /* [2] .shstrtab */
    wr32(shdr + 128 + 0, 7);
    wr32(shdr + 128 + 4, 3);
    wr64(shdr + 128 + 24, shstr_off);
    wr64(shdr + 128 + 32, sizeof(shstr));
    wr64(shdr + 128 + 48, 1);
    /* [3] .symtab */
    wr32(shdr + 192 + 0, 17);
    wr32(shdr + 192 + 4, 2);
    wr64(shdr + 192 + 24, sym_off);
    wr64(shdr + 192 + 32, sym_bytes);
    wr32(shdr + 192 + 40, 4); /* link .strtab */
    wr32(shdr + 192 + 44, 1); /* one local (null) */
    wr64(shdr + 192 + 48, 8);
    wr64(shdr + 192 + 56, 24);
    /* [4] .strtab */
    wr32(shdr + 256 + 0, 25);
    wr32(shdr + 256 + 4, 3);
    wr64(shdr + 256 + 24, str_off);
    wr64(shdr + 256 + 32, str_len);
    wr64(shdr + 256 + 48, 1);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return 20;
    }
    if (write(fd, ehdr, 64) != 64) {
        close(fd);
        return 21;
    }
    if (write(fd, code, code_len) != (ssize_t)code_len) {
        close(fd);
        return 22;
    }
    if (write(fd, shstr, sizeof(shstr)) != (ssize_t)sizeof(shstr)) {
        close(fd);
        return 23;
    }
    if (write(fd, symtab, sym_bytes) != (ssize_t)sym_bytes) {
        close(fd);
        return 24;
    }
    if (write(fd, strtab, str_len) != (ssize_t)str_len) {
        close(fd);
        return 25;
    }
    {
        unsigned pad = sh_off - (str_off + str_len);
        unsigned char z = 0;
        while (pad--) {
            if (write(fd, &z, 1) != 1) {
                close(fd);
                return 26;
            }
        }
    }
    if (write(fd, shdr, sizeof(shdr)) != (ssize_t)sizeof(shdr)) {
        close(fd);
        return 27;
    }
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    int fd;
    ssize_t n;
    int rc;
    if (argc < 3) {
        return 50;
    }
    fd = open(argv[1], O_RDONLY, 0);
    if (fd < 0) {
        return 51;
    }
    n = read(fd, src, sizeof(src) - 1);
    close(fd);
    if (n <= 0) {
        return 52;
    }
    src[n] = 0;
    rc = assemble();
    if (rc != 0) {
        return 60 + rc;
    }
    rc = write_elf(argv[2]);
    if (rc != 0) {
        return rc;
    }
    return 0;
}
