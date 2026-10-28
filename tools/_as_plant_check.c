#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* short job name stem in plant_src filename -> expected global symbol */
static const struct {
    const char* file;
    const char* sym;
} k_expect[] = {
    {"isdigit.s", "isdigit"},   {"labs.s", "labs"},
    {"abs.s", "abs"},           {"llabs.s", "llabs"},
    {"imaxabs.s", "imaxabs"},   {"isascii.s", "isascii"},
    {"toascii.s", "toascii"},   {"isspace.s", "isspace"},
    {"isalpha.s", "isalpha"},   {"isalnum.s", "isalnum"},
    {"islower.s", "islower"},   {"isupper.s", "isupper"},
    {"isblank.s", "isblank"},   {"isgraph.s", "isgraph"},
    {"isprint.s", "isprint"},   {"isxdigit.s", "isxdigit"},
    {"iscntrl.s", "iscntrl"},   {"tolower.s", "tolower"},
    {"toupper.s", "toupper"},   {"htons.s", "htons"},
    {"htonl.s", "htonl"},       {"ntohs.s", "ntohs"},
    {"ntohl.s", "ntohl"},
    /* M298 batch2 */
    {"ispunct.s", "ispunct"},   {"ffs.s", "ffs"},
    {"bzero.s", "bzero"},       {"bcmp.s", "bcmp"},
    {"strnlen.s", "strnlen"},   {"memchr.s", "memchr"},
    {"strlen.s", "strlen"},     {"strchr.s", "strchr"},
    /* M300 batch3 */
    {"memset.s", "memset"},     {"memcmp.s", "memcmp"},
    {"strcmp.s", "strcmp"},     {"strncmp.s", "strncmp"},
    {"strcpy.s", "strcpy"},     {"stpcpy.s", "stpcpy"},
    {"strcat.s", "strcat"},     {"strchrnul.s", "strchrnul"},
    {"atoi.s", "atoi"},
    /* M300 musl-compat aliases (same ET_REL as primary) */
    {"stpcpy.s", "__stpcpy"},   {"strchrnul.s", "__strchrnul"},
    /* M303 mem/str + bridge */
    {"memrchr.s", "memrchr"},   {"memcpy.s", "memcpy"},
    {"memmove.s", "memmove"},   {"strcspn.s", "strcspn"},
    {"strspn.s", "strspn"},     {"strpbrk.s", "strpbrk"},
    {"strrchr.s", "strrchr"},   {"tcap_bridge_add.s", "tcap_bridge_add"},
    {"memrchr.s", "__memrchr"}, {"memcpy.s", "__memcpy_fwd"},
    /* M304 hard_stubs + strdup */
    {"hard_stubs.s", "malloc"}, {"hard_stubs.s", "free"},
    {"hard_stubs.s", "___errno_location"},
    {"hard_stubs.s", "__stack_chk_fail"},
    {"hard_stubs.s", "__uflow"},
    {"strdup.s", "strdup"},
    /* M305 strtol chain */
    {"strtol.s", "strtol"},   {"intscan.s", "__intscan"},
    {"shgetc.s", "__shgetc"}, {"shgetc.s", "__shlim"},
};

static int expect_known(const char* fname) {
    for (size_t i = 0; i < sizeof(k_expect) / sizeof(k_expect[0]); ++i) {
        if (strcmp(fname, k_expect[i].file) == 0) {
            return 1;
        }
    }
    return 0;
}

static int elf_has_global(const unsigned char* elf, size_t n, const char* want) {
    if (n < 64 || elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' ||
        elf[3] != 'F' || elf[4] != 2 || elf[16] != 1) {
        return 0;
    }
    unsigned long e_shoff = 0;
    unsigned short e_shentsize = 0, e_shnum = 0, e_shstrndx = 0;
    memcpy(&e_shoff, elf + 40, 8);
    memcpy(&e_shentsize, elf + 58, 2);
    memcpy(&e_shnum, elf + 60, 2);
    memcpy(&e_shstrndx, elf + 62, 2);
    if (e_shentsize < 64 || e_shnum == 0 ||
        e_shoff + (unsigned long)e_shnum * e_shentsize > n) {
        return 0;
    }
    const unsigned char* shstr =
        elf + e_shoff + (unsigned long)e_shstrndx * e_shentsize;
    unsigned long shstr_off = 0, shstr_size = 0;
    memcpy(&shstr_off, shstr + 24, 8);
    memcpy(&shstr_size, shstr + 32, 8);
    if (shstr_off + shstr_size > n) {
        return 0;
    }
    unsigned long sym_off = 0, sym_size = 0, sym_entsize = 0;
    unsigned int str_link = 0;
    for (unsigned short i = 0; i < e_shnum; ++i) {
        const unsigned char* sh = elf + e_shoff + (unsigned long)i * e_shentsize;
        unsigned int name = 0, type = 0, link = 0;
        unsigned long offset = 0, size_sz = 0, entsize = 0;
        memcpy(&name, sh + 0, 4);
        memcpy(&type, sh + 4, 4);
        memcpy(&offset, sh + 24, 8);
        memcpy(&size_sz, sh + 32, 8);
        memcpy(&link, sh + 40, 4); /* Elf64_Shdr.sh_link is u32 */
        memcpy(&entsize, sh + 56, 8);
        if (type != 2 /* SHT_SYMTAB */ || name >= shstr_size) {
            continue;
        }
        const char* nm = (const char*)(elf + shstr_off + name);
        if (strcmp(nm, ".symtab") != 0) {
            continue;
        }
        sym_off = offset;
        sym_size = size_sz;
        sym_entsize = entsize ? entsize : 24;
        str_link = link;
        break;
    }
    if (sym_off == 0 || str_link >= e_shnum ||
        sym_off + sym_size > n || sym_entsize < 24) {
        return 0;
    }
    const unsigned char* strsh =
        elf + e_shoff + (unsigned long)str_link * e_shentsize;
    unsigned long stroff = 0, strsz = 0;
    memcpy(&stroff, strsh + 24, 8);
    memcpy(&strsz, strsh + 32, 8);
    if (stroff + strsz > n) {
        return 0;
    }
    for (unsigned long off = 0; off + sym_entsize <= sym_size; off += sym_entsize) {
        const unsigned char* sym = elf + sym_off + off;
        unsigned int st_name = 0;
        unsigned char st_info = 0;
        unsigned short st_shndx = 0;
        memcpy(&st_name, sym + 0, 4);
        st_info = sym[4];
        memcpy(&st_shndx, sym + 6, 2);
        if (st_name == 0 || st_name >= strsz || st_shndx == 0) {
            continue;
        }
        /* STB_GLOBAL=1 or STB_WEAK=2 */
        if ((st_info >> 4) != 1 && (st_info >> 4) != 2) {
            continue;
        }
        const char* nm = (const char*)(elf + stroff + st_name);
        if (strcmp(nm, want) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    DIR* d = opendir("user/plant_src");
    struct dirent* e;
    int fails = 0;
    int n = 0;
    if (!d) {
        perror("opendir");
        return 2;
    }
    while ((e = readdir(d)) != 0) {
        size_t L = strlen(e->d_name);
        char path[256];
        FILE* f;
        long sz;
        char* buf;
        unsigned char* elf = 0;
        size_t eln = 0;
        const char* want;
        if (L < 3 || strcmp(e->d_name + L - 2, ".s") != 0) {
            continue;
        }
        if (!expect_known(e->d_name)) {
            fprintf(stderr, "FAIL unexpected plant file %s\n", e->d_name);
            fails++;
            continue;
        }
        if (L > 200) {
            fprintf(stderr, "FAIL name too long %s\n", e->d_name);
            fails++;
            continue;
        }
        snprintf(path, sizeof path, "user/plant_src/%s", e->d_name);
        f = fopen(path, "rb");
        if (!f) {
            perror(path);
            fails++;
            continue;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            fails++;
            continue;
        }
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (char*)malloc((size_t)sz + 1);
        if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            fclose(f);
            free(buf);
            fails++;
            continue;
        }
        fclose(f);
        buf[sz] = 0;
        if (as_compile(buf, (size_t)sz, &elf, &eln) != 0 || !elf || eln < 64) {
            fprintf(stderr, "FAIL as %s code=%d\n", e->d_name, as_last_fail_code());
            fails++;
            free(buf);
            continue;
        }
        for (size_t i = 0; i < sizeof(k_expect) / sizeof(k_expect[0]); ++i) {
            if (strcmp(e->d_name, k_expect[i].file) != 0) {
                continue;
            }
            want = k_expect[i].sym;
            if (!elf_has_global(elf, eln, want)) {
                fprintf(stderr, "FAIL sym %s missing global %s\n", e->d_name,
                        want);
                fails++;
            } else {
                printf("ok %s -> %s (%zu)\n", e->d_name, want, eln);
                n++;
            }
        }
        /* as_compile owns elf buffer (arena); do not free. */
        free(buf);
    }
    closedir(d);
    printf("pass=%d fail=%d expect=%zu\n", n, fails,
           sizeof(k_expect) / sizeof(k_expect[0]));
    if (n != (int)(sizeof(k_expect) / sizeof(k_expect[0]))) {
        fails++;
    }
    return fails ? 1 : 0;
}
