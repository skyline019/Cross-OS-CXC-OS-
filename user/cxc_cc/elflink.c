/* Import ET_REL: place .text, PC32/PLT32, cross-#lib UND (M109). */
#include "cxc_cc/cc.h"

#ifdef CC_HOST
#include <stdio.h>
#endif

enum {
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_RELA = 4,
    SHF_EXECINSTR = 4,
    R_X86_64_PC32 = 2,
    R_X86_64_PLT32 = 4,
    CC_FAIL_ELF = 20,
    CC_FAIL_UND = 21,
    CC_FAIL_RELOC = 22,
    CC_FAIL_DUP = 23
};

static unsigned u16(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned u32(const unsigned char* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

static unsigned long long u64(const unsigned char* p) {
    unsigned long long v = 0;
    int i;
    for (i = 0; i < 8; i++) {
        v |= (unsigned long long)p[i] << (8 * i);
    }
    return v;
}

static void w32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static int name_eq(const char* a, const char* b) {
    size_t i = 0;
    while (i <= CC_IDENT_MAX) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            return 1;
        }
        i++;
    }
    return 0;
}

static int copy_ident(char* dst, const char* src, size_t strsz, unsigned off) {
    size_t nl = 0;
    if (off >= strsz) {
        return -1;
    }
    while (nl < CC_IDENT_MAX && src[off + nl] != 0) {
        if (off + nl >= strsz) {
            return -1;
        }
        dst[nl] = src[off + nl];
        nl++;
    }
    dst[nl] = 0;
    return nl == 0 ? -1 : 0;
}

static int export_add(CcEmit* em, const char* name, size_t entry) {
    int i;
    if (em->nexports >= CC_EXPORT_MAX) {
        return -1;
    }
    for (i = 0; i < em->nexports; i++) {
        if (name_eq(em->exports[i].name, name)) {
            return -1;
        }
    }
    i = 0;
    while (name[i] != 0 && i < CC_IDENT_MAX) {
        em->exports[em->nexports].name[i] = name[i];
        i++;
    }
    em->exports[em->nexports].name[i] = 0;
    em->exports[em->nexports].entry = entry;
    em->nexports++;
    return 0;
}

static int resolve_addr(CcEmit* em, const char* name, size_t* out) {
    int i;
    for (i = 0; i < em->nexports; i++) {
        if (name_eq(em->exports[i].name, name)) {
            *out = em->exports[i].entry;
            return 0;
        }
    }
    i = cc_fn_find(em, name);
    if (i >= 0 && em->fns[i].defined) {
        *out = em->fns[i].entry;
        return 0;
    }
    return -1;
}

int cc_import_elf(CcEmit* em, const char* path) {
    static unsigned char buf[CC_SRC_MAX];
    size_t n = 0;
    unsigned shoff;
    unsigned shentsize;
    unsigned shnum;
    unsigned i;
    unsigned symoff = 0;
    unsigned syment = 24;
    unsigned symn = 0;
    unsigned stroff = 0;
    unsigned strsz = 0;
    unsigned text_i = 0;
    unsigned text_off = 0;
    unsigned text_sz = 0;
    unsigned rela_off = 0;
    unsigned rela_sz = 0;
    unsigned rela_entsz = 24;
    int have_rela = 0;
    size_t text_base;
    const char* strbase;
    if (!em || !path || cc_read_source(path, (char*)buf, sizeof(buf), &n) !=
                            0) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (n < 64 || buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' ||
        buf[3] != 'F' || buf[4] != 2) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (u16(buf + 16) != 1) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    shoff = (unsigned)u64(buf + 40);
    shentsize = u16(buf + 58);
    shnum = u16(buf + 60);
    if (shentsize < 64 || shnum < 2 || shnum > 32) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (shoff > n || n - shoff < (size_t)shnum * shentsize) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    for (i = 0; i < shnum; i++) {
        const unsigned char* sh = buf + shoff + i * shentsize;
        unsigned typ = u32(sh + 4);
        unsigned flags = (unsigned)u64(sh + 8);
        unsigned link = u32(sh + 40);
        unsigned entsz = (unsigned)u64(sh + 56);
        unsigned off = (unsigned)u64(sh + 24);
        unsigned sz = (unsigned)u64(sh + 32);
        if (typ == SHT_SYMTAB) {
            symoff = off;
            syment = entsz ? entsz : 24;
            if (syment < 24) {
                em->fail_code = CC_FAIL_ELF;
                return -1;
            }
            symn = sz / syment;
            if (link >= shnum) {
                em->fail_code = CC_FAIL_ELF;
                return -1;
            }
            {
                const unsigned char* strsh = buf + shoff + link * shentsize;
                stroff = (unsigned)u64(strsh + 24);
                strsz = (unsigned)u64(strsh + 32);
            }
        } else if (typ == SHT_PROGBITS && (flags & SHF_EXECINSTR) &&
                   text_sz == 0) {
            text_i = i;
            text_off = off;
            text_sz = sz;
        } else if (typ == SHT_RELA) {
            rela_off = off;
            rela_sz = sz;
            rela_entsz = entsz ? entsz : 24;
            have_rela = 1;
        }
    }
    if (symn == 0 || symn > 64 || stroff >= n || strsz > n - stroff) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (symoff >= n || n - symoff < (size_t)symn * syment) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (text_sz == 0 || text_sz > 2048 || text_off > n ||
        n - text_off < text_sz) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (have_rela) {
        if (rela_entsz < 24 || rela_sz > 4096 || rela_sz % rela_entsz != 0 ||
            rela_off > n || n - rela_off < rela_sz) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
    }

    /* Always place listed #lib .text (exports feed later UND resolves). */
    text_base = em->code_len;
    cc_emit_bytes(em, buf + text_off, text_sz);
    if (em->failed) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    strbase = (const char*)(buf + stroff);

    if (have_rela) {
        unsigned ri;
        unsigned nrela = rela_sz / rela_entsz;
        if (nrela > 64) {
            em->fail_code = CC_FAIL_RELOC;
            return -1;
        }
        for (ri = 0; ri < nrela; ri++) {
            const unsigned char* re = buf + rela_off + ri * rela_entsz;
            unsigned long long r_off = u64(re);
            unsigned long long r_info = u64(re + 8);
            long long r_add = (long long)u64(re + 16);
            unsigned r_sym = (unsigned)(r_info >> 32);
            unsigned r_type = (unsigned)(r_info & 0xffffffffu);
            const unsigned char* tsy;
            unsigned t_shndx;
            unsigned long long t_val;
            unsigned t_name_off;
            char ident[CC_IDENT_MAX + 1];
            size_t p_at;
            size_t s_at;
            long long disp;
            if (r_type != R_X86_64_PC32 && r_type != R_X86_64_PLT32) {
                em->fail_code = CC_FAIL_RELOC;
                return -1;
            }
            if (r_off + 4 > text_sz || r_sym == 0 || r_sym >= symn) {
                em->fail_code = CC_FAIL_RELOC;
                return -1;
            }
            tsy = buf + symoff + r_sym * syment;
            t_shndx = u16(tsy + 6);
            t_val = u64(tsy + 8);
            t_name_off = u32(tsy);
            p_at = text_base + (size_t)r_off;
            if (t_shndx == text_i) {
                if (t_val > text_sz) {
                    em->fail_code = CC_FAIL_RELOC;
                    return -1;
                }
                s_at = text_base + (size_t)t_val;
            } else if (t_shndx == 0) {
                if (copy_ident(ident, strbase, strsz, t_name_off) != 0) {
                    em->fail_code = CC_FAIL_UND;
                    return -1;
                }
                if (resolve_addr(em, ident, &s_at) != 0) {
                    em->fail_code = CC_FAIL_UND;
                    return -1;
                }
            } else {
                em->fail_code = CC_FAIL_UND;
                return -1;
            }
            disp = (long long)s_at + r_add - (long long)p_at;
            if (disp < -0x80000000LL || disp > 0x7fffffffLL) {
                em->fail_code = CC_FAIL_RELOC;
                return -1;
            }
            w32(em->code + p_at, (unsigned)(int)disp);
        }
    }

    for (i = 1; i < symn; i++) {
        const unsigned char* sy = buf + symoff + i * syment;
        unsigned name_off = u32(sy);
        unsigned char info = sy[4];
        unsigned shndx = u16(sy + 6);
        unsigned long long value = u64(sy + 8);
        unsigned long long size = u64(sy + 16);
        unsigned bind = info >> 4;
        unsigned type = info & 0xf;
        char ident[CC_IDENT_MAX + 1];
        int fi;
        size_t entry;
        if (bind != 1 || type != 2 || shndx != text_i) {
            continue;
        }
        if (copy_ident(ident, strbase, strsz, name_off) != 0) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
        if (value > text_sz || size == 0 || size > text_sz - value) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
        entry = text_base + (size_t)value;
        if (export_add(em, ident, entry) != 0) {
            em->fail_code = CC_FAIL_DUP;
            return -1;
        }
        fi = cc_fn_find(em, ident);
        if (fi < 0) {
            continue;
        }
        if (em->fns[fi].defined) {
            em->fail_code = CC_FAIL_DUP;
            return -1;
        }
        em->fns[fi].entry = entry;
        em->fns[fi].defined = 1;
        em->fns[fi].sysv = 1;
    }
    return em->failed ? -1 : 0;
}
