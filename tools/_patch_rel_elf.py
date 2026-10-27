#!/usr/bin/env python3
from pathlib import Path

path = Path("/mnt/e/OS/workspace-study/cxc-os/user/cxc_cc/emit.c")
text = path.read_text()
idx = text.index("/* ET_REL for #lib:")
i = idx
depth = 0
started = False
end = None
while i < len(text):
    if text[i] == "{":
        depth += 1
        started = True
    elif text[i] == "}":
        depth -= 1
        if started and depth == 0:
            end = i + 1
            if end < len(text) and text[end] == "\n":
                end += 1
            break
    i += 1
assert end is not None

new = r'''/* ET_REL for #lib: .text + .rela + .rodata/.data/.bss (M245) + exports. */
int cc_build_rel_elf(CcEmit* em, unsigned char** out, size_t* out_len) {
    enum { MAX_SYM = CC_FN_MAX };
    int order[MAX_SYM];
    int und_order[MAX_SYM];
    size_t sizes[MAX_SYM];
    int nfn = 0;
    int nund = 0;
    int i;
    int j;
    int ncall;
    int nlea;
    int nrela;
    int have_ro;
    int have_rw;
    int have_bss;
    int have_rela;
    int n_dsec;
    size_t ro_len;
    size_t rw_len;
    size_t strtab_len;
    size_t shstr_len;
    size_t off_text;
    size_t off_rela;
    size_t off_ro;
    size_t off_rw;
    size_t off_shstr;
    size_t off_sym;
    size_t off_str;
    size_t off_sh;
    size_t nsym;
    size_t total;
    size_t name_off;
    unsigned char* elf;
    int shnum;
    int shstrndx;
    int sym_shndx;
    int str_shndx;
    int ro_shndx;
    int rw_shndx;
    int bss_shndx;
    int ro_sec_sym;
    int rw_sec_sym;
    int bss_sec_sym;
    int n_sec_sym;
    int fn_sym0;
    int und_sym0;
    int slot;
    /* Unified shstr; unused names are harmless. */
    static const char shstr_all[] =
        "\0.text\0.rela.text\0.rodata\0.data\0.bss\0.shstrtab\0.symtab\0.strtab";
    enum {
        NM_TEXT = 1,
        NM_RELA = 7,
        NM_RO = 18,
        NM_RW = 26,
        NM_BSS = 32,
        NM_SHSTR = 37,
        NM_SYM = 47,
        NM_STR = 55
    };

    if (!em || em->failed || em->code_len == 0 ||
        em->code_len > CC_CODE_MAX) {
        return -1;
    }
    if (cc_data_seal(em) != 0 || em->failed) {
        return -1;
    }
    ro_len = em->ro_len;
    if (ro_len > em->data_len) {
        ro_len = em->data_len;
    }
    rw_len = em->data_len - ro_len;
    if (em->data_len > CC_DATA_MAX || g_nrelocs < 0 ||
        g_nrelocs > CC_RELOC_MAX) {
        return -1;
    }
    if (em->data_len == 0 && em->bss_len == 0 && g_nrelocs != 0) {
        return -1;
    }
    if (em->nfns < 1 || em->nfns > CC_FN_MAX) {
        return -1;
    }
    ncall = em->ncalls;
    nlea = g_nrelocs;
    if (ncall < 0 || ncall > CC_CALL_MAX) {
        return -1;
    }
    nrela = ncall + nlea;
    if (nrela > CC_CALL_MAX + CC_RELOC_MAX) {
        return -1;
    }
    have_ro = ro_len > 0 ? 1 : 0;
    have_rw = rw_len > 0 ? 1 : 0;
    have_bss = em->bss_len > 0 ? 1 : 0;
    have_rela = nrela > 0 ? 1 : 0;
    n_dsec = have_ro + have_rw + have_bss;
    for (i = 0; i < em->nfns; i++) {
        if (!em->fns[i].defined || em->fns[i].name[0] == 0) {
            continue;
        }
        if (nfn >= MAX_SYM) {
            return -1;
        }
        order[nfn++] = i;
    }
    if (nfn < 1) {
        return -1;
    }
    for (i = 0; i < nfn - 1; i++) {
        for (j = i + 1; j < nfn; j++) {
            if (em->fns[order[j]].entry < em->fns[order[i]].entry) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (i = 0; i < nfn; i++) {
        size_t start = em->fns[order[i]].entry;
        size_t end =
            (i + 1 < nfn) ? em->fns[order[i + 1]].entry : em->code_len;
        if (end <= start || end > em->code_len) {
            return -1;
        }
        sizes[i] = end - start;
    }
    for (i = 0; i < em->ncalls; i++) {
        int fi = em->calls[i].fn;
        int seen = 0;
        if (fi < 0 || fi >= em->nfns || em->fns[fi].defined) {
            continue;
        }
        if (em->fns[fi].name[0] == 0) {
            return -1;
        }
        for (j = 0; j < nund; j++) {
            if (und_order[j] == fi) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        if (nund >= MAX_SYM) {
            return -1;
        }
        und_order[nund++] = fi;
    }

    strtab_len = 1;
    for (i = 0; i < nfn; i++) {
        size_t nl = 0;
        while (nl < CC_IDENT_MAX && em->fns[order[i]].name[nl] != 0) {
            nl++;
        }
        if (nl == 0 || em->fns[order[i]].name[nl] != 0) {
            return -1;
        }
        strtab_len += nl + 1;
    }
    for (i = 0; i < nund; i++) {
        size_t nl = 0;
        while (nl < CC_IDENT_MAX && em->fns[und_order[i]].name[nl] != 0) {
            nl++;
        }
        if (nl == 0 || em->fns[und_order[i]].name[nl] != 0) {
            return -1;
        }
        strtab_len += nl + 1;
    }

    shstr_len = sizeof(shstr_all);
    shnum = 1 + 1 + have_rela + n_dsec + 3; /* null+text+...+shstr+sym+str */
    /* Assign section indices. */
    slot = 1; /* .text = 1 */
    slot = 2;
    if (have_rela) {
        slot++;
    }
    ro_shndx = 0;
    rw_shndx = 0;
    bss_shndx = 0;
    if (have_ro) {
        ro_shndx = slot++;
    }
    if (have_rw) {
        rw_shndx = slot++;
    }
    if (have_bss) {
        bss_shndx = slot++;
    }
    shstrndx = slot++;
    sym_shndx = slot++;
    str_shndx = slot;
    if (slot + 1 != shnum) {
        return -1;
    }

    n_sec_sym = n_dsec;
    nsym = (size_t)nfn + (size_t)nund + 1 + (size_t)n_sec_sym;
    ro_sec_sym = have_ro ? 1 : 0;
    rw_sec_sym = have_rw ? (1 + have_ro) : 0;
    bss_sec_sym = have_bss ? (1 + have_ro + have_rw) : 0;
    fn_sym0 = 1 + n_sec_sym;
    und_sym0 = fn_sym0 + nfn;

    off_text = 64;
    if (em->code_len > sizeof(g_elf_buf) - 64) {
        return -1;
    }
    off_rela = off_text + em->code_len;
    if (have_rela) {
        off_ro = off_rela + (size_t)nrela * 24;
    } else {
        off_ro = off_rela;
    }
    off_rw = off_ro + (have_ro ? ro_len : 0);
    off_shstr = off_rw + (have_rw ? rw_len : 0);
    /* .bss is NOBITS — not in file */
    off_sym = (off_shstr + shstr_len + 7u) & ~(size_t)7;
    off_str = off_sym + nsym * 24;
    off_sh = (off_str + strtab_len + 7u) & ~(size_t)7;
    total = off_sh + (size_t)shnum * 64;
    if (total < off_sh || total > sizeof(g_elf_buf)) {
        return -1;
    }
    elf = g_elf_buf;
    memset(elf, 0, total);

    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;
    elf[5] = 1;
    elf[6] = 1;
    elf[16] = 1;
    elf[18] = 62;
    elf[20] = 1;
    {
        unsigned long long shoff = (unsigned long long)off_sh;
        size_t o = 40;
        for (i = 0; i < 8; i++) {
            elf[o + i] = (unsigned char)(shoff >> (8 * i));
        }
    }
    elf[52] = 64;
    elf[58] = 64;
    elf[60] = (unsigned char)shnum;
    elf[62] = (unsigned char)shstrndx;

    memcpy(elf + off_text, em->code, em->code_len);
    if (have_rela) {
        int ri = 0;
        for (i = 0; i < ncall; i++) {
            unsigned char* re = elf + off_rela + (size_t)ri * 24;
            int fi = em->calls[i].fn;
            unsigned r_sym = 0;
            unsigned long long r_off;
            unsigned long long r_info;
            long long r_add = -4;
            int k;
            for (j = 0; j < nfn; j++) {
                if (order[j] == fi) {
                    r_sym = (unsigned)(fn_sym0 + j);
                    break;
                }
            }
            if (r_sym == 0) {
                for (j = 0; j < nund; j++) {
                    if (und_order[j] == fi) {
                        r_sym = (unsigned)(und_sym0 + j);
                        break;
                    }
                }
            }
            if (r_sym == 0) {
                return -1;
            }
            r_off = (unsigned long long)em->calls[i].at;
            r_info = ((unsigned long long)r_sym << 32) | 4ull;
            for (k = 0; k < 8; k++) {
                re[k] = (unsigned char)(r_off >> (8 * k));
                re[8 + k] = (unsigned char)(r_info >> (8 * k));
                re[16 + k] =
                    (unsigned char)((unsigned long long)r_add >> (8 * k));
            }
            ri++;
        }
        for (i = 0; i < nlea; i++) {
            unsigned char* re = elf + off_rela + (size_t)ri * 24;
            unsigned r_sym = 0;
            unsigned long long r_off;
            unsigned long long r_info;
            long long r_add;
            size_t doff = g_relocs[i].data_off;
            int k;
            if (doff < ro_len) {
                if (!ro_sec_sym) {
                    return -1;
                }
                r_sym = (unsigned)ro_sec_sym;
                r_add = (long long)doff - 4;
            } else if (doff < ro_len + rw_len) {
                if (!rw_sec_sym) {
                    return -1;
                }
                r_sym = (unsigned)rw_sec_sym;
                r_add = (long long)(doff - ro_len) - 4;
            } else {
                if (!bss_sec_sym) {
                    return -1;
                }
                r_sym = (unsigned)bss_sec_sym;
                r_add = (long long)(doff - ro_len - rw_len) - 4;
            }
            r_off = (unsigned long long)g_relocs[i].code_off;
            r_info = ((unsigned long long)r_sym << 32) | 2ull;
            for (k = 0; k < 8; k++) {
                re[k] = (unsigned char)(r_off >> (8 * k));
                re[8 + k] = (unsigned char)(r_info >> (8 * k));
                re[16 + k] =
                    (unsigned char)((unsigned long long)r_add >> (8 * k));
            }
            ri++;
        }
    }
    if (have_ro) {
        memcpy(elf + off_ro, em->data, ro_len);
    }
    if (have_rw) {
        memcpy(elf + off_rw, em->data + ro_len, rw_len);
    }
    memcpy(elf + off_shstr, shstr_all, shstr_len);

    memset(elf + off_sym, 0, 24);
    name_off = 1;
    elf[off_str] = 0;
    {
        int ssi = 0;
        if (have_ro) {
            unsigned char* sy = elf + off_sym + (size_t)(1 + ssi) * 24;
            sy[4] = 3;
            sy[6] = (unsigned char)ro_shndx;
            ssi++;
        }
        if (have_rw) {
            unsigned char* sy = elf + off_sym + (size_t)(1 + ssi) * 24;
            sy[4] = 3;
            sy[6] = (unsigned char)rw_shndx;
            ssi++;
        }
        if (have_bss) {
            unsigned char* sy = elf + off_sym + (size_t)(1 + ssi) * 24;
            sy[4] = 3;
            sy[6] = (unsigned char)bss_shndx;
            ssi++;
        }
        (void)ssi;
    }
    {
        size_t so = 1;
        for (i = 0; i < nfn; i++) {
            unsigned char* sy =
                elf + off_sym + (size_t)(fn_sym0 + i) * 24;
            size_t nl = 0;
            while (nl < CC_IDENT_MAX && em->fns[order[i]].name[nl] != 0) {
                nl++;
            }
            memcpy(elf + off_str + so, em->fns[order[i]].name, nl);
            elf[off_str + so + nl] = 0;
            sy[0] = (unsigned char)name_off;
            sy[1] = (unsigned char)(name_off >> 8);
            sy[2] = (unsigned char)(name_off >> 16);
            sy[3] = (unsigned char)(name_off >> 24);
            sy[4] = (unsigned char)((1u << 4) | 2u);
            sy[6] = 1;
            {
                unsigned long long v =
                    (unsigned long long)em->fns[order[i]].entry;
                unsigned long long sz = (unsigned long long)sizes[i];
                int k;
                for (k = 0; k < 8; k++) {
                    sy[8 + k] = (unsigned char)(v >> (8 * k));
                    sy[16 + k] = (unsigned char)(sz >> (8 * k));
                }
            }
            name_off += nl + 1;
            so += nl + 1;
        }
        for (i = 0; i < nund; i++) {
            unsigned char* sy =
                elf + off_sym + (size_t)(und_sym0 + i) * 24;
            size_t nl = 0;
            while (nl < CC_IDENT_MAX && em->fns[und_order[i]].name[nl] != 0) {
                nl++;
            }
            memcpy(elf + off_str + so, em->fns[und_order[i]].name, nl);
            elf[off_str + so + nl] = 0;
            sy[0] = (unsigned char)name_off;
            sy[1] = (unsigned char)(name_off >> 8);
            sy[2] = (unsigned char)(name_off >> 16);
            sy[3] = (unsigned char)(name_off >> 24);
            sy[4] = (unsigned char)((1u << 4) | 2u);
            sy[6] = 0;
            name_off += nl + 1;
            so += nl + 1;
        }
    }

    {
        unsigned char* sh = elf + off_sh;
        memset(sh, 0, (size_t)shnum * 64);
        /* 1 .text */
        {
            unsigned char* s = sh + 64;
            unsigned long long off = (unsigned long long)off_text;
            unsigned long long sz = (unsigned long long)em->code_len;
            int k;
            s[0] = NM_TEXT;
            s[4] = 1;
            s[8] = 6;
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
        }
        slot = 2;
        if (have_rela) {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long off = (unsigned long long)off_rela;
            unsigned long long sz = (unsigned long long)nrela * 24;
            int k;
            s[0] = NM_RELA;
            s[4] = 4;
            s[8] = 0x40;
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[40] = (unsigned char)sym_shndx;
            s[44] = 1;
            s[48] = 8;
            s[56] = 24;
            slot++;
        }
        if (have_ro) {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long off = (unsigned long long)off_ro;
            unsigned long long sz = (unsigned long long)ro_len;
            int k;
            s[0] = NM_RO;
            s[4] = 1;
            s[8] = 2; /* SHF_ALLOC */
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
            slot++;
        }
        if (have_rw) {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long off = (unsigned long long)off_rw;
            unsigned long long sz = (unsigned long long)rw_len;
            int k;
            s[0] = NM_RW;
            s[4] = 1;
            s[8] = 3; /* SHF_WRITE|SHF_ALLOC */
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
            slot++;
        }
        if (have_bss) {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long sz = (unsigned long long)em->bss_len;
            int k;
            s[0] = NM_BSS;
            s[4] = 8; /* SHT_NOBITS */
            s[8] = 3; /* SHF_WRITE|SHF_ALLOC */
            for (k = 0; k < 8; k++) {
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
            slot++;
        }
        {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long off = (unsigned long long)off_shstr;
            unsigned long long sz = (unsigned long long)shstr_len;
            int k;
            s[0] = NM_SHSTR;
            s[4] = 3;
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
            slot++;
        }
        {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long off = (unsigned long long)off_sym;
            unsigned long long sz = (unsigned long long)(nsym * 24);
            int k;
            s[0] = NM_SYM;
            s[4] = 2;
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[40] = (unsigned char)str_shndx;
            s[44] = 1;
            s[48] = 8;
            s[56] = 24;
            slot++;
        }
        {
            unsigned char* s = sh + (size_t)slot * 64;
            unsigned long long off = (unsigned long long)off_str;
            unsigned long long sz = (unsigned long long)strtab_len;
            int k;
            s[0] = NM_STR;
            s[4] = 3;
            for (k = 0; k < 8; k++) {
                s[24 + k] = (unsigned char)(off >> (8 * k));
                s[32 + k] = (unsigned char)(sz >> (8 * k));
            }
            s[48] = 1;
        }
    }

    *out = elf;
    *out_len = total;
    return 0;
}
'''

path.write_text(text[:idx] + new + text[end:])
print("patched", end - idx, "->", len(new))
