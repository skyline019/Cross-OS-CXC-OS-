#!/usr/bin/env python3
"""Write M295 shallow-leaf plant_src/*.s and embed header for HARD probe."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "user" / "plant_src"
HDR = ROOT / "include" / "cxc" / "plant_asm_texts.hpp"

# short, stem, primary_sym, body (labels+insns), optional musl-style aliases
LEAVES: list[tuple[str, str, str, str, tuple[str, ...]]] = []


def leaf(
    short: str,
    stem: str,
    sym: str,
    body: str,
    aliases: tuple[str, ...] = (),
) -> None:
    LEAVES.append((short, stem, sym, body, aliases))


leaf(
    "dig",
    "isdigit",
    "isdigit",
    """isdigit:
\tmov %edi, %eax
\tsub $48, %eax
\tcmp $9, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "labs",
    "labs",
    "labs",
    """labs:
\tmov %rdi, %rax
\ttest %rax, %rax
\tjns .Lpos
\tneg %rax
.Lpos:
\tret
""",
)
leaf(
    "iabs",
    "abs",
    "abs",
    """abs:
\tmov %edi, %eax
\ttest %eax, %eax
\tjns .Lpos
\tneg %eax
.Lpos:
\tret
""",
)
leaf(
    "llab",
    "llabs",
    "llabs",
    """llabs:
\tmov %rdi, %rax
\ttest %rax, %rax
\tjns .Lpos
\tneg %rax
.Lpos:
\tret
""",
)
leaf(
    "imax",
    "imaxabs",
    "imaxabs",
    """imaxabs:
\tmov %rdi, %rax
\ttest %rax, %rax
\tjns .Lpos
\tneg %rax
.Lpos:
\tret
""",
)
leaf(
    "iasc",
    "isascii",
    "isascii",
    """isascii:
\tmov %edi, %eax
\tand $128, %eax
\tsete %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "tasc",
    "toascii",
    "toascii",
    """toascii:
\tmov %edi, %eax
\tand $127, %eax
\tret
""",
)
leaf(
    "ispc",
    "isspace",
    "isspace",
    """isspace:
\tcmp $32, %edi
\tje .Ly
\tcmp $9, %edi
\tje .Ly
\tcmp $10, %edi
\tje .Ly
\tcmp $11, %edi
\tje .Ly
\tcmp $12, %edi
\tje .Ly
\tcmp $13, %edi
\tje .Ly
\txor %eax, %eax
\tret
.Ly:
\tmov $1, %eax
\tret
""",
)
leaf(
    "ialp",
    "isalpha",
    "isalpha",
    """isalpha:
\tmov %edi, %eax
\tor $32, %eax
\tsub $97, %eax
\tcmp $25, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "ialn",
    "isalnum",
    "isalnum",
    """isalnum:
\tmov %edi, %eax
\tsub $48, %eax
\tcmp $9, %eax
\tjbe .Ly
\tmov %edi, %eax
\tor $32, %eax
\tsub $97, %eax
\tcmp $25, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
.Ly:
\tmov $1, %eax
\tret
""",
)
leaf(
    "ilwr",
    "islower",
    "islower",
    """islower:
\tmov %edi, %eax
\tsub $97, %eax
\tcmp $25, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "iupr",
    "isupper",
    "isupper",
    """isupper:
\tmov %edi, %eax
\tsub $65, %eax
\tcmp $25, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "iblk",
    "isblank",
    "isblank",
    """isblank:
\tcmp $32, %edi
\tje .Ly
\tcmp $9, %edi
\tje .Ly
\txor %eax, %eax
\tret
.Ly:
\tmov $1, %eax
\tret
""",
)
leaf(
    "igrp",
    "isgraph",
    "isgraph",
    """isgraph:
\tmov %edi, %eax
\tsub $33, %eax
\tcmp $93, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "iprt",
    "isprint",
    "isprint",
    """isprint:
\tmov %edi, %eax
\tsub $32, %eax
\tcmp $94, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
""",
)
leaf(
    "ixdg",
    "isxdigit",
    "isxdigit",
    """isxdigit:
\tmov %edi, %eax
\tsub $48, %eax
\tcmp $9, %eax
\tjbe .Ly
\tmov %edi, %eax
\tor $32, %eax
\tsub $97, %eax
\tcmp $5, %eax
\tsetbe %al
\tmovzbl %al, %eax
\tret
.Ly:
\tmov $1, %eax
\tret
""",
)
leaf(
    "ictl",
    "iscntrl",
    "iscntrl",
    """iscntrl:
\tcmp $127, %edi
\tje .Ly
\tmov %edi, %eax
\tcmp $32, %eax
\tsetb %al
\tmovzbl %al, %eax
\tret
.Ly:
\tmov $1, %eax
\tret
""",
)
leaf(
    "tlwr",
    "tolower",
    "tolower",
    """tolower:
\tmov %edi, %eax
\tcmp $65, %edi
\tjb .Ldone
\tcmp $90, %edi
\tja .Ldone
\tadd $32, %eax
.Ldone:
\tret
""",
)
leaf(
    "tupr",
    "toupper",
    "toupper",
    """toupper:
\tmov %edi, %eax
\tcmp $97, %edi
\tjb .Ldone
\tcmp $122, %edi
\tja .Ldone
\tsub $32, %eax
.Ldone:
\tret
""",
)
leaf(
    "htns",
    "htons",
    "htons",
    """htons:
\tmov %edi, %eax
\trolw $8, %ax
\tmovzwl %ax, %eax
\tret
""",
)
leaf(
    "htnl",
    "htonl",
    "htonl",
    """htonl:
\tmov %edi, %eax
\tbswap %eax
\tret
""",
)
leaf(
    "ntns",
    "ntohs",
    "ntohs",
    """ntohs:
\tmov %edi, %eax
\trolw $8, %ax
\tmovzwl %ax, %eax
\tret
""",
)
leaf(
    "ntnl",
    "ntohl",
    "ntohl",
    """ntohl:
\tmov %edi, %eax
\tbswap %eax
\tret
""",
)
# --- M298 batch2 (≥8 shallow leaves; no deep UND) ---
leaf(
    "ipct",
    "ispunct",
    "ispunct",
    """ispunct:
\tmov %edi, %eax
\tcmp $33, %eax
\tjb .Ln
\tcmp $47, %eax
\tjbe .Ly
\tcmp $58, %eax
\tjb .Ln
\tcmp $64, %eax
\tjbe .Ly
\tcmp $91, %eax
\tjb .Ln
\tcmp $96, %eax
\tjbe .Ly
\tcmp $123, %eax
\tjb .Ln
\tcmp $126, %eax
\tjbe .Ly
.Ln:
\txor %eax, %eax
\tret
.Ly:
\tmov $1, %eax
\tret
""",
)
leaf(
    "iffs",
    "ffs",
    "ffs",
    """ffs:
\tmov %edi, %eax
\ttest %eax, %eax
\tjz .Lz
\tbsf %eax, %eax
\tadd $1, %eax
\tret
.Lz:
\txor %eax, %eax
\tret
""",
)
leaf(
    "bzro",
    "bzero",
    "bzero",
    """bzero:
\ttest %rsi, %rsi
\tjz .Ld
\tmov %rsi, %rcx
\txor %eax, %eax
\trep stosb
.Ld:
\tret
""",
)
leaf(
    "bcmp",
    "bcmp",
    "bcmp",
    """bcmp:
\tcall memcmp
\tret
""",
)
leaf(
    "snln",
    "strnlen",
    "strnlen",
    """strnlen:
\txor %ecx, %ecx
\ttest %rsi, %rsi
\tjz .Ld
.L:
\tmovzbl (%rdi), %eax
\ttest %eax, %eax
\tjz .Ld
\tadd $1, %rdi
\tadd $1, %ecx
\tsub $1, %rsi
\tjnz .L
.Ld:
\tmov %ecx, %eax
\tret
""",
)
leaf(
    "mchr",
    "memchr",
    "memchr",
    """memchr:
\ttest %rdx, %rdx
\tjz .Ln
\tmov %esi, %eax
\tand $255, %eax
\tmov %eax, %ecx
.L:
\tmovzbl (%rdi), %eax
\tcmp %ecx, %eax
\tje .Lf
\tadd $1, %rdi
\tsub $1, %rdx
\tjnz .L
.Ln:
\txor %eax, %eax
\tret
.Lf:
\tmov %rdi, %rax
\tret
""",
)
leaf(
    "slen",
    "strlen",
    "strlen",
    """strlen:
\txor %ecx, %ecx
.L:
\tmovzbl (%rdi), %eax
\ttest %eax, %eax
\tjz .Ld
\tadd $1, %rdi
\tadd $1, %ecx
\tjmp .L
.Ld:
\tmov %ecx, %eax
\tret
""",
)
leaf(
    "schr",
    "strchr",
    "strchr",
    """strchr:
\tmov %esi, %eax
\tand $255, %eax
\tmov %eax, %edx
.L:
\tmovzbl (%rdi), %eax
\tcmp %edx, %eax
\tje .Lf
\ttest %eax, %eax
\tjz .Ln
\tadd $1, %rdi
\tjmp .L
.Lf:
\tmov %rdi, %rax
\tret
.Ln:
\txor %eax, %eax
\tret
""",
)
# M300 batch3: shallow string/mem — drop host .o plants
leaf(
    "mset",
    "memset",
    "memset",
    """memset:
\tpush %rdi
\tmov %esi, %eax
\tand $255, %eax
\tmov %rdx, %rcx
\ttest %rcx, %rcx
\tjz .Ld
\trep stosb
.Ld:
\tpop %rax
\tret
""",
)
leaf(
    "mcmp",
    "memcmp",
    "memcmp",
    """memcmp:
\ttest %rdx, %rdx
\tjz .Lz
.L:
\tmovzbl (%rdi), %eax
\tmovzbl (%rsi), %ecx
\tcmp %ecx, %eax
\tjne .Ld
\tadd $1, %rdi
\tadd $1, %rsi
\tsub $1, %rdx
\tjnz .L
.Lz:
\txor %eax, %eax
\tret
.Ld:
\tsub %ecx, %eax
\tret
""",
)
leaf(
    "scmp",
    "strcmp",
    "strcmp",
    """strcmp:
.L:
\tmovzbl (%rdi), %eax
\tmovzbl (%rsi), %ecx
\tcmp %ecx, %eax
\tjne .Ld
\ttest %eax, %eax
\tjz .Lz
\tadd $1, %rdi
\tadd $1, %rsi
\tjmp .L
.Ld:
\tsub %ecx, %eax
\tret
.Lz:
\txor %eax, %eax
\tret
""",
)
leaf(
    "sncmp",
    "strncmp",
    "strncmp",
    """strncmp:
\ttest %rdx, %rdx
\tjz .Lz
.L:
\tmovzbl (%rdi), %eax
\tmovzbl (%rsi), %ecx
\tcmp %ecx, %eax
\tjne .Ld
\ttest %eax, %eax
\tjz .Lz
\tadd $1, %rdi
\tadd $1, %rsi
\tsub $1, %rdx
\tjnz .L
.Lz:
\txor %eax, %eax
\tret
.Ld:
\tsub %ecx, %eax
\tret
""",
)
leaf(
    "scpy",
    "strcpy",
    "strcpy",
    """strcpy:
\tpush %rdi
.L:
\tmovzbl (%rsi), %eax
\tmov %al, (%rdi)
\tadd $1, %rsi
\tadd $1, %rdi
\ttest %eax, %eax
\tjnz .L
\tpop %rax
\tret
""",
)
leaf(
    "stpc",
    "stpcpy",
    "stpcpy",
    """__stpcpy:
stpcpy:
.L:
\tmovzbl (%rsi), %eax
\tmov %al, (%rdi)
\tadd $1, %rsi
\tadd $1, %rdi
\ttest %eax, %eax
\tjnz .L
\tsub $1, %rdi
\tmov %rdi, %rax
\tret
""",
    aliases=("__stpcpy",),
)
leaf(
    "scat",
    "strcat",
    "strcat",
    """strcat:
\tpush %rdi
.F:
\tmovzbl (%rdi), %eax
\ttest %eax, %eax
\tjz .C
\tadd $1, %rdi
\tjmp .F
.C:
\tmovzbl (%rsi), %eax
\tmov %al, (%rdi)
\tadd $1, %rsi
\tadd $1, %rdi
\ttest %eax, %eax
\tjnz .C
\tpop %rax
\tret
""",
)
leaf(
    "snul",
    "strchrnul",
    "strchrnul",
    """__strchrnul:
strchrnul:
\tmov %esi, %eax
\tand $255, %eax
\tmov %eax, %edx
.L:
\tmovzbl (%rdi), %eax
\tcmp %edx, %eax
\tje .Lf
\ttest %eax, %eax
\tjz .Lf
\tadd $1, %rdi
\tjmp .L
.Lf:
\tmov %rdi, %rax
\tret
""",
    aliases=("__strchrnul",),
)
leaf(
    "atoi",
    "atoi",
    "atoi",
    """atoi:
\txor %eax, %eax
.L:
\tmovzbl (%rdi), %edx
\tsub $48, %rdx
\tcmp $9, %rdx
\tja .D
\timul $10, %eax
\tadd %edx, %eax
\tadd $1, %rdi
\tjmp .L
.D:
\tret
""",
)


def c_escape(s: str) -> str:
    return (
        s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\t", "\\t")
    )


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    lines = [
        "/* Generated by tools/gen_plant_src.py — M295 plant .s texts. */",
        "#pragma once",
        "",
        "struct PlantAsmText {",
        "    const char* disk_path; /* disk/<short>.s */",
        "    const char* text;",
        "};",
        "",
    ]
    entries = []
    for short, stem, sym, body, aliases in LEAVES:
        # musl internal aliases first (strong), then public name
        globs = list(aliases) + [sym]
        # de-dup while preserving order
        seen: set[str] = set()
        glob_lines = []
        for g in globs:
            if g in seen:
                continue
            seen.add(g)
            glob_lines.append(f".globl {g}")
        text = ".text\n" + "\n".join(glob_lines) + "\n" + body
        (OUT / f"{stem}.s").write_text(text, encoding="utf-8")
        var = f"plant_asm_{stem}"
        lines.append(f'static const char {var}[] = "{c_escape(text)}";')
        entries.append(f'    {{"disk/{short}.s", {var}}}')
    lines.append("")
    lines.append(f"enum {{ PLANT_ASM_COUNT = {len(LEAVES)} }};")
    lines.append("static const PlantAsmText plant_asm_texts[PLANT_ASM_COUNT] = {")
    lines.extend(f"{e}," for e in entries)
    lines.append("};")
    lines.append("")
    HDR.parent.mkdir(parents=True, exist_ok=True)
    HDR.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {len(LEAVES)} plant_src + {HDR.relative_to(ROOT)}")
    print("shorts:", ",".join(s for s, _, _, _, _ in LEAVES))


if __name__ == "__main__":
    main()
