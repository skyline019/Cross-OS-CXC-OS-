#!/usr/bin/env python3
"""Write M295 shallow-leaf plant_src/*.s and embed header for HARD probe."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "user" / "plant_src"
HDR = ROOT / "include" / "cxc" / "plant_asm_texts.hpp"

# short job name -> (filename_stem, primary symbol, asm body without header)
LEAVES: list[tuple[str, str, str, str]] = []


def leaf(short: str, stem: str, sym: str, body: str) -> None:
    LEAVES.append((short, stem, sym, body))


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
    for short, stem, sym, body in LEAVES:
        text = f".text\n.globl {sym}\n{body}"
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
    print("shorts:", ",".join(s for s, _, _, _ in LEAVES))


if __name__ == "__main__":
    main()
