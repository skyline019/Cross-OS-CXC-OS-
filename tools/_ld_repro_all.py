#!/usr/bin/env python3
"""Host-side full HARD link.rsp repro using planted musl objs + as main."""
from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRE = ROOT / "user" / "prebuilt"

# Map disk names from probe link.rsp → prebuilt files (lib.o/main.o built below).
MAP = {
    "atoi.o": "musl_atoi.o",
    "scmp.o": "musl_strcmp.o",
    "labs.o": "musl_labs.o",
    "dig.o": "musl_isdigit.o",
    "scat.o": "musl_strcat.o",
    "schr.o": "musl_strchr.o",
    "snul.o": "musl_strchrnul.o",
    "scpy.o": "musl_strcpy.o",
    "stpc.o": "musl_stpcpy.o",
    "sncmp.o": "musl_strncmp.o",
    "mcmp.o": "musl_memcmp.o",
    "mchr.o": "musl_memchr.o",
    "tlwr.o": "musl_tolower.o",
    "tupr.o": "musl_toupper.o",
    "ispc.o": "musl_isspace.o",
    "ialp.o": "musl_isalpha.o",
    "ialn.o": "musl_isalnum.o",
    "ilwr.o": "musl_islower.o",
    "iupr.o": "musl_isupper.o",
    "iblk.o": "musl_isblank.o",
    "igrp.o": "musl_isgraph.o",
    "iprt.o": "musl_isprint.o",
    "ixdg.o": "musl_isxdigit.o",
    "ictl.o": "musl_iscntrl.o",
    "ipct.o": "musl_ispunct.o",
    "iabs.o": "musl_abs.o",
    "bzro.o": "musl_bzero.o",
    "snln.o": "musl_strnlen.o",
    "iasc.o": "musl_isascii.o",
    "llab.o": "musl_llabs.o",
    "tasc.o": "musl_toascii.o",
    "imax.o": "musl_imaxabs.o",
    "bcmp.o": "musl_bcmp.o",
    "iffs.o": "musl_ffs.o",
    "htns.o": "musl_htons.o",
    "htnl.o": "musl_htonl.o",
    "ntns.o": "musl_ntohs.o",
    "ntnl.o": "musl_ntohl.o",
    "mset.o": "musl_memset.o",
    "scsp.o": "musl_strcspn.o",
    "sspn.o": "musl_strspn.o",
    "spbk.o": "musl_strpbrk.o",
    "srch.o": "musl_strrchr.o",
    "mmov.o": "musl_memmove.o",
    "sdup.o": "musl_strdup.o",
    "stol.o": "musl_strtol.o",
    "iscn.o": "musl_intscan.o",
    "shgt.o": "musl_shgetc.o",
    "mrch.o": "musl_memrchr.o",
    "hstb.o": "hard_stubs.o",
    "mcpy.o": "musl_memcpy.o",
    "slen.o": "musl_strlen.o",
}

MAIN_S = r"""
.text
.globl main
main:
push %rbp
mov %rsp, %rbp
lea s42(%rip), %rax
mov %rax, %rdi
xor %esi, %esi
mov $10, %edx
call strtol
leave
and $255, %eax
ret
.rodata
s42:
.asciz "42"
"""

LIB_S = r"""
.text
.globl ans
ans:
mov $42, %eax
ret
"""


def main() -> int:
    cc_srcs = [
        ROOT / "tools" / "_ld_repro_step.c",
        ROOT / "user" / "cxc_cc" / "lex.c",
        ROOT / "user" / "cxc_cc" / "parse.c",
        ROOT / "user" / "cxc_cc" / "emit.c",
        ROOT / "user" / "cxc_cc" / "prep.c",
        ROOT / "user" / "cxc_cc" / "elflink.c",
        ROOT / "user" / "cxc_cc" / "as.c",
        ROOT / "user" / "cxc_cc" / "ld.c",
    ]
    # Ensure step driver exists.
    step = ROOT / "tools" / "_ld_repro_step.c"
    if not step.is_file():
        raise SystemExit("missing _ld_repro_step.c")

    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        # Build driver once; it reads path list from argv.
        exe = tdp / "step"
        cmd = ["gcc", "-DCC_HOST", "-I" + str(ROOT / "user"), "-O0", "-o", str(exe)]
        cmd += [str(p) for p in cc_srcs]
        subprocess.check_call(cmd)

        # Assemble main/lib via driver mode.
        (tdp / "main.s").write_text(MAIN_S)
        (tdp / "lib.s").write_text(LIB_S)
        subprocess.check_call([str(exe), "as", str(tdp / "main.s"), str(tdp / "main.o")])
        subprocess.check_call([str(exe), "as", str(tdp / "lib.s"), str(tdp / "lib.o")])

        paths = [str(tdp / "main.o"), str(tdp / "lib.o")]
        for disk_name, pre_name in MAP.items():
            src = PRE / pre_name
            if not src.is_file():
                print("MISSING", src)
                return 1
            paths.append(str(src))

        # Import one-by-one then full ld.
        subprocess.check_call([str(exe), "import"] + paths)
        subprocess.check_call([str(exe), "ld"] + paths)
        print("FULL HARD link OK, npaths=", len(paths))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
