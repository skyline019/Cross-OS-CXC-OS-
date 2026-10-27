#!/usr/bin/env python3
"""Emit as_cli / ld_cli / cc_cli ELF headers for on-disk TDRV/CCDRV plant."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRE = ROOT / "user" / "prebuilt"
INC = ROOT / "include" / "cxc"


def write_header(path: Path, symbol: str, blob: bytes, comment: str) -> None:
    parts = [f"0x{b:02x}" for b in blob]
    lines = [
        "    " + ", ".join(parts[i : i + 12]) + ","
        for i in range(0, len(parts), 12)
    ]
    path.write_text(
        f"/* {comment} */\n#pragma once\n"
        f"static const unsigned char {symbol}[] = {{\n"
        + "\n".join(lines)
        + "\n};\n"
        + f"static const unsigned {symbol}_size = {len(blob)};\n",
        encoding="ascii",
    )


def main() -> int:
    as_path = PRE / "as_cli.elf"
    ld_path = PRE / "ld_cli.elf"
    cc_path = PRE / "cc_cli.elf"
    if not as_path.is_file() or not ld_path.is_file() or not cc_path.is_file():
        print("missing as_cli.elf, ld_cli.elf, or cc_cli.elf", file=sys.stderr)
        return 1
    as_blob = as_path.read_bytes()
    ld_blob = ld_path.read_bytes()
    cc_blob = cc_path.read_bytes()
    assert as_blob[:4] == b"\x7fELF" and ld_blob[:4] == b"\x7fELF"
    assert cc_blob[:4] == b"\x7fELF"
    write_header(
        INC / "as_cli_elf.hpp",
        "as_cli_elf",
        as_blob,
        f"M266 freestanding as.c CLI; bytes={len(as_blob)}",
    )
    write_header(
        INC / "ld_cli_elf.hpp",
        "ld_cli_elf",
        ld_blob,
        f"M267 freestanding ld.c CLI; bytes={len(ld_blob)}",
    )
    write_header(
        INC / "cc_cli_elf.hpp",
        "cc_cli_elf",
        cc_blob,
        f"M275 freestanding disk/cc CLI; bytes={len(cc_blob)}",
    )
    print("as_cli", len(as_blob))
    print("ld_cli", len(ld_blob))
    print("cc_cli", len(cc_blob))
    return 0


if __name__ == "__main__":
    sys.exit(main())
