/* Import ET_REL: multi .text.* + .rodata; size0/keep-first (M121). */
#include "cxc_cc/cc.h"

#ifdef CC_HOST
#include <stdio.h>
#endif

enum {
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_RELA = 4,
    SHF_ALLOC = 2,
    SHF_EXECINSTR = 4,
    R_X86_64_PC32 = 2,
    R_X86_64_PLT32 = 4,
    R_X86_64_32 = 10,
    R_X86_64_GOTPCREL = 41,
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

static void w64(unsigned char* p, unsigned long long v) {
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (unsigned char)(v >> (8 * i));
    }
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

/* Allocate/reuse an 8-byte GOT slot holding abs VA of symbol at s_at. */
static int got_slot(CcEmit* em, const char* name, size_t s_at, size_t* out) {
    int i;
    size_t slot;
    unsigned char z[8];
    unsigned long long absva;
    for (i = 0; i < em->ngots; i++) {
        if (name_eq(em->gots[i].name, name)) {
            *out = em->gots[i].slot;
            return 0;
        }
    }
    if (em->ngots >= CC_GOT_MAX) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        z[i] = 0;
    }
    slot = em->code_len;
    cc_emit_bytes(em, z, 8);
    if (em->failed) {
        return -1;
    }
    absva = (unsigned long long)CC_LOAD_VA + (unsigned long long)s_at;
    w64(em->code + slot, absva);
    i = 0;
    while (name[i] != 0 && i < CC_IDENT_MAX) {
        em->gots[em->ngots].name[i] = name[i];
        i++;
    }
    em->gots[em->ngots].name[i] = 0;
    em->gots[em->ngots].slot = slot;
    em->ngots++;
    *out = slot;
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
    /* Reject truncated names (musl __isdigit_l etc.). */
    if (nl == CC_IDENT_MAX && off + nl < strsz && src[off + nl] != 0) {
        return -1;
    }
    return nl == 0 ? -1 : 0;
}

/* Returns 1 if newly added, 0 if name already exported (keep first), -1 on OOM. */
static int export_add(CcEmit* em, const char* name, size_t entry) {
    int i;
    if (em->nexports >= CC_EXPORT_MAX) {
        return -1;
    }
    for (i = 0; i < em->nexports; i++) {
        if (name_eq(em->exports[i].name, name)) {
            return 0;
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
    return 1;
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

/* M125: defer cross-#lib UND until all objects exported. */
typedef struct CcUndPend {
    size_t p_at;
    long long addend;
    unsigned r_type;
    char name[CC_IDENT_MAX + 1];
} CcUndPend;

static CcUndPend g_unds[CC_UND_PEND_MAX];
static int g_nunds;

void cc_und_reset(void) {
    g_nunds = 0;
}

static int und_pend_add(size_t p_at, const char* name, long long addend,
                        unsigned r_type) {
    int i = 0;
    if (g_nunds >= CC_UND_PEND_MAX || !name || name[0] == 0) {
        return -1;
    }
    g_unds[g_nunds].p_at = p_at;
    g_unds[g_nunds].addend = addend;
    g_unds[g_nunds].r_type = r_type;
    while (name[i] != 0 && i < CC_IDENT_MAX) {
        g_unds[g_nunds].name[i] = name[i];
        i++;
    }
    g_unds[g_nunds].name[i] = 0;
    if (name[i] != 0) {
        return -1;
    }
    g_nunds++;
    return 0;
}

static int patch_at(CcEmit* em, size_t p_at, size_t s_at, long long r_add,
                    unsigned r_type, const char* ident) {
    long long disp;
    unsigned long long absva;
    char got_name[CC_IDENT_MAX + 1];
    if (r_type == R_X86_64_32) {
        absva = (unsigned long long)CC_LOAD_VA + (unsigned long long)s_at +
                (unsigned long long)r_add;
        if (absva > 0xffffffffULL) {
            return -1;
        }
        w32(em->code + p_at, (unsigned)absva);
        return 0;
    }
    if (r_type == R_X86_64_GOTPCREL) {
        size_t g_at;
        int i = 0;
        if (ident && ident[0] != 0) {
            while (ident[i] != 0 && i < CC_IDENT_MAX) {
                got_name[i] = ident[i];
                i++;
            }
            got_name[i] = 0;
        } else {
            got_name[0] = '@';
            got_name[1] = 'L';
            got_name[2] = 0;
        }
        if (got_slot(em, got_name, s_at, &g_at) != 0) {
            return -1;
        }
        disp = (long long)g_at + r_add - (long long)p_at;
        if (disp < -0x80000000LL || disp > 0x7fffffffLL) {
            return -1;
        }
        w32(em->code + p_at, (unsigned)(int)disp);
        return 0;
    }
    disp = (long long)s_at + r_add - (long long)p_at;
    if (disp < -0x80000000LL || disp > 0x7fffffffLL) {
        return -1;
    }
    w32(em->code + p_at, (unsigned)(int)disp);
    return 0;
}

int cc_und_resolve(CcEmit* em) {
    int i;
    if (!em) {
        return -1;
    }
    for (i = 0; i < g_nunds; i++) {
        size_t s_at = 0;
        if (resolve_addr(em, g_unds[i].name, &s_at) != 0) {
            em->fail_code = CC_FAIL_UND;
            return -1;
        }
        if (patch_at(em, g_unds[i].p_at, s_at, g_unds[i].addend,
                     g_unds[i].r_type, g_unds[i].name) != 0) {
            em->fail_code = CC_FAIL_RELOC;
            return -1;
        }
    }
    g_nunds = 0;
    return 0;
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
    unsigned tex_off[CC_TEXT_SEC_MAX];
    unsigned tex_sz[CC_TEXT_SEC_MAX];
    size_t tex_base[CC_TEXT_SEC_MAX];
    int sec_tex[64];
    int ntex = 0;
    unsigned rodata_i = 0;
    unsigned rodata_off = 0;
    unsigned rodata_sz = 0;
    int have_rodata = 0;
    unsigned rela_off[CC_RELA_SEC_MAX];
    unsigned rela_sz[CC_RELA_SEC_MAX];
    unsigned rela_entsz[CC_RELA_SEC_MAX];
    unsigned rela_info[CC_RELA_SEC_MAX];
    int nrela_sec = 0;
    size_t rodata_base = 0;
    const char* strbase;
    int si;
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
    if (shentsize < 64 || shnum < 2 || shnum > 64) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (shoff > n || n - shoff < (size_t)shnum * shentsize) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    for (si = 0; si < 64; si++) {
        sec_tex[si] = -1;
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
        } else if (typ == SHT_PROGBITS && (flags & SHF_EXECINSTR) && sz > 0) {
            if (ntex >= CC_TEXT_SEC_MAX || i >= 64) {
                em->fail_code = CC_FAIL_ELF;
                return -1;
            }
            tex_off[ntex] = off;
            tex_sz[ntex] = sz;
            sec_tex[i] = ntex;
            ntex++;
        } else if (typ == SHT_PROGBITS && (flags & SHF_ALLOC) &&
                   !(flags & SHF_EXECINSTR) && sz > 0 && !have_rodata) {
            rodata_i = i;
            rodata_off = off;
            rodata_sz = sz;
            have_rodata = 1;
        } else if (typ == SHT_RELA) {
            if (nrela_sec >= CC_RELA_SEC_MAX) {
                em->fail_code = CC_FAIL_ELF;
                return -1;
            }
            rela_off[nrela_sec] = off;
            rela_sz[nrela_sec] = sz;
            rela_entsz[nrela_sec] = entsz ? entsz : 24;
            rela_info[nrela_sec] = info;
            nrela_sec++;
        }
    }
    if (symn == 0 || symn > 128 || stroff >= n || strsz > n - stroff) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (symoff >= n || n - symoff < (size_t)symn * syment) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    if (ntex < 1) {
        em->fail_code = CC_FAIL_ELF;
        return -1;
    }
    for (si = 0; si < ntex; si++) {
        if (tex_sz[si] > 4096 || tex_off[si] > n ||
            n - tex_off[si] < tex_sz[si]) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
    }
    if (have_rodata) {
        if (rodata_sz > 1024 || rodata_off > n || n - rodata_off < rodata_sz) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
    }
    for (si = 0; si < nrela_sec; si++) {
        unsigned entsz = rela_entsz[si];
        unsigned rsz = rela_sz[si];
        unsigned roff = rela_off[si];
        if (entsz < 24 || rsz > 4096 || rsz % entsz != 0 || roff > n ||
            n - roff < rsz) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
    }

    /* Place all non-empty EXECINSTR sections, then .rodata. */
    for (si = 0; si < ntex; si++) {
        tex_base[si] = em->code_len;
        cc_emit_bytes(em, buf + tex_off[si], tex_sz[si]);
        if (em->failed) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
    }
    if (have_rodata) {
        rodata_base = em->code_len;
        cc_emit_bytes(em, buf + rodata_off, rodata_sz);
        if (em->failed) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
    }
    strbase = (const char*)(buf + stroff);

    for (si = 0; si < nrela_sec; si++) {
        unsigned ri;
        unsigned entsz = rela_entsz[si];
        unsigned nrela = rela_sz[si] / entsz;
        int apply_slot;
        unsigned apply_sz;
        size_t apply_base;
        if (nrela > 64) {
            em->fail_code = CC_FAIL_RELOC;
            return -1;
        }
        if (rela_info[si] >= 64 || sec_tex[rela_info[si]] < 0) {
            em->fail_code = CC_FAIL_RELOC;
            return -1;
        }
        apply_slot = sec_tex[rela_info[si]];
        apply_base = tex_base[apply_slot];
        apply_sz = tex_sz[apply_slot];
        for (ri = 0; ri < nrela; ri++) {
            const unsigned char* re =
                buf + rela_off[si] + ri * entsz;
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
            int tslot;
            if (r_type != R_X86_64_PC32 && r_type != R_X86_64_PLT32 &&
                r_type != R_X86_64_32 && r_type != R_X86_64_GOTPCREL) {
                em->fail_code = CC_FAIL_RELOC;
                return -1;
            }
            if (r_off + 4 > apply_sz || r_sym == 0 || r_sym >= symn) {
                em->fail_code = CC_FAIL_RELOC;
                return -1;
            }
            tsy = buf + symoff + r_sym * syment;
            t_shndx = u16(tsy + 6);
            t_val = u64(tsy + 8);
            t_name_off = u32(tsy);
            p_at = apply_base + (size_t)r_off;
            ident[0] = 0;
            if (t_shndx < 64 && sec_tex[t_shndx] >= 0) {
                tslot = sec_tex[t_shndx];
                if (t_val > tex_sz[tslot]) {
                    em->fail_code = CC_FAIL_RELOC;
                    return -1;
                }
                s_at = tex_base[tslot] + (size_t)t_val;
            } else if (have_rodata && t_shndx == rodata_i) {
                if (t_val > rodata_sz) {
                    em->fail_code = CC_FAIL_RELOC;
                    return -1;
                }
                s_at = rodata_base + (size_t)t_val;
            } else if (t_shndx == 0) {
                if (copy_ident(ident, strbase, strsz, t_name_off) != 0) {
                    em->fail_code = CC_FAIL_UND;
                    return -1;
                }
                if (resolve_addr(em, ident, &s_at) != 0) {
                    /* M125: defer until later #lib exports land. */
                    if (und_pend_add(p_at, ident, r_add, r_type) != 0) {
                        em->fail_code = CC_FAIL_UND;
                        return -1;
                    }
                    continue;
                }
            } else {
                em->fail_code = CC_FAIL_UND;
                return -1;
            }
            if (patch_at(em, p_at, s_at, r_add, r_type, ident) != 0) {
                em->fail_code = CC_FAIL_RELOC;
                return -1;
            }
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
        int tslot;
        size_t entry;
        if (bind != 1 || type != 2 || shndx >= 64 || sec_tex[shndx] < 0) {
            continue;
        }
        tslot = sec_tex[shndx];
        if (copy_ident(ident, strbase, strsz, name_off) != 0) {
            continue; /* skip long/weak-adjacent names */
        }
        if (value > tex_sz[tslot]) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
        /* musl memcpy/memset often st_size==0; treat as rest of section. */
        if (size == 0) {
            size = (unsigned long long)tex_sz[tslot] - value;
        }
        if (size == 0 || size > tex_sz[tslot] - value) {
            em->fail_code = CC_FAIL_ELF;
            return -1;
        }
        entry = tex_base[tslot] + (size_t)value;
        {
            int added = export_add(em, ident, entry);
            if (added < 0) {
                em->fail_code = CC_FAIL_DUP;
                return -1;
            }
            if (added == 0) {
                continue;
            }
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
