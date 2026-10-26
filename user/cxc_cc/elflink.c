/* Import ET_REL FUNC symbols into the current emit image (M106/M107). */
#include "cxc_cc/cc.h"

#ifdef CC_HOST
#include <stdio.h>
#endif

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
        unsigned link = u32(sh + 40);
        unsigned entsz = (unsigned)u64(sh + 56);
        unsigned off = (unsigned)u64(sh + 24);
        unsigned sz = (unsigned)u64(sh + 32);
        if (typ == 2) { /* SHT_SYMTAB */
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
        }
    }
    if (symn == 0 || symn > 64 || stroff >= n || strsz > n - stroff) {
        return -1;
    }
    if (symoff >= n || n - symoff < (size_t)symn * syment) {
        return -1;
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
        const char* nm;
        size_t nl = 0;
        int fi;
        const unsigned char* sh;
        unsigned sec_off;
        unsigned sec_sz;
        char ident[CC_IDENT_MAX + 1];
        if (bind != 1 || type != 2 || shndx == 0 || shndx >= shnum ||
            size == 0 || size > 256) {
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
        sh = buf + shoff + shndx * shentsize;
        sec_off = (unsigned)u64(sh + 24);
        sec_sz = (unsigned)u64(sh + 32);
        if (value > sec_sz || size > sec_sz - value) {
            return -1;
        }
        if (sec_off > n || n - sec_off < sec_sz) {
            return -1;
        }
        fi = cc_fn_find(em, ident);
        if (fi < 0) {
            continue;
        }
        if (em->fns[fi].defined) {
            return -1;
        }
        em->fns[fi].entry = em->code_len;
        cc_emit_bytes(em, buf + sec_off + (size_t)value, (size_t)size);
        em->fns[fi].defined = 1;
        em->fns[fi].sysv = 1;
    }
    return em->failed ? -1 : 0;
}
