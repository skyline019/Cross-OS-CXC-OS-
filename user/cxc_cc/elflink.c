/* Import ET_REL: place .text, apply R_X86_64_PC32, bind imports (M108). */
#include "cxc_cc/cc.h"

#ifdef CC_HOST
#include <stdio.h>
#endif

enum {
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_RELA = 4,
    SHF_EXECINSTR = 4,
    R_X86_64_PC32 = 2
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
    unsigned rela_link = 0;
    int have_rela = 0;
    size_t text_base = 0;
    int placed = 0;
    if (!em || !path || cc_read_source(path, (char*)buf, sizeof(buf), &n) !=
                            0) {
        return -1;
    }
    if (n < 64 || buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' ||
        buf[3] != 'F' || buf[4] != 2) {
        return -1;
    }
    if (u16(buf + 16) != 1) {
        return -1; /* ET_REL */
    }
    shoff = (unsigned)u64(buf + 40);
    shentsize = u16(buf + 58);
    shnum = u16(buf + 60);
    if (shentsize < 64 || shnum < 2 || shnum > 32) {
        return -1;
    }
    if (shoff > n || n - shoff < (size_t)shnum * shentsize) {
        return -1;
    }
    for (i = 0; i < shnum; i++) {
        const unsigned char* sh = buf + shoff + i * shentsize;
        unsigned typ = u32(sh + 4);
        unsigned flags = (unsigned)u64(sh + 8);
        unsigned link = u32(sh + 40);
        unsigned info = u32(sh + 44);
        unsigned entsz = (unsigned)u64(sh + 56);
        unsigned off = (unsigned)u64(sh + 24);
        unsigned sz = (unsigned)u64(sh + 32);
        if (typ == SHT_SYMTAB) {
            symoff = off;
            syment = entsz ? entsz : 24;
            if (syment < 24) {
                return -1;
            }
            symn = sz / syment;
            if (link >= shnum) {
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
            rela_link = link;
            have_rela = 1;
            if (info != 0 && info < shnum) {
                /* prefer rela that targets exec PROGBITS */
                (void)info;
            }
        }
    }
    if (symn == 0 || symn > 64 || stroff >= n || strsz > n - stroff) {
        return -1;
    }
    if (symoff >= n || n - symoff < (size_t)symn * syment) {
        return -1;
    }
    if (text_sz == 0 || text_sz > 2048 || text_off > n ||
        n - text_off < text_sz) {
        return -1;
    }
    if (have_rela) {
        if (rela_entsz < 24 || rela_link >= shnum || rela_sz > 4096 ||
            rela_sz % rela_entsz != 0 || rela_off > n ||
            n - rela_off < rela_sz) {
            return -1;
        }
    }

    /* Place whole .text once if any imported GLOBAL FUNC lives there. */
    for (i = 1; i < symn; i++) {
        const unsigned char* sy = buf + symoff + i * syment;
        unsigned name_off = u32(sy);
        unsigned char info = sy[4];
        unsigned shndx = u16(sy + 6);
        unsigned bind = info >> 4;
        unsigned type = info & 0xf;
        const char* nm;
        size_t nl = 0;
        char ident[CC_IDENT_MAX + 1];
        int fi;
        if (bind != 1 || type != 2 || shndx != text_i) {
            continue;
        }
        if (name_off >= strsz) {
            return -1;
        }
        nm = (const char*)(buf + stroff + name_off);
        while (nl < CC_IDENT_MAX && nm[nl] != 0) {
            ident[nl] = nm[nl];
            nl++;
        }
        ident[nl] = 0;
        if (nl == 0) {
            continue;
        }
        fi = cc_fn_find(em, ident);
        if (fi < 0) {
            continue;
        }
        if (em->fns[fi].defined) {
            return -1;
        }
        if (!placed) {
            text_base = em->code_len;
            cc_emit_bytes(em, buf + text_off, text_sz);
            if (em->failed) {
                return -1;
            }
            if (have_rela) {
                unsigned ri;
                unsigned nrela = rela_sz / rela_entsz;
                if (nrela > 64) {
                    return -1;
                }
                for (ri = 0; ri < nrela; ri++) {
                    const unsigned char* re =
                        buf + rela_off + ri * rela_entsz;
                    unsigned long long r_off = u64(re);
                    unsigned long long r_info = u64(re + 8);
                    long long r_add = (long long)u64(re + 16);
                    unsigned r_sym = (unsigned)(r_info >> 32);
                    unsigned r_type = (unsigned)(r_info & 0xffffffffu);
                    const unsigned char* tsy;
                    unsigned t_shndx;
                    unsigned long long t_val;
                    size_t p_at;
                    size_t s_at;
                    long long disp;
                    if (r_type != R_X86_64_PC32) {
                        return -1;
                    }
                    if (r_off + 4 > text_sz || r_sym == 0 || r_sym >= symn) {
                        return -1;
                    }
                    tsy = buf + symoff + r_sym * syment;
                    t_shndx = u16(tsy + 6);
                    t_val = u64(tsy + 8);
                    if (t_shndx != text_i || t_val > text_sz) {
                        return -1;
                    }
                    p_at = text_base + (size_t)r_off;
                    s_at = text_base + (size_t)t_val;
                    disp = (long long)s_at + r_add - (long long)p_at;
                    if (disp < -0x80000000LL || disp > 0x7fffffffLL) {
                        return -1;
                    }
                    w32(em->code + p_at, (unsigned)(int)disp);
                }
            }
            placed = 1;
        }
        {
            unsigned long long value = u64(sy + 8);
            unsigned long long size = u64(sy + 16);
            if (value > text_sz || size == 0 || size > text_sz - value) {
                return -1;
            }
            em->fns[fi].entry = text_base + (size_t)value;
            em->fns[fi].defined = 1;
            em->fns[fi].sysv = 1;
        }
    }
    return em->failed ? -1 : 0;
}
