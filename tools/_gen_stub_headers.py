#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def wh(path: Path, sym: str, blob: bytes, comment: str) -> None:
    parts = [f"0x{b:02x}" for b in blob]
    lines = ["    " + ", ".join(parts[i : i + 12]) + "," for i in range(0, len(parts), 12)]
    path.write_text(
        f"/* {comment} */\n#pragma once\n"
        f"static const unsigned char {sym}[] = {{\n"
        + "\n".join(lines)
        + "\n};\n"
        + f"static const unsigned {sym}_size = {len(blob)};\n",
        encoding="ascii",
    )
    print(path, len(blob))


wh(
    ROOT / "include/cxc/fat_stub_elf.hpp",
    "fat_stub_elf",
    (ROOT / "user/prebuilt/fat_stub.elf").read_bytes(),
    "M260 stub execve disk/fat.elf with long argv",
)
wh(
    ROOT / "include/cxc/as_stub_elf.hpp",
    "as_stub_elf",
    (ROOT / "user/prebuilt/as_stub.elf").read_bytes(),
    "M261 stub execve disk/as disk/t.s disk/t.o",
)
