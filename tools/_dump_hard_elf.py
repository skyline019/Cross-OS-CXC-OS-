#!/usr/bin/env python3
"""Link full HARD probe and dump ELF + disasm around fault RIP offset."""
from __future__ import annotations

import codecs
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRE = ROOT / "user" / "prebuilt"
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
    nxt = text.find("static const char ", idx + 10)
    chunk = text[idx:nxt]
    parts = re.findall(r'"((?:\\.|[^"\\])*)"', chunk)
    return "".join(codecs.decode(p, "unicode_escape") for p in parts)


def main() -> None:
    step = Path("/tmp/step_probe")
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
    subprocess.check_call(
        ["gcc", "-DCC_HOST", f"-I{ROOT / 'user'}", "-O0", "-o", str(step)]
        + [str(p) for p in srcs]
    )
    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        (tdp / "main.s").write_text(cpp_string("main_s_text"))
        (tdp / "lib.c").write_text(cpp_string("lib_c_text"))
        subprocess.check_call([str(step), "as", str(tdp / "main.s"), str(tdp / "main.o")])
        # compile lib
        libdrv = tdp / "libdrv.c"
        libdrv.write_text(
            '#define CC_HOST 1\n#include "cxc_cc/cc.h"\n#include <stdio.h>\n'
            "#include <stdlib.h>\nint main(int c,char**v){FILE*f=fopen(v[1],\"rb\");"
            "fseek(f,0,2);long n=ftell(f);fseek(f,0,0);char*b=malloc(n+1);fread(b,1,n,f);"
            "fclose(f);b[n]=0;unsigned char*e=0;size_t en=0;"
            "if(cc_compile_obj(b,n,&e,&en))return 1;f=fopen(v[2],\"wb\");fwrite(e,1,en,f);"
            "fclose(f);return 0;}\n"
        )
        libexe = tdp / "libdrv"
        subprocess.check_call(
            ["gcc", "-DCC_HOST", f"-I{ROOT / 'user'}", "-O0", "-o", str(libexe), str(libdrv)]
            + [str(p) for p in srcs[1:]]
        )
        subprocess.check_call([str(libexe), str(tdp / "lib.c"), str(tdp / "lib.o")])
        paths = [str(tdp / "main.o"), str(tdp / "lib.o")] + [
            str(PRE / MAP[k]) for k in MAP
        ]
        # save via custom ld wrapper
        save = tdp / "save.c"
        save.write_text(
            '#define CC_HOST 1\n#include "cxc_cc/cc.h"\n#include <stdio.h>\n'
            "int main(int c,char**v){unsigned char*e=0;size_t n=0;"
            "if(ld_link((const char**)(v+1),c-1,&e,&n)){fprintf(stderr,\"%d\\n\",ld_last_fail_code());return 1;}"
            'FILE*f=fopen("/tmp/hard_out.elf","wb");fwrite(e,1,n,f);fclose(f);'
            'printf("%zu\\n",n);return 0;}\n'
        )
        saveexe = tdp / "save"
        subprocess.check_call(
            ["gcc", "-DCC_HOST", f"-I{ROOT / 'user'}", "-O0", "-o", str(saveexe), str(save)]
            + [str(p) for p in srcs[1:]]
        )
        subprocess.check_call([str(saveexe)] + paths)
    subprocess.check_call(["readelf", "-l", "/tmp/hard_out.elf"])
    # disasm entry.. and around 0x1205
    subprocess.check_call(
        ["objdump", "-d", "--start-address=0x400000", "--stop-address=0x400100", "/tmp/hard_out.elf"]
    )
    subprocess.check_call(
        ["objdump", "-d", "--start-address=0x4011e0", "--stop-address=0x401240", "/tmp/hard_out.elf"]
    )


if __name__ == "__main__":
    main()
