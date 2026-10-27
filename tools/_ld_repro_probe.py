#!/usr/bin/env python3
"""Reproduce on-box HARD ld with full main.s + lib.c + planted objs."""
from __future__ import annotations

import codecs
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRE = ROOT / "user" / "prebuilt"
STEP = Path("/tmp/step_probe")

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


def cpp_string(name: str) -> str:
    text = (ROOT / "src" / "user.cpp").read_text()
    idx = text.find(f"static const char {name}[]")
    if idx < 0:
        raise SystemExit(f"{name} not found")
    # next static const char after this one
    nxt = text.find("static const char ", idx + 10)
    chunk = text[idx:nxt]
    parts = re.findall(r'"((?:\\.|[^"\\])*)"', chunk)
    return "".join(codecs.decode(p, "unicode_escape") for p in parts)


def build_step() -> None:
    srcs = [
        ROOT / "tools" / "_ld_repro_step.c",
        ROOT / "user" / "cxc_cc" / "lex.c",
        ROOT / "user" / "cxc_cc" / "parse.c",
        ROOT / "user" / "cxc_cc" / "emit.c",
        ROOT / "user" / "cxc_cc" / "prep.c",
        ROOT / "user" / "cxc_cc" / "elflink.c",
        ROOT / "user" / "cxc_cc" / "as.c",
        ROOT / "user" / "cxc_cc" / "ld.c",
    ]
    cmd = ["gcc", "-DCC_HOST", f"-I{ROOT / 'user'}", "-O0", "-o", str(STEP)]
    cmd += [str(p) for p in srcs]
    subprocess.check_call(cmd)


def main() -> int:
    build_step()
    main_s = cpp_string("main_s_text")
    lib_c = cpp_string("lib_c_text")
    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        (tdp / "main.s").write_text(main_s)
        (tdp / "lib.c").write_text(lib_c)
        print("as main.s ...")
        subprocess.check_call([str(STEP), "as", str(tdp / "main.s"), str(tdp / "main.o")])
        # Compile lib via a tiny host helper using cc_compile_obj — write C driver.
        lib_driver = tdp / "libdrv.c"
        lib_driver.write_text(
            r"""
#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
  FILE* f; char* buf; long n; unsigned char* elf=0; size_t en=0;
  f=fopen(argv[1],"rb"); if(!f) return 1;
  fseek(f,0,SEEK_END); n=ftell(f); fseek(f,0,SEEK_SET);
  buf=malloc(n+1); fread(buf,1,n,f); fclose(f); buf[n]=0;
  if (cc_compile_obj(buf,(size_t)n,&elf,&en)!=0||!elf) {
    fprintf(stderr,"cc_compile_obj fail code=%d\n", cc_last_fail_code());
    return 1;
  }
  f=fopen(argv[2],"wb"); fwrite(elf,1,en,f); fclose(f);
  return 0;
}
"""
        )
        libexe = tdp / "libdrv"
        cmd = [
            "gcc",
            "-DCC_HOST",
            f"-I{ROOT / 'user'}",
            "-O0",
            "-o",
            str(libexe),
            str(lib_driver),
            str(ROOT / "user" / "cxc_cc" / "lex.c"),
            str(ROOT / "user" / "cxc_cc" / "parse.c"),
            str(ROOT / "user" / "cxc_cc" / "emit.c"),
            str(ROOT / "user" / "cxc_cc" / "prep.c"),
            str(ROOT / "user" / "cxc_cc" / "elflink.c"),
            str(ROOT / "user" / "cxc_cc" / "as.c"),
            str(ROOT / "user" / "cxc_cc" / "ld.c"),
        ]
        subprocess.check_call(cmd)
        print("cc lib.c ...")
        subprocess.check_call([str(libexe), str(tdp / "lib.c"), str(tdp / "lib.o")])

        paths = [str(tdp / "main.o"), str(tdp / "lib.o")]
        for disk, pre in MAP.items():
            p = PRE / pre
            if not p.is_file():
                print("MISSING", p)
                return 1
            paths.append(str(p))
        print("ld npaths", len(paths))
        r = subprocess.run([str(STEP), "ld"] + paths, capture_output=True, text=True)
        print(r.stdout)
        print(r.stderr)
        if r.returncode != 0:
            print("ld FAIL", r.returncode)
            # also try import one-by-one
            r2 = subprocess.run(
                [str(STEP), "import"] + paths, capture_output=True, text=True
            )
            print("--- import ---")
            print(r2.stdout[-2000:])
            print(r2.stderr)
            return 1
        print("PROBE HARD link OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
